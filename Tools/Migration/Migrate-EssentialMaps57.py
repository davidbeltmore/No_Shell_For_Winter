"""Migrate three essential maps and exact OFPA package families via UE 5.7.

Required environment:
  CODEX_ESSENTIAL_MAPS_RECEIPT
  CODEX_ESSENTIAL_MAPS_VALIDATION_EVIDENCE
  CODEX_ESSENTIAL_MAPS_TARGET_ROOT
  CODEX_ESSENTIAL_MAPS_MIGRATION_EVIDENCE
  CODEX_ESSENTIAL_MAPS_EXPECTED_HARNESS_CONTENT
"""

import datetime
import hashlib
import json
import os

import unreal


RECEIPT_VALUE = os.environ.get("CODEX_ESSENTIAL_MAPS_RECEIPT", "").strip()
VALIDATION_VALUE = os.environ.get(
    "CODEX_ESSENTIAL_MAPS_VALIDATION_EVIDENCE", ""
).strip()
TARGET_ROOT_VALUE = os.environ.get("CODEX_ESSENTIAL_MAPS_TARGET_ROOT", "").strip()
EVIDENCE_VALUE = os.environ.get(
    "CODEX_ESSENTIAL_MAPS_MIGRATION_EVIDENCE", ""
).strip()
EXPECTED_HARNESS_VALUE = os.environ.get(
    "CODEX_ESSENTIAL_MAPS_EXPECTED_HARNESS_CONTENT", ""
).strip()
EXPECTED_MAPS = {
    "/Game/_Game/Hub/HUB",
    "/Game/_Game/Locations/StorySelection",
    "/Game/_Game/Locations/PCGLevel",
}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")
PROTECTED_SEED_PREFIXES = (
    "/Game/FullSample",
    "/Game/DazToUnreal",
    "/Game/ACFUltimate",
)


def fail(message):
    unreal.log_error("CODEX_ESSENTIAL_MAPS57_MIGRATION_FAIL: " + message)
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
    try:
        values = getattr(unreal.EditorLoadingAndSavingUtils, function_name)()
    except Exception as exc:
        fail("Dirty-package probe failed: {}: {!r}".format(function_name, exc))
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
if not all(
    (
        RECEIPT_VALUE,
        VALIDATION_VALUE,
        TARGET_ROOT_VALUE,
        EVIDENCE_VALUE,
        EXPECTED_HARNESS_VALUE,
    )
):
    fail("All documented EssentialMaps migration variables are required")

receipt_path = os.path.realpath(RECEIPT_VALUE)
validation_path = os.path.realpath(VALIDATION_VALUE)
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
    fail("Canonical target Content is absent")
if not is_under(harness_content, os.path.join(target_root, "Saved", "Migration")):
    fail("Harness escapes target Saved/Migration staging")
if is_under(harness_content, target_content):
    fail("Harness unexpectedly lives inside live target Content")
if not is_under(evidence_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Migration evidence escapes target Saved/Migration")
for required in (receipt_path, validation_path):
    if not os.path.isfile(required):
        fail("Required evidence is absent: " + required)
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
with open(validation_path, "r", encoding="utf-8-sig") as handle:
    validation = json.load(handle)
if receipt.get("status") != "ISOLATED_ESSENTIAL_MAPS57_HARNESS_PASS":
    fail("Harness receipt is not PASS")
if validation.get("status") != "UE57_ESSENTIAL_MAPS_READ_ONLY_LOAD_PASS":
    fail("UE 5.7 read-only map validation is not PASS")
if validation.get("receipt_sha256") != sha256(receipt_path):
    fail("Validation evidence is chained to a different receipt")
if validation.get("run_id") != receipt.get("run_id"):
    fail("Validation and receipt RunId differ")
if validation.get("map_count") != 3 or validation.get("map_save_requested") is not False:
    fail("Validation evidence does not prove three read-only map loads")

map_packages = {row.get("package") for row in receipt.get("maps", [])}
if map_packages != EXPECTED_MAPS:
    fail("Receipt essential map set differs")
package_rows = list(receipt.get("migration_packages", []))
if len(package_rows) != int(receipt.get("migration_package_count", -1)):
    fail("Receipt migration package count differs")
packages = [str(row.get("package", "")) for row in package_rows]
if len(packages) != len(set(packages)) or not EXPECTED_MAPS.issubset(packages):
    fail("Migration seed set is duplicate or incomplete")
for package in packages:
    if not package.startswith("/Game/"):
        fail("Migration seed is not /Game: " + package)
    if package.startswith(PROTECTED_SEED_PREFIXES):
        fail("Protected package entered migration seed set: " + package)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()
expected_created = set()
collision_rows = []
created_contract_rows = []
for package_row in package_rows:
    package = str(package_row["package"])
    assets = list(
        registry.get_assets_by_package_name(package, include_only_on_disk_assets=True)
    )
    if not assets:
        fail("Harness Asset Registry has no on-disk asset for " + package)
    files = list(package_row.get("files", []))
    primary_extension = str(package_row.get("primary_extension", "")).lower()
    primaries = [
        row
        for row in files
        if os.path.splitext(str(row.get("relative_file", "")))[1].lower()
        == primary_extension
    ]
    if len(primaries) != 1:
        fail("Migration package has no unique primary: " + package)
    target_files = []
    for row in files:
        staged = os.path.realpath(str(row.get("staged", "")))
        if not is_under(staged, harness_content) or not os.path.isfile(staged):
            fail("Staged package file is absent or escapes harness: " + package)
        if os.path.getsize(staged) != int(row.get("length", -1)) or sha256(
            staged
        ) != str(row.get("sha256", "")).upper():
            fail("Staged package bytes differ: " + package)
        relative = str(row.get("relative_file", "")).replace("/", os.sep)
        target_file = os.path.realpath(os.path.join(target_content, relative))
        if not is_under(target_file, target_content):
            fail("Target package file escapes Content: " + package)
        target_files.append((row, target_file))
    primary_relative = str(primaries[0]["relative_file"]).replace("/", os.sep)
    target_primary = os.path.realpath(os.path.join(target_content, primary_relative))
    primary_exists = os.path.isfile(target_primary)
    existing_expected_files = [path for _row, path in target_files if os.path.exists(path)]
    if primary_exists:
        if len(existing_expected_files) != len(target_files):
            fail("Partial target collision exists for " + package)
        collision_rows.append(
            {
                "package": package,
                "disposition": "SKIP",
                "files_before": [
                    {
                        "file": path,
                        "length": os.path.getsize(path),
                        "sha256": sha256(path),
                    }
                    for _row, path in target_files
                ],
            }
        )
    else:
        if existing_expected_files:
            fail("Orphan target sidecar exists for " + package)
        relative_files = []
        for _row, path in target_files:
            relative = os.path.relpath(path, target_content).replace(os.sep, "/").lower()
            expected_created.add(relative)
            relative_files.append(relative)
        created_contract_rows.append(
            {"package": package, "expected_files": sorted(relative_files)}
        )

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
migration_options.set_editor_property(
    "asset_conflict", unreal.AssetMigrationConflict.SKIP
)
migration_options.set_editor_property("orphan_folder", "")
unreal.log(
    "CODEX_ESSENTIAL_MAPS57_BEGIN: seeds={} collisions={}".format(
        len(packages), len(collision_rows)
    )
)
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
        "Target delta is not exact; created={!r}, expected={!r}, removed={!r}, modified={!r}".format(
            created[:50], sorted(expected_created)[:50], removed[:50], modified[:50]
        )
    )

output_rows = []
for package_row in package_rows:
    package = str(package_row["package"])
    package_output = []
    for row in package_row.get("files", []):
        relative = str(row["relative_file"]).replace("/", os.sep)
        target_file = os.path.realpath(os.path.join(target_content, relative))
        if not os.path.isfile(target_file):
            fail("Target package file is absent after migration: " + package)
        target_length = os.path.getsize(target_file)
        target_hash = sha256(target_file)
        collision = any(item["package"] == package for item in collision_rows)
        if not collision and (
            target_length != int(row["length"])
            or target_hash != str(row["sha256"]).upper()
        ):
            fail("Created target bytes differ from staged source: " + package)
        package_output.append(
            {
                "file": target_file,
                "relative_file": str(row["relative_file"]),
                "length": target_length,
                "sha256": target_hash,
            }
        )
    output_rows.append(
        {
            "package": package,
            "role": package_row.get("role"),
            "owner_map": package_row.get("owner_map"),
            "disposition": "SKIP" if any(
                item["package"] == package for item in collision_rows
            ) else "CREATED",
            "files": package_output,
        }
    )

for collision in collision_rows:
    after_rows = []
    for before_row in collision["files_before"]:
        path = before_row["file"]
        after_row = {
            "file": path,
            "length": os.path.getsize(path),
            "sha256": sha256(path),
        }
        if (
            after_row["length"] != before_row["length"]
            or after_row["sha256"] != before_row["sha256"]
        ):
            fail("SKIP collision changed on disk: " + collision["package"])
        after_rows.append(after_row)
    collision["files_after"] = after_rows

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
    "status": "ASSETTOOLS_EXACT_ESSENTIAL_MAPS57_MIGRATION_PASS",
    "engine_version": engine_version,
    "run_id": receipt.get("run_id"),
    "receipt": receipt_path,
    "receipt_sha256": sha256(receipt_path),
    "validation": validation_path,
    "validation_sha256": sha256(validation_path),
    "harness_project": os.path.realpath(unreal.Paths.get_project_file_path()),
    "harness_content": harness_content,
    "target_root": target_root,
    "destination": target_content,
    "map_count": 3,
    "migration_package_count": len(package_rows),
    "packages": output_rows,
    "created_contract": created_contract_rows,
    "collision_packages_skipped": collision_rows,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_existing_package_files": modified,
    "target_delta_exact": True,
    "ignore_dependencies": True,
    "asset_conflict": "SKIP",
    "protected_seed_prefixes_excluded": list(PROTECTED_SEED_PREFIXES),
    "direct_dependencies_migrated_implicitly": False,
    "ofpa_external_packages_explicitly_seeded": True,
    "raw_copy_to_target_content": False,
    "dirty_content_packages_before": dirty_content_before,
    "dirty_content_packages_after": dirty_content_after,
    "dirty_map_packages_before": dirty_maps_before,
    "dirty_map_packages_after": dirty_maps_after,
    "post_migration_protected_invariant_gate": "PENDING_EXTERNAL_GATE",
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary = evidence_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, evidence_path)
unreal.log("CODEX_ESSENTIAL_MAPS57_MIGRATION_PASS: " + evidence_path)
