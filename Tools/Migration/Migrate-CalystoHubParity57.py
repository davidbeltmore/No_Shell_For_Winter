"""Exact three-package UE 5.7 AssetTools migration for HUB parity."""

import datetime
import hashlib
import json
import os

import unreal


RECEIPT = os.path.realpath(os.environ.get("CODEX_CALYSTO_HUB_RECEIPT", "").strip())
OUTPUT = os.path.realpath(os.environ.get("CODEX_CALYSTO_HUB_MIGRATION_OUTPUT", "").strip())
ALLOW_REPAIR = os.environ.get("CODEX_CALYSTO_HUB_ALLOW_REPAIR", "").strip() == "1"


def fail(message):
    unreal.log_error("CODEX_CALYSTO_HUB_MIGRATION57_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


if not RECEIPT or not OUTPUT:
    fail("Receipt and output environment variables are required")
if not unreal.SystemLibrary.get_engine_version().startswith("5.7."):
    fail("Expected Unreal Engine 5.7")
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "calystohubparity57harness.uproject":
    fail("Expected the isolated CalystoHubParity57 harness")

with open(RECEIPT, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if receipt.get("status") != "CALYSTO_HUB_PARITY57_HARNESS_PASS":
    fail("Harness receipt is not PASS")
rows = list(receipt.get("packages", []))
if len(rows) != 3 or int(receipt.get("package_count", -1)) != 3:
    fail("Expected exactly three migration packages")

target_content = os.path.realpath(os.path.join(receipt["target_root"], "Content"))
packages = sorted(row["package"] for row in rows)
target_before = []
for row in rows:
    source = os.path.realpath(row["source"])
    staged = os.path.realpath(row["staged"])
    target = os.path.realpath(row["target"])
    expected = (int(row["length"]), str(row["sha256"]).upper())
    for path in (source, staged):
        if not os.path.isfile(path) or (os.path.getsize(path), sha256(path)) != expected:
            fail("Source/staged package differs: " + row["package"])
    if os.path.exists(target):
        if not ALLOW_REPAIR:
            fail("Target collision appeared: " + target)
        target_before.append({
            "package": row["package"],
            "file": target,
            "length": os.path.getsize(target),
            "sha256": sha256(target),
        })

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/ExportedAnimations"], True)
registry.wait_for_completion()
for package in packages:
    if not list(registry.get_assets_by_package_name(package, include_only_on_disk_assets=True)):
        fail("Harness registry package is absent: " + package)

dirty_content = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
dirty_maps = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
if dirty_content or dirty_maps:
    fail("Harness began dirty")

options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", True)
options.set_editor_property(
    "asset_conflict",
    unreal.AssetMigrationConflict.OVERWRITE if ALLOW_REPAIR else unreal.AssetMigrationConflict.SKIP,
)
options.set_editor_property("orphan_folder", "")
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(packages, target_content, options)

target_rows = []
for row in rows:
    target = os.path.realpath(row["target"])
    if not os.path.isfile(target) or os.path.getsize(target) <= 0:
        fail("Migrated target is absent or empty: " + row["package"])
    target_rows.append({
        "package": row["package"],
        "file": target,
        "length": os.path.getsize(target),
        "sha256": sha256(target),
    })
    for extension in (".uexp", ".ubulk", ".uptnl"):
        if os.path.exists(os.path.splitext(target)[0] + extension):
            fail("Unexpected target sidecar: " + target + extension)

if list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()):
    fail("AssetTools left dirty content packages")
if list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()):
    fail("AssetTools left dirty map packages")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "CALYSTO_HUB_PARITY57_ASSETTOOLS_PASS",
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project": project_file,
    "receipt": RECEIPT,
    "package_count": 3,
    "packages": target_rows,
    "target_before_repair": target_before,
    "ignore_dependencies": True,
    "asset_conflict": "OVERWRITE_EXACT_RETRY_SET" if ALLOW_REPAIR else "SKIP_AFTER_ABSENCE_GATE",
    "raw_target_asset_copy": False,
    "source_assets_unchanged": True,
}
os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
temporary = OUTPUT + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, OUTPUT)
unreal.log("CODEX_CALYSTO_HUB_MIGRATION57_PASS: " + OUTPUT)
