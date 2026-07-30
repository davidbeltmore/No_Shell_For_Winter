"""Migrate two EFCharacterCreation WBP packages through UE 5.7 AssetTools.

This runs only inside the isolated target-hosted UE 5.7 harness. The harness
contains explicit staging copies, so neither the live 5.7 source project nor
its plugin directory is mounted or writable by the process.
"""

import datetime
import hashlib
import json
import os

import unreal


DESTINATION = os.environ.get("CODEX_EFCC_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_EFCC_EVIDENCE", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
PACKAGES = (
    "/EFCharacterCreation/UI/WBP_EFCharacterCreationRoot",
    "/EFCharacterCreation/UI/WBP_EFMorphSlider",
)


def fail(message):
    unreal.log_error("CODEX_EFCC_WIDGET_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


if not DESTINATION or not EVIDENCE_PATH or not EXPECTED_TARGET_ROOT:
    fail("CODEX_EFCC_DESTINATION, CODEX_EFCC_EVIDENCE, and CODEX_EXPECTED_TARGET_ROOT are required")

destination = os.path.realpath(DESTINATION)
target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
expected_destination = os.path.realpath(
    os.path.join(target_root, "Plugins", "EFCharacterCreation", "Content")
)
if destination.lower() != expected_destination.lower():
    fail("Destination invariant failed: {!r} != {!r}".format(destination, expected_destination))
if not os.path.isdir(destination):
    fail("Destination Content folder does not exist: " + destination)
if not os.path.isfile(os.path.join(os.path.dirname(destination), "EFCharacterCreation.uplugin")):
    fail("Destination is not owned by the target EFCharacterCreation plugin")
if not os.path.realpath(EVIDENCE_PATH).lower().startswith(
    os.path.realpath(os.path.join(target_root, "Saved", "Migration")).lower() + os.sep
):
    fail("Evidence path escapes target Saved/Migration")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/EFCharacterCreation"], True)
registry.wait_for_completion()

for package_name in PACKAGES:
    assets = registry.get_assets_by_package_name(package_name, include_only_on_disk_assets=True)
    expected_asset_name = package_name.rsplit("/", 1)[-1]
    asset_names = [str(asset.asset_name) for asset in assets]
    if not assets or expected_asset_name not in asset_names:
        fail(
            "Expected staged asset {} in package {} but found {}".format(
                expected_asset_name, package_name, asset_names
            )
        )
    unreal.log(
        "CODEX_EFCC_STAGED_PACKAGE: {} records={}".format(
            package_name, asset_names
        )
    )

preexisting = []
for root, _directories, files in os.walk(destination):
    preexisting.extend(os.path.join(root, name) for name in files if name.lower().endswith((".uasset", ".umap")))
if preexisting:
    fail("Destination already contains packages: " + repr(preexisting))

options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", False)
options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
options.set_editor_property("orphan_folder", "")

unreal.log("CODEX_EFCC_WIDGET_MIGRATION_BEGIN: " + repr(PACKAGES))
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(PACKAGES), destination, options
)

expected_files = (
    os.path.join(destination, "UI", "WBP_EFCharacterCreationRoot.uasset"),
    os.path.join(destination, "UI", "WBP_EFMorphSlider.uasset"),
)
for path in expected_files:
    if not os.path.isfile(path):
        fail("AssetTools did not create expected package: " + path)

all_destination_packages = []
for root, _directories, files in os.walk(destination):
    all_destination_packages.extend(
        os.path.realpath(os.path.join(root, name))
        for name in files
        if name.lower().endswith((".uasset", ".umap"))
    )
if sorted(path.lower() for path in all_destination_packages) != sorted(
    path.lower() for path in expected_files
):
    fail("Unexpected migrated package set: " + repr(all_destination_packages))

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_MIGRATION_PASS",
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "harness_project": unreal.Paths.get_project_file_path(),
    "destination": destination,
    "packages": [
        {
            "package_name": package_name,
            "file": path,
            "length": os.path.getsize(path),
            "sha256": sha256(path),
        }
        for package_name, path in zip(PACKAGES, expected_files)
    ],
    "asset_conflict": "SKIP",
    "dependencies_enabled": True,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFCC_WIDGET_MIGRATION_PASS: " + EVIDENCE_PATH)
