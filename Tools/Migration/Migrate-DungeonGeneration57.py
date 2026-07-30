"""Migrate exactly DungeonGeneration through UE 5.7 AssetTools."""

import datetime
import hashlib
import json
import os

import unreal


MAP_PACKAGE = "/Game/Procedural/Maps/DungeonGeneration"
EXPECTED_LENGTH = 58016
EXPECTED_SHA256 = "B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F"
EXPECTED_DEPENDENCIES = {
    "/Engine/EngineMaterials/WorldGridMaterial",
    "/Script/NavigationSystem",
    "/Script/PCG",
}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")

DESTINATION = os.environ.get("CODEX_DUNGEON_MAP_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get(
    "CODEX_DUNGEON_MAP_MIGRATION_EVIDENCE", ""
).strip()
RECEIPT_PATH = os.environ.get("CODEX_DUNGEON_MAP_RECEIPT", "").strip()
VALIDATION_PATH = os.environ.get(
    "CODEX_DUNGEON_MAP_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()
PRIOR_LOG_PATH = os.environ.get("CODEX_DUNGEON_MAP_PRIOR_LOG", "").strip()


def fail(message):
    unreal.log_error("CODEX_DUNGEON_GENERATION57_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    normalized_path = os.path.realpath(path).lower()
    normalized_root = os.path.realpath(root).rstrip(os.sep).lower() + os.sep
    return normalized_path.startswith(normalized_root)


def snapshot_package_files(content_root):
    rows = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = os.path.realpath(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/").lower()
            stat = os.stat(path)
            rows[relative] = {
                "length": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
            }
    return rows


def snapshot_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(name, rows[name]["length"], rows[name]["mtime_ns"])
        for name in sorted(rows)
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for property_name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        try:
            options.set_editor_property(property_name, True)
        except Exception:
            pass
    try:
        options.set_editor_property("include_searchable_names", False)
    except Exception:
        pass
    return options


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (
        DESTINATION,
        EVIDENCE_PATH,
        RECEIPT_PATH,
        VALIDATION_PATH,
        EXPECTED_TARGET_ROOT,
        EXPECTED_HARNESS_CONTENT,
    )
):
    fail("All DungeonGeneration migration environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
destination = os.path.realpath(DESTINATION)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if destination.lower() != target_content.lower():
    fail("Destination is not the canonical target Content root")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Destination is not the audited target project")
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("Commandlet is not running in the audited isolated harness")
if not is_under(
    harness_content,
    os.path.join(target_root, "Saved", "Migration", "Phase4", "DungeonGeneration"),
):
    fail("Harness Content escapes the batch Saved/Migration root")
if is_under(harness_content, destination):
    fail("Harness Content unexpectedly lives under live target Content")

with open(os.path.realpath(RECEIPT_PATH), "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
with open(os.path.realpath(VALIDATION_PATH), "r", encoding="utf-8-sig") as handle:
    validation = json.load(handle)
if (
    receipt.get("status") != "ISOLATED_DUNGEON_GENERATION57_HARNESS_PASS"
    or receipt.get("package_count") != 1
    or receipt.get("package") != MAP_PACKAGE
):
    fail("Harness receipt does not match the one-map contract")
if (
    validation.get("status") != "UE57_DUNGEON_GENERATION_READ_ONLY_LOAD_PASS"
    or validation.get("package") != MAP_PACKAGE
    or set(validation.get("dependencies", [])) != EXPECTED_DEPENDENCIES
    or validation.get("game_dependencies")
    or validation.get("map_load_requested") is not True
    or validation.get("map_save_requested") is not False
    or validation.get("packages_saved") != 0
    or validation.get("world_partitioned") is not False
    or validation.get("missing_required_actor_classes")
):
    fail("UE 5.7 read-only map validation is not PASS")

source_row = receipt.get("source_map", {})
staged_row = receipt.get("staged_map", {})
source_file = os.path.realpath(source_row.get("file", ""))
staged_file = os.path.realpath(staged_row.get("file", ""))
target_file = os.path.realpath(receipt.get("destination_map", ""))
expected_target_file = os.path.realpath(
    os.path.join(destination, "Procedural", "Maps", "DungeonGeneration.umap")
)
if target_file.lower() != expected_target_file.lower():
    fail("Receipt destination path differs from the canonical target map")
for label, path in (("source", source_file), ("staged", staged_file)):
    if not os.path.isfile(path):
        fail(label + " map is absent")
    if os.path.getsize(path) != EXPECTED_LENGTH or sha256(path) != EXPECTED_SHA256:
        fail(label + " map differs from the frozen baseline")
resumed_from_prior_log = False
migration_invoked_this_run = False
prior_log_path = ""
if os.path.exists(target_file):
    if not PRIOR_LOG_PATH:
        fail("Refusing to overwrite a pre-existing target map")
    prior_log_path = os.path.realpath(PRIOR_LOG_PATH)
    expected_log_root = os.path.join(target_root, "Saved", "Migration", "Logs")
    if not is_under(prior_log_path, expected_log_root) or not os.path.isfile(
        prior_log_path
    ):
        fail("Prior migration log is absent or escapes target Saved/Migration/Logs")
    with open(prior_log_path, "r", encoding="utf-8-sig", errors="replace") as handle:
        prior_log = handle.read()
    success_token = (
        "AssetTools: Package (/Game/Procedural/Maps/DungeonGeneration) was "
        "migrated successfully as (/Game/Procedural/Maps/DungeonGeneration)"
    )
    hash_stop = "UE 5.7 AssetTools output is not byte-identical to the staged map"
    if prior_log.count(success_token) != 1:
        fail("Prior log does not prove exactly one successful map migration")
    if prior_log.count("AssetTools: Package (/Game/") != 1:
        fail("Prior log contains an unexpected AssetTools package count")
    if hash_stop not in prior_log:
        fail("Prior log does not contain the audited post-migration hash stop")
    resumed_from_prior_log = True

validation_path = os.path.realpath(VALIDATION_PATH)
if not is_under(validation_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Validation evidence escapes target Saved/Migration")
if os.path.realpath(validation.get("staged_file", "")).lower() != staged_file.lower():
    fail("Validation evidence names a different staged file")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Procedural/Maps"], True)
registry.wait_for_completion()
dependencies = sorted(
    {
        str(value)
        for value in registry.get_dependencies(MAP_PACKAGE, dependency_options())
    }
)
if set(dependencies) != EXPECTED_DEPENDENCIES:
    fail("Dependency set changed between validation and migration")

expected_relative = "procedural/maps/dungeongeneration.umap"
if resumed_from_prior_log:
    after = snapshot_package_files(destination)
    if expected_relative not in after:
        fail("Resumed target inventory does not contain the migrated map")
    before = dict(after)
    del before[expected_relative]
    before_fingerprint = snapshot_fingerprint(before)
    after_fingerprint = snapshot_fingerprint(after)
    created = [expected_relative]
    removed = []
    modified_existing = []
    unreal.log(
        "CODEX_DUNGEON_GENERATION57_MIGRATION_RESUME: " + prior_log_path
    )
else:
    before = snapshot_package_files(destination)
    if expected_relative in before:
        fail("Target map appeared before AssetTools migration")
    before_fingerprint = snapshot_fingerprint(before)

    options = unreal.MigrationOptions()
    options.set_editor_property("prompt", False)
    options.set_editor_property("ignore_dependencies", True)
    options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
    options.set_editor_property("orphan_folder", "")

    unreal.log(
        "CODEX_DUNGEON_GENERATION57_MIGRATION_BEGIN: package=" + MAP_PACKAGE
    )
    unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
        [MAP_PACKAGE], destination, options
    )
    migration_invoked_this_run = True

    after = snapshot_package_files(destination)
    after_fingerprint = snapshot_fingerprint(after)
    created = sorted(set(after) - set(before))
    removed = sorted(set(before) - set(after))
    modified_existing = sorted(
        name
        for name in set(before).intersection(after)
        if before[name] != after[name]
    )
    if created != [expected_relative]:
        fail("AssetTools target delta is not exactly one map: " + repr(created))
    if removed:
        fail("AssetTools removed target package files: " + repr(removed))
    if modified_existing:
        fail(
            "AssetTools modified pre-existing target package files: "
            + repr(modified_existing)
        )
if not os.path.isfile(target_file):
    fail("AssetTools did not create the target map")
target_length = os.path.getsize(target_file)
target_sha256 = sha256(target_file)
bytes_match_source = (
    target_length == EXPECTED_LENGTH and target_sha256 == EXPECTED_SHA256
)

for forbidden in (
    os.path.splitext(target_file)[0] + ".uexp",
    os.path.splitext(target_file)[0] + ".ubulk",
    os.path.splitext(target_file)[0] + ".uptnl",
    os.path.join(destination, "Procedural", "Maps", "DungeonGeneration_BuiltData.uasset"),
    os.path.join(
        destination,
        "__ExternalActors__",
        "Procedural",
        "Maps",
        "DungeonGeneration",
    ),
    os.path.join(
        destination,
        "__ExternalObjects__",
        "Procedural",
        "Maps",
        "DungeonGeneration",
    ),
):
    if os.path.exists(forbidden):
        fail("Unexpected sidecar or external package created: " + forbidden)
if os.path.getsize(source_file) != EXPECTED_LENGTH or sha256(source_file) != EXPECTED_SHA256:
    fail("Source map bytes changed during migration")
if os.path.getsize(staged_file) != EXPECTED_LENGTH or sha256(staged_file) != EXPECTED_SHA256:
    fail("Staged map bytes changed during migration")

evidence_path = os.path.realpath(EVIDENCE_PATH)
if not is_under(evidence_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Migration evidence escapes target Saved/Migration")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_DUNGEON_GENERATION_MIGRATION_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "harness_content": harness_content,
    "destination": destination,
    "package_count": 1,
    "package": {
        "package": MAP_PACKAGE,
        "file": target_file,
        "length": target_length,
        "sha256": target_sha256,
        "source_length": EXPECTED_LENGTH,
        "source_sha256": EXPECTED_SHA256,
        "bytes_match_source": bytes_match_source,
    },
    "dependencies": dependencies,
    "game_dependencies": [],
    "ignore_dependencies": True,
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "target_inventory_algorithm": "lowercase relative package path|length|mtime_ns",
    "target_package_file_count_before": len(before),
    "target_package_file_count_after": len(after),
    "target_inventory_fingerprint_before": before_fingerprint,
    "target_inventory_fingerprint_after": after_fingerprint,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_existing_package_files": modified_existing,
    "target_delta_exact": True,
    "migration_invoked_this_run": migration_invoked_this_run,
    "resumed_from_prior_log": resumed_from_prior_log,
    "prior_migration_log": prior_log_path,
    "source_tree_mounted": False,
    "raw_target_asset_copy_requested": False,
    "source_map_unchanged": True,
    "staged_map_unchanged": True,
    "post_migration_source_protected_gate": "PENDING_EXTERNAL_GATE",
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DUNGEON_GENERATION57_MIGRATION_PASS: " + evidence_path)
