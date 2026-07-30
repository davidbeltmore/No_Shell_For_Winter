"""Migrate the exact 12-package Defeat visual batch via UE 5.7 AssetTools."""

import datetime
import hashlib
import json
import os

import unreal


PACKAGES = (
    "/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensed",
    "/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensedMedium",
    "/Game/UI/Defeat/Struggle/Fonts/FF_Cinzel",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_Arrow",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_BackdropVignette",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_GlowStreak",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_MainPanel",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_Noise",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetChamber",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetPulse",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetRing",
    "/Game/UI/Defeat/Struggle/Textures/T_Struggle_TopPanel",
)
DESTINATION = os.environ.get("CODEX_DEFEAT_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_DEFEAT_EVIDENCE", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get("CODEX_EXPECTED_HARNESS_CONTENT", "").strip()


def fail(message):
    unreal.log_error("CODEX_DEFEAT_VISUAL57_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def package_file(content_root, package_name):
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def list_packages(content_root):
    result = set()
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if name.lower().endswith((".uasset", ".umap")):
                result.add(os.path.realpath(os.path.join(root, name)).lower())
    return result


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all((DESTINATION, EVIDENCE_PATH, EXPECTED_TARGET_ROOT, EXPECTED_HARNESS_CONTENT)):
    fail("All Defeat migration environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
destination = os.path.realpath(DESTINATION)
expected_destination = os.path.realpath(os.path.join(target_root, "Content"))
if destination.lower() != expected_destination.lower():
    fail("Destination is not the audited target Content root")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Destination is not the audited target project")

harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("Commandlet is not running inside the audited harness")
if harness_content.lower().startswith(destination.lower() + os.sep):
    fail("Harness Content unexpectedly lives below target Content")

evidence_path = os.path.realpath(EVIDENCE_PATH)
evidence_root = os.path.realpath(os.path.join(target_root, "Saved", "Migration"))
if not evidence_path.lower().startswith(evidence_root.lower() + os.sep):
    fail("Evidence path escapes target Saved/Migration")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/UI/Defeat"], True)
registry.wait_for_completion()
options = unreal.AssetRegistryDependencyOptions()
for name in (
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
):
    try:
        options.set_editor_property(name, True)
    except Exception:
        pass

dependencies = {}
unexpected_game_dependencies = set()
for package in PACKAGES:
    if not registry.get_assets_by_package_name(package, include_only_on_disk_assets=True):
        fail("Staged package is not registered: " + package)
    direct = sorted({str(item) for item in registry.get_dependencies(package, options)})
    dependencies[package] = direct
    unexpected_game_dependencies.update(
        item for item in direct if item.startswith("/Game/") and item not in PACKAGES
    )
if unexpected_game_dependencies:
    fail("Unexpected /Game dependencies: " + repr(sorted(unexpected_game_dependencies)))

expected_files = {package_file(destination, package).lower(): package for package in PACKAGES}
for path in expected_files:
    if os.path.exists(path):
        fail("Refusing to overwrite destination package: " + path)

before = list_packages(destination)
migration_options = unreal.MigrationOptions()
migration_options.set_editor_property("prompt", False)
migration_options.set_editor_property("ignore_dependencies", False)
migration_options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
migration_options.set_editor_property("orphan_folder", "")

unreal.log("CODEX_DEFEAT_VISUAL57_BEGIN: " + repr(PACKAGES))
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(PACKAGES), destination, migration_options
)

created = list_packages(destination) - before
if created != set(expected_files):
    fail(
        "Unexpected migrated package set; created={!r}, expected={!r}".format(
            sorted(created), sorted(expected_files)
        )
    )

rows = []
for path in sorted(created):
    rows.append(
        {
            "package": expected_files[path],
            "file": path,
            "length": os.path.getsize(path),
            "sha256": sha256(path),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_DEFEAT_VISUAL_MIGRATION_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "destination": destination,
    "dependencies": dependencies,
    "packages": rows,
    "created_package_count": len(rows),
    "unexpected_game_dependencies": [],
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DEFEAT_VISUAL57_PASS: " + evidence_path)
