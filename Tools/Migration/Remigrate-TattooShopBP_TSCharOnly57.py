"""Overwrite exactly one target package from the detached UE 5.7 harness.

This script is intentionally narrow. It migrates only:
  /Game/TattooShop/Blueprints/TSCharacter/BP_TSChar

Required environment variables:
  CODEX_BP_TSCHAR57_RECEIPT
  CODEX_BP_TSCHAR57_TARGET_ROOT
  CODEX_BP_TSCHAR57_EXPECTED_HARNESS_CONTENT
  CODEX_BP_TSCHAR57_EVIDENCE
  CODEX_BP_TSCHAR57_BACKUP_DIR

Run with the UE 5.7 UnrealEditor-Cmd PythonScript commandlet against the
detached harness project. Never run this script against LustAsDeadlySin.
"""

import datetime
import hashlib
import json
import os
import shutil

import unreal


PACKAGE = "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar"
RELATIVE_PACKAGE_STEM = "TattooShop/Blueprints/TSCharacter/BP_TSChar"
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")
PROTECTED_RELATIVES = (
    "TattooShop/Blueprints/BP_TSGameMode.uasset",
)


def env(name):
    value = os.environ.get(name, "").strip()
    if not value:
        raise RuntimeError("Missing required environment variable: " + name)
    return value


def real(path):
    return os.path.realpath(os.path.abspath(path))


def is_under(path, root):
    try:
        return os.path.commonpath([real(path), real(root)]).lower() == real(root).lower()
    except ValueError:
        return False


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def file_record(path):
    stat = os.stat(path)
    return {
        "file": real(path),
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(path),
    }


def content_inventory(content_root):
    """Fast whole-Content mutation gate using size and nanosecond timestamp."""
    result = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = real(os.path.join(root, name))
            stat = os.stat(path)
            relative = os.path.relpath(path, content_root).replace(os.sep, "/")
            result[relative.lower()] = {
                "relative_file": relative,
                "length": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
            }
    return result


def package_files(content_root):
    result = []
    for extension in PACKAGE_EXTENSIONS:
        path = real(os.path.join(content_root, RELATIVE_PACKAGE_STEM + extension))
        if os.path.isfile(path):
            result.append(path)
    return sorted(result)


def dirty_packages(function_name):
    values = getattr(unreal.EditorLoadingAndSavingUtils, function_name)()
    names = []
    for value in values:
        try:
            names.append(value.get_name())
        except Exception:
            names.append(str(value))
    return sorted(set(names))


def write_json_atomic(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
    os.replace(temporary, path)


receipt_path = real(env("CODEX_BP_TSCHAR57_RECEIPT"))
target_root = real(env("CODEX_BP_TSCHAR57_TARGET_ROOT"))
expected_harness_content = real(env("CODEX_BP_TSCHAR57_EXPECTED_HARNESS_CONTENT"))
evidence_path = real(env("CODEX_BP_TSCHAR57_EVIDENCE"))
backup_dir = real(env("CODEX_BP_TSCHAR57_BACKUP_DIR"))
target_content = real(os.path.join(target_root, "Content"))
harness_content = real(unreal.Paths.project_content_dir())
harness_project = real(unreal.Paths.get_project_file_path())
target_project = real(os.path.join(target_root, "NoShellForWinter.uproject"))
target_saved_migration = real(os.path.join(target_root, "Saved", "Migration"))
engine_version = unreal.SystemLibrary.get_engine_version()


def fail(message, extra=None):
    payload = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "ASSETTOOLS_BP_TSCHAR_ONLY57_MIGRATION_FAIL",
        "message": message,
        "engine_version": engine_version,
        "package_seed": [PACKAGE],
        "ignore_dependencies": True,
        "target_root": target_root,
        "harness_project": harness_project,
    }
    if extra:
        payload.update(extra)
    if is_under(evidence_path, target_saved_migration):
        write_json_atomic(evidence_path, payload)
    unreal.log_error("CODEX_BP_TSCHAR_ONLY57_FAIL: " + message)
    raise RuntimeError(message)


if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if harness_content.lower() != expected_harness_content.lower():
    fail("Commandlet is not running in the audited detached harness")
if "lustasdeadlysin" in harness_project.lower():
    fail("Refusing to run against the read-only source project")
if not os.path.isfile(target_project):
    fail("Target project identity file is absent: " + target_project)
if not os.path.isdir(target_content) or not is_under(target_content, target_root):
    fail("Target Content is absent or escapes target root")
if is_under(harness_content, target_content) or is_under(target_content, harness_content):
    fail("Harness and live target Content are not detached")
if not is_under(evidence_path, target_saved_migration):
    fail("Evidence path escapes target Saved/Migration")
if not is_under(backup_dir, target_saved_migration):
    fail("Backup path escapes target Saved/Migration")
if os.path.exists(backup_dir) and os.listdir(backup_dir):
    fail("Backup directory already exists and is not empty: " + backup_dir)
if not os.path.isfile(receipt_path):
    fail("Harness receipt is absent: " + receipt_path)

with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if receipt.get("status") != "ISOLATED_TATTOO_SHOP_BP_TSCHAR57_HARNESS_PASS":
    fail("Harness receipt status is not the audited PASS status")
if real(receipt.get("harness_content", "")).lower() != harness_content.lower():
    fail("Harness receipt names a different Content directory")

matches = [row for row in receipt.get("packages", []) if row.get("package") == PACKAGE]
if len(matches) != 1:
    fail("Receipt must contain exactly one BP_TSChar package row")
row = matches[0]
source_files = []
for file_row in row.get("files", []):
    staged = real(str(file_row.get("staged", "")))
    if not is_under(staged, harness_content) or not os.path.isfile(staged):
        fail("Staged package file is absent or escapes harness Content")
    relative = os.path.relpath(staged, harness_content).replace(os.sep, "/")
    if not relative.lower().startswith(RELATIVE_PACKAGE_STEM.lower() + "."):
        fail("Receipt row contains a file outside BP_TSChar: " + relative)
    if os.path.getsize(staged) != int(file_row.get("length", -1)):
        fail("Staged BP_TSChar length differs from audited receipt")
    if sha256(staged) != str(file_row.get("sha256", "")).upper():
        fail("Staged BP_TSChar hash differs from audited receipt")
    source_files.append(staged)
if not source_files:
    fail("Receipt BP_TSChar row has no package files")

target_files_before = package_files(target_content)
if not target_files_before:
    fail("Target BP_TSChar package does not exist; this run is overwrite-only")
source_relative_set = {
    os.path.relpath(path, harness_content).replace(os.sep, "/").lower()
    for path in source_files
}
target_relative_set = {
    os.path.relpath(path, target_content).replace(os.sep, "/").lower()
    for path in target_files_before
}
allowed_delta = source_relative_set.union(target_relative_set)
if any(not value.startswith(RELATIVE_PACKAGE_STEM.lower() + ".") for value in allowed_delta):
    fail("Allowed target delta escaped BP_TSChar package files")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()
assets = list(registry.get_assets_by_package_name(PACKAGE, include_only_on_disk_assets=True))
if not assets:
    fail("Detached harness Asset Registry cannot resolve BP_TSChar")

dirty_content_before = dirty_packages("get_dirty_content_packages")
dirty_maps_before = dirty_packages("get_dirty_map_packages")
if dirty_content_before or dirty_maps_before:
    fail(
        "Harness process began dirty",
        {"dirty_content_before": dirty_content_before, "dirty_maps_before": dirty_maps_before},
    )

protected_before = {}
for relative in PROTECTED_RELATIVES:
    path = real(os.path.join(target_content, relative.replace("/", os.sep)))
    if not os.path.isfile(path):
        fail("Protected target asset is absent: " + relative)
    protected_before[relative] = file_record(path)

target_records_before = [file_record(path) for path in target_files_before]
before = content_inventory(target_content)

os.makedirs(backup_dir, exist_ok=False)
backup_records = []
for source in target_files_before:
    destination = real(os.path.join(backup_dir, os.path.basename(source)))
    if not is_under(destination, backup_dir):
        fail("Backup destination escaped backup directory")
    shutil.copy2(source, destination)
    if sha256(destination) != sha256(source):
        fail("BP_TSChar backup hash differs from live target")
    backup_records.append(file_record(destination))

# UE 5.7 IAssetTools.h declares Skip, Overwrite and Cancel. Python exposes the
# required overwrite member as AssetMigrationConflict.OVERWRITE.
if not hasattr(unreal.AssetMigrationConflict, "OVERWRITE"):
    fail("UE 5.7 Python enum has no AssetMigrationConflict.OVERWRITE")

migration_options = unreal.MigrationOptions()
migration_options.set_editor_property("prompt", False)
migration_options.set_editor_property("ignore_dependencies", True)
migration_options.set_editor_property(
    "asset_conflict", unreal.AssetMigrationConflict.OVERWRITE
)
migration_options.set_editor_property("orphan_folder", "")

# The UE 5.7 new migration backend loads and re-saves the Blueprint.  This
# detached harness intentionally does not carry Marketplace/ACFU binaries, so
# loading BP_TSChar there strips its inherited Player components.  The legacy
# AssetTools backend is still the official editor migration path, but copies
# the audited package bytes without loading or mutating the Blueprint.
unreal.SystemLibrary.execute_console_command(
    None, "AssetTools.UseNewPackageMigration 0"
)

unreal.log("CODEX_BP_TSCHAR_ONLY57_BEGIN: package_seed=[{}]".format(PACKAGE))
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    [PACKAGE], target_content, migration_options
)

after = content_inventory(target_content)
created = sorted(set(after) - set(before))
removed = sorted(set(before) - set(after))
modified = sorted(
    relative
    for relative in set(before).intersection(after)
    if before[relative] != after[relative]
)
delta = set(created).union(removed).union(modified)
outside_delta = sorted(delta - allowed_delta)
if outside_delta:
    fail(
        "AssetTools changed package files outside BP_TSChar",
        {
            "created_package_files": created,
            "removed_package_files": removed,
            "modified_package_files": modified,
            "outside_allowed_delta": outside_delta,
            "backup_records": backup_records,
        },
    )
if not delta:
    fail("AssetTools reported success but BP_TSChar target did not change")

target_files_after = package_files(target_content)
target_records_after = [file_record(path) for path in target_files_after]
target_after_by_relative = {
    os.path.relpath(record["file"], target_content).replace(os.sep, "/").lower(): record
    for record in target_records_after
}
source_by_relative = {
    os.path.relpath(path, harness_content).replace(os.sep, "/").lower(): file_record(path)
    for path in source_files
}
for relative, source_record in source_by_relative.items():
    target_record = target_after_by_relative.get(relative)
    if target_record is None:
        fail("Migrated BP_TSChar package file is absent: " + relative)
    if target_record["sha256"] != source_record["sha256"]:
        fail("Migrated BP_TSChar hash differs from detached harness: " + relative)

protected_after = {}
for relative, before_record in protected_before.items():
    path = before_record["file"]
    after_record = file_record(path)
    protected_after[relative] = after_record
    if after_record["sha256"] != before_record["sha256"]:
        fail("Protected target asset changed: " + relative)

dirty_content_after = dirty_packages("get_dirty_content_packages")
dirty_maps_after = dirty_packages("get_dirty_map_packages")
if dirty_content_after or dirty_maps_after:
    fail(
        "Migration left dirty packages",
        {"dirty_content_after": dirty_content_after, "dirty_maps_after": dirty_maps_after},
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_BP_TSCHAR_ONLY57_OVERWRITE_PASS",
    "engine_version": engine_version,
    "asset_owner": "NoShellForWinter project content",
    "package_seed": [PACKAGE],
    "package_seed_count": 1,
    "ignore_dependencies": True,
    "asset_conflict": "OVERWRITE",
    "migration_backend": "ASSETTOOLS_LEGACY_EXACT_PACKAGE_COPY",
    "prompt": False,
    "harness_project": harness_project,
    "harness_content": harness_content,
    "receipt": receipt_path,
    "receipt_sha256": sha256(receipt_path),
    "target_root": target_root,
    "target_content": target_content,
    "target_records_before": target_records_before,
    "target_records_after": target_records_after,
    "backup_dir": backup_dir,
    "backup_records": backup_records,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_package_files": modified,
    "outside_allowed_delta": outside_delta,
    "protected_before": protected_before,
    "protected_after": protected_after,
    "bp_tsgamemode_unchanged": True,
    "only_bp_tschar_changed": True,
    "raw_copy_to_target_content": False,
}
write_json_atomic(evidence_path, payload)
unreal.log("CODEX_BP_TSCHAR_ONLY57_OVERWRITE_PASS: " + evidence_path)
