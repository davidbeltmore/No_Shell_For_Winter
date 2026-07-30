"""Migrate the exact LADS TattooShop BP_TSChar closure with UE 5.7 AssetTools.

Required environment:
  CODEX_TATTOOSHOP_RECEIPT
  CODEX_TATTOOSHOP_TARGET_ROOT
  CODEX_TATTOOSHOP_MIGRATION_EVIDENCE
  CODEX_TATTOOSHOP_EXPECTED_HARNESS_CONTENT
"""

import datetime
import hashlib
import json
import os

import unreal


RECEIPT_VALUE = os.environ.get("CODEX_TATTOOSHOP_RECEIPT", "").strip()
TARGET_ROOT_VALUE = os.environ.get("CODEX_TATTOOSHOP_TARGET_ROOT", "").strip()
EVIDENCE_VALUE = os.environ.get("CODEX_TATTOOSHOP_MIGRATION_EVIDENCE", "").strip()
EXPECTED_HARNESS_VALUE = os.environ.get(
    "CODEX_TATTOOSHOP_EXPECTED_HARNESS_CONTENT", ""
).strip()
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")
EXPECTED_COUNT = 50


def fail(message):
    unreal.log_error("CODEX_TATTOOSHOP57_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    try:
        return (
            os.path.commonpath([os.path.realpath(path), os.path.realpath(root)]).lower()
            == os.path.realpath(root).lower()
        )
    except ValueError:
        return False


def inventory(content_root):
    result = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = os.path.realpath(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/").lower()
            stat = os.stat(path)
            result[relative] = {"length": stat.st_size, "mtime_ns": stat.st_mtime_ns}
    return result


def dirty_packages(function_name):
    values = getattr(unreal.EditorLoadingAndSavingUtils, function_name)()
    names = []
    for value in values:
        try:
            names.append(value.get_name())
        except Exception:
            names.append(str(value))
    return sorted(set(names))


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all((RECEIPT_VALUE, TARGET_ROOT_VALUE, EVIDENCE_VALUE, EXPECTED_HARNESS_VALUE)):
    fail("All documented TattooShop migration environment variables are required")

receipt_path = os.path.realpath(RECEIPT_VALUE)
target_root = os.path.realpath(TARGET_ROOT_VALUE)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
evidence_path = os.path.realpath(EVIDENCE_VALUE)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_harness = os.path.realpath(EXPECTED_HARNESS_VALUE)

if harness_content.lower() != expected_harness.lower():
    fail("Commandlet is not running in the audited detached harness")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Target root is not NoShellForWinter")
if not os.path.isdir(target_content) or not is_under(target_content, target_root):
    fail("Target Content is absent or escapes target root")
if is_under(harness_content, target_content):
    fail("Harness Content must be detached from live target Content")
if not is_under(evidence_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Migration evidence escapes target Saved/Migration")

with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if receipt.get("status") != "ISOLATED_TATTOO_SHOP_BP_TSCHAR57_HARNESS_PASS":
    fail("Harness receipt is not the BP_TSChar TattooShop PASS receipt")
if int(receipt.get("package_count", -1)) != EXPECTED_COUNT:
    fail("Receipt package count differs")
if os.path.realpath(receipt.get("harness_content", "")).lower() != harness_content.lower():
    fail("Receipt names a different harness Content")

packages = []
expected_created = set()
for row in receipt.get("packages", []):
    package = str(row.get("package", ""))
    if not package.startswith("/Game/TattooShop/"):
        fail("Package escapes /Game/TattooShop: " + package)
    if package in packages:
        fail("Duplicate package in receipt: " + package)
    packages.append(package)
    for file_row in row.get("files", []):
        staged = os.path.realpath(str(file_row.get("staged", "")))
        if not is_under(staged, harness_content) or not os.path.isfile(staged):
            fail("Staged file is absent or escapes harness: " + package)
        if os.path.getsize(staged) != int(file_row.get("length", -1)):
            fail("Staged length differs: " + package)
        if sha256(staged) != str(file_row.get("sha256", "")).upper():
            fail("Staged hash differs: " + package)
        relative = str(file_row.get("relative_file", "")).replace("/", os.sep)
        target_file = os.path.realpath(os.path.join(target_content, relative))
        if not is_under(target_file, target_content):
            fail("Target file escapes Content: " + package)
        if os.path.exists(target_file):
            fail("Target collision exists for " + package + ": " + target_file)
        expected_created.add(os.path.relpath(target_file, target_content).replace(os.sep, "/").lower())

if len(packages) != EXPECTED_COUNT:
    fail("Package count differs: {} != {}".format(len(packages), EXPECTED_COUNT))
if "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar" not in packages:
    fail("BP_TSChar is absent from migration seed")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()
for package in packages:
    assets = list(registry.get_assets_by_package_name(package, include_only_on_disk_assets=True))
    if not assets:
        fail("Harness Asset Registry has no on-disk asset for " + package)

dirty_content_before = dirty_packages("get_dirty_content_packages")
dirty_maps_before = dirty_packages("get_dirty_map_packages")
if dirty_content_before or dirty_maps_before:
    fail(
        "Migration process began dirty: content={!r}, maps={!r}".format(
            dirty_content_before, dirty_maps_before
        )
    )

before = inventory(target_content)
migration_options = unreal.MigrationOptions()
migration_options.set_editor_property("prompt", False)
migration_options.set_editor_property("ignore_dependencies", True)
migration_options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
migration_options.set_editor_property("orphan_folder", "")

unreal.log("CODEX_TATTOOSHOP57_BEGIN: packages={}".format(len(packages)))
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    sorted(packages), target_content, migration_options
)

after = inventory(target_content)
created = sorted(set(after) - set(before))
removed = sorted(set(before) - set(after))
modified = sorted(
    relative
    for relative in set(before).intersection(after)
    if before[relative] != after[relative]
)
if set(created) != expected_created or removed or modified:
    fail(
        "Target delta is not exact; created={}, expected={}, removed={}, modified={}".format(
            created[:50], sorted(expected_created)[:50], removed[:20], modified[:20]
        )
    )

package_rows = []
for row in receipt.get("packages", []):
    package_output = []
    for file_row in row.get("files", []):
        relative = str(file_row["relative_file"]).replace("/", os.sep)
        target_file = os.path.realpath(os.path.join(target_content, relative))
        if not os.path.isfile(target_file):
            fail("Target package file is absent after migration: " + str(row["package"]))
        package_output.append(
            {
                "relative_file": str(file_row["relative_file"]),
                "file": target_file,
                "length": os.path.getsize(target_file),
                "sha256": sha256(target_file),
            }
        )
    package_rows.append(
        {
            "package": str(row["package"]),
            "class": row.get("class"),
            "files": package_output,
        }
    )

dirty_content_after = dirty_packages("get_dirty_content_packages")
dirty_maps_after = dirty_packages("get_dirty_map_packages")
if dirty_content_after or dirty_maps_after:
    fail(
        "Migration left dirty packages: content={!r}, maps={!r}".format(
            dirty_content_after, dirty_maps_after
        )
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_TATTOOSHOP_BP_TSCHAR57_MIGRATION_PASS",
    "engine_version": engine_version,
    "run_id": receipt.get("run_id"),
    "receipt": receipt_path,
    "receipt_sha256": sha256(receipt_path),
    "harness_project": os.path.realpath(unreal.Paths.get_project_file_path()),
    "harness_content": harness_content,
    "target_root": target_root,
    "destination": target_content,
    "package_count": len(package_rows),
    "packages": package_rows,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_existing_package_files": modified,
    "target_delta_exact": True,
    "ignore_dependencies": True,
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "raw_copy_to_target_content": False,
    "legacy_save_slots_migrated": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary = evidence_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, evidence_path)
unreal.log("CODEX_TATTOOSHOP57_MIGRATION_PASS: " + evidence_path)
