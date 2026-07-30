"""Migrate the three source-owned 0001Scene packages from the detached UE 5.7 harness.

The script refuses to run against LustAsDeadlySin or against live target Content.
It migrates exact package seeds with dependencies disabled so protected DAZ/ACF
content cannot be overwritten.
"""

import datetime
import hashlib
import json
import os

import unreal


PACKAGES = (
    "/Game/ExportedAnimations/Together/0001Scene",
    "/Game/ExportedAnimations/SexAnimations/AS_DoggyClassic_1_Female",
    "/Game/ExportedAnimations/M_SexAnimations/AS_DoggyClassic_1_Male_Corrected",
)
PACKAGE_EXTENSIONS = (".uasset", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_ENEMY_INTIMACY57_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    try:
        return os.path.commonpath(
            [os.path.realpath(path), os.path.realpath(root)]
        ).lower() == os.path.realpath(root).lower()
    except ValueError:
        return False


def package_files(content_root, package):
    relative_stem = package.removeprefix("/Game/").replace("/", os.sep)
    rows = []
    for extension in PACKAGE_EXTENSIONS:
        path = os.path.realpath(os.path.join(content_root, relative_stem + extension))
        if os.path.isfile(path):
            rows.append(
                {
                    "path": path,
                    "relative": os.path.relpath(path, content_root).replace(os.sep, "/"),
                    "length": os.path.getsize(path),
                    "sha256": sha256(path),
                }
            )
    return rows


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "bulkprojectcontent57harness.uproject":
    fail("This migration may run only in the detached BulkProjectContent57Harness")

target_root = os.path.realpath(
    os.environ.get("CODEX_ENEMY_INTIMACY_TARGET_ROOT", "").strip()
)
evidence_path = os.path.realpath(
    os.environ.get("CODEX_ENEMY_INTIMACY57_EVIDENCE", "").strip()
)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
target_content = os.path.realpath(os.path.join(target_root, "Content"))
target_saved = os.path.realpath(os.path.join(target_root, "Saved"))

if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Target root is not NoShellForWinter")
if not is_under(harness_content, os.path.join(target_root, "Saved", "Migration")):
    fail("Detached harness escapes target Saved/Migration")
if is_under(harness_content, target_content):
    fail("Harness Content unexpectedly overlaps live target Content")
if not is_under(evidence_path, target_saved):
    fail("Evidence path must remain below target Saved")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()

harness_rows = {}
for package in PACKAGES:
    asset = unreal.load_asset(package)
    if asset is None:
        fail("Harness package failed to load: " + package)
    files = package_files(harness_content, package)
    if not files or not any(row["relative"].lower().endswith(".uasset") for row in files):
        fail("Harness package files are incomplete: " + package)
    harness_rows[package] = files
    if package_files(target_content, package):
        fail("Target collision exists at legacy staging path: " + package)

options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", True)
options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
options.set_editor_property("orphan_folder", "")

unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(PACKAGES), target_content, options
)

target_rows = {}
for package in PACKAGES:
    files = package_files(target_content, package)
    if not files:
        fail("Target package was not created: " + package)
    target_rows[package] = files
    harness_by_relative = {row["relative"].lower(): row for row in harness_rows[package]}
    target_by_relative = {row["relative"].lower(): row for row in files}
    if set(harness_by_relative) != set(target_by_relative):
        fail("Target sidecar set differs for " + package)
    for relative, source_row in harness_by_relative.items():
        target_row = target_by_relative[relative]
        if (
            target_row["length"] != source_row["length"]
            or target_row["sha256"] != source_row["sha256"]
        ):
            fail("Migrated bytes differ for " + relative)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_DETACHED_ENEMY_INTIMACY_ASSETTOOLS_MIGRATION_PASS",
    "engine_version": engine_version,
    "harness_project": project_file,
    "harness_content": harness_content,
    "target_root": target_root,
    "packages": list(PACKAGES),
    "harness_files": harness_rows,
    "target_files": target_rows,
    "ignore_dependencies": True,
    "protected_content_overwritten": False,
    "source_project_opened": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
unreal.log("CODEX_ENEMY_INTIMACY57_MIGRATION_PASS: " + evidence_path)
