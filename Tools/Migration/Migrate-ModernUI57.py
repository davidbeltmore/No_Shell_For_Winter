"""Migrate the manifest-frozen 127-package modern UI batch with UE 5.7 AssetTools."""

import datetime
import hashlib
import json
import os

import unreal


DESTINATION = os.environ.get("CODEX_MODERN_UI_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_MODERN_UI_EVIDENCE", "").strip()
RECEIPT_PATH = os.environ.get("CODEX_MODERN_UI_RECEIPT", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()
ROOT_COUNTS = {
    "/Game/_Game/Widgets/Chronicle": 23,
    "/Game/_Game/Widgets/InnerState": 24,
    "/Game/_Game/Widgets/Status": 17,
    "/Game/_Game/Widgets/Attributes": 27,
    "/Game/_Game/Widgets/SinfulAscensionAltar": 36,
}
EXPECTED_CLASS_COUNTS = {
    "WidgetBlueprint": 66,
    "Texture2D": 54,
    "FontFace": 7,
}


def fail(message):
    unreal.log_error("CODEX_MODERN_UI57_FAIL: " + message)
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


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (
        DESTINATION,
        EVIDENCE_PATH,
        RECEIPT_PATH,
        EXPECTED_TARGET_ROOT,
        EXPECTED_HARNESS_CONTENT,
    )
):
    fail("All modern UI migration environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
destination = os.path.realpath(DESTINATION)
if destination.lower() != os.path.realpath(
    os.path.join(target_root, "Content")
).lower():
    fail("Destination is not the audited target Content root")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Destination is not the audited target project")

harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if harness_content.lower() != os.path.realpath(
    EXPECTED_HARNESS_CONTENT
).lower():
    fail("Commandlet is not running inside the audited harness")
if harness_content.lower().startswith(destination.lower() + os.sep):
    fail("Harness Content unexpectedly lives below target Content")

receipt_path = os.path.realpath(RECEIPT_PATH)
expected_phase_root = os.path.realpath(
    os.path.join(target_root, "Saved", "Migration", "Phase4")
)
if not receipt_path.lower().startswith(expected_phase_root.lower() + os.sep):
    fail("Harness receipt escapes target Saved/Migration/Phase4")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if receipt.get("status") != "ISOLATED_MODERN_UI_HARNESS_PASS":
    fail("Harness receipt status is not PASS")
packages = tuple(
    sorted(row["package"] for row in receipt.get("staged_assets", []))
)
if len(packages) != 127 or len(set(packages)) != 127:
    fail("Harness receipt does not contain 127 unique packages")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(list(ROOT_COUNTS), True)
registry.wait_for_completion()
registered = {}
for package in packages:
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    primary_assets = [
        asset
        for asset in assets
        if class_name(asset) in EXPECTED_CLASS_COUNTS
    ]
    if len(primary_assets) != 1:
        fail("Expected exactly one registered primary asset for " + package)
    registered[package] = class_name(primary_assets[0])
actual_root_counts = {
    root: sum(1 for package in packages if package.startswith(root + "/"))
    for root in ROOT_COUNTS
}
if actual_root_counts != ROOT_COUNTS:
    fail("Harness receipt root counts differ from the audited baseline")

actual_class_counts = {}
for value in registered.values():
    actual_class_counts[value] = actual_class_counts.get(value, 0) + 1
if actual_class_counts != EXPECTED_CLASS_COUNTS:
    fail("Unexpected modern UI class counts: " + repr(actual_class_counts))

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
for package in packages:
    direct = sorted({str(item) for item in registry.get_dependencies(package, options)})
    dependencies[package] = direct
    unexpected_game_dependencies.update(
        item for item in direct if item.startswith("/Game/") and item not in packages
    )
if unexpected_game_dependencies:
    fail(
        "Unexpected /Game dependencies: "
        + repr(sorted(unexpected_game_dependencies))
    )

expected_files = {
    package_file(destination, package).lower(): package for package in packages
}
for path in expected_files:
    if os.path.exists(path):
        fail("Refusing to overwrite destination package: " + path)

before = list_packages(destination)
migration_options = unreal.MigrationOptions()
migration_options.set_editor_property("prompt", False)
migration_options.set_editor_property("ignore_dependencies", True)
migration_options.set_editor_property(
    "asset_conflict", unreal.AssetMigrationConflict.SKIP
)
migration_options.set_editor_property("orphan_folder", "")

unreal.log("CODEX_MODERN_UI57_BEGIN: packages=127")
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(packages), destination, migration_options
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
    package = expected_files[path]
    rows.append(
        {
            "package": package,
            "class": registered[package],
            "file": path,
            "length": os.path.getsize(path),
            "sha256": sha256(path),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_MODERN_UI_MIGRATION_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "destination": destination,
    "package_count": len(rows),
    "root_counts": actual_root_counts,
    "class_counts": actual_class_counts,
    "packages": rows,
    "dependencies": dependencies,
    "unexpected_game_dependencies": [],
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "ignore_dependencies": True,
}
evidence_path = os.path.realpath(EVIDENCE_PATH)
if not evidence_path.lower().startswith(
    os.path.realpath(os.path.join(target_root, "Saved", "Migration")).lower()
    + os.sep
):
    fail("Evidence path escapes target Saved/Migration")
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_MODERN_UI57_PASS: " + evidence_path)
