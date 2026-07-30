"""Migrate exactly twenty procedural contracts with UE 5.7 AssetTools."""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_PACKAGE_COUNT = 20
EXPECTED_SOURCE_BYTES = 223283
EXPECTED_SOURCE_FINGERPRINT = (
    "30A8ACBD998EBD41242A3BD850C8CBB3E7E6A19D63D72E632D0BB897917FA006"
)
EXPECTED_CLASS_COUNTS = {
    "Blueprint": 6,
    "UserDefinedStruct": 12,
    "UserDefinedEnum": 2,
}
EXPECTED_PACKAGES = (
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
)
EXPECTED_CLASS = {
    package: (
        "Blueprint"
        if package.endswith("/BP_StartPoint")
        or package.rsplit("/", 1)[-1].startswith("PDA_")
        else "UserDefinedEnum"
        if "/Data/Enumerator/" in package
        else "UserDefinedStruct"
    )
    for package in EXPECTED_PACKAGES
}

DESTINATION = os.environ.get(
    "CODEX_PROCEDURAL_CONTRACTS_DESTINATION", ""
).strip()
EVIDENCE_PATH = os.environ.get(
    "CODEX_PROCEDURAL_CONTRACTS_MIGRATION_EVIDENCE", ""
).strip()
RECEIPT_PATH = os.environ.get(
    "CODEX_PROCEDURAL_CONTRACTS_RECEIPT", ""
).strip()
VALIDATION_PATH = os.environ.get(
    "CODEX_PROCEDURAL_CONTRACTS_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()
RESUME_LOG_PATH = os.environ.get(
    "CODEX_PROCEDURAL_CONTRACTS_PRIOR_LOG", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_PROCEDURAL_CONTRACTS57_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def fingerprint(rows):
    values = [rows[package]["sha256"].upper() for package in sorted(rows)]
    return hashlib.sha256("\n".join(values).encode("utf-8")).hexdigest().upper()


def package_file(content_root, package_name):
    relative = package_name[len("/Game/") :].replace("/", os.sep)
    return os.path.realpath(os.path.join(content_root, relative + ".uasset"))


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


def is_under(path, root):
    normalized_path = os.path.realpath(path).lower()
    normalized_root = os.path.realpath(root).rstrip(os.sep).lower() + os.sep
    return normalized_path.startswith(normalized_root)


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
    fail("All procedural-contract migration environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
destination = os.path.realpath(DESTINATION)
expected_destination = os.path.realpath(os.path.join(target_root, "Content"))
if destination.lower() != expected_destination.lower():
    fail("Destination is not the audited target Content root")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Destination is not the audited target project")

harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("Commandlet is not running inside the audited isolated harness")
if not is_under(harness_content, os.path.join(target_root, "Saved", "Migration", "Phase4")):
    fail("Harness Content escapes target Saved/Migration/Phase4")
if is_under(harness_content, destination):
    fail("Harness Content unexpectedly lives below target Content")

with open(os.path.realpath(RECEIPT_PATH), "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
with open(os.path.realpath(VALIDATION_PATH), "r", encoding="utf-8-sig") as handle:
    validation = json.load(handle)
if (
    receipt.get("status") != "ISOLATED_PROCEDURAL_CONTRACTS_HARNESS_PASS"
    or receipt.get("expected_package_count") != EXPECTED_PACKAGE_COUNT
    or receipt.get("expected_source_bytes") != EXPECTED_SOURCE_BYTES
    or receipt.get("expected_source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
):
    fail("Harness receipt does not match the frozen baseline")
if (
    validation.get("status")
    != "UE57_PROCEDURAL_CONTRACTS_READ_ONLY_LOAD_COMPILE_PASS"
    or validation.get("package_count") != EXPECTED_PACKAGE_COUNT
    or validation.get("compiled_blueprint_count") != 6
    or validation.get("class_counts") != EXPECTED_CLASS_COUNTS
    or validation.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or validation.get("unexpected_game_dependencies")
    or validation.get("asset_save_requested") is not False
):
    fail("UE 5.7 read-only validation gate is not PASS")

receipt_rows = {
    row["package"]: row for row in receipt.get("staged_assets", [])
}
if set(receipt_rows) != set(EXPECTED_PACKAGES) or len(receipt_rows) != 20:
    fail("Harness receipt package set differs from the exact allowlist")
for package, row in receipt_rows.items():
    path = os.path.realpath(row["staged"])
    if not is_under(path, harness_content) or not os.path.isfile(path):
        fail("Staged package is absent or escapes harness: " + package)
    if os.path.getsize(path) != row["length"] or sha256(path) != row["sha256"]:
        fail("Staged package differs from its receipt: " + package)
if fingerprint(receipt_rows) != EXPECTED_SOURCE_FINGERPRINT:
    fail("Harness receipt fingerprint differs from the frozen baseline")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    [
        "/Game/Calysto/Dungeon/Blueprint/Utility",
        "/Game/Calysto/Dungeon/Data",
        "/Game/Calysto/Shared/Data",
    ],
    True,
)
registry.wait_for_completion()

class_counts = {}
for package in EXPECTED_PACKAGES:
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    primary = [item for item in assets if class_name(item) == EXPECTED_CLASS[package]]
    if len(primary) != 1:
        fail("Expected one registered primary asset for " + package)
    value = class_name(primary[0])
    class_counts[value] = class_counts.get(value, 0) + 1
if class_counts != EXPECTED_CLASS_COUNTS:
    fail("Unexpected harness class counts: " + repr(class_counts))

dependency_options = unreal.AssetRegistryDependencyOptions()
for property_name in (
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
):
    try:
        dependency_options.set_editor_property(property_name, True)
    except Exception:
        pass
try:
    dependency_options.set_editor_property("include_searchable_names", False)
except Exception:
    pass

dependencies = {}
unexpected_game_dependencies = set()
for package in EXPECTED_PACKAGES:
    direct = sorted(
        {
            str(value)
            for value in registry.get_dependencies(package, dependency_options)
        }
    )
    dependencies[package] = direct
    unexpected_game_dependencies.update(
        value
        for value in direct
        if value.startswith("/Game/") and value not in EXPECTED_PACKAGES
    )
if unexpected_game_dependencies:
    fail(
        "Unexpected /Game dependencies: "
        + repr(sorted(unexpected_game_dependencies))
    )

expected_files = {
    package_file(destination, package).lower(): package
    for package in EXPECTED_PACKAGES
}
existing = {path for path in expected_files if os.path.exists(path)}
resumed_from_prior_log = False
migration_invoked_this_run = False
prior_log_path = ""

if existing:
    if existing != set(expected_files):
        fail("Refusing partial pre-existing procedural-contract batch")
    if not RESUME_LOG_PATH:
        fail("Refusing to overwrite destination packages")
    prior_log_path = os.path.realpath(RESUME_LOG_PATH)
    expected_log_root = os.path.join(target_root, "Saved", "Migration", "Logs")
    if not is_under(prior_log_path, expected_log_root) or not os.path.isfile(
        prior_log_path
    ):
        fail("Prior migration log is absent or escapes target Saved/Migration/Logs")
    with open(prior_log_path, "r", encoding="utf-8-sig", errors="replace") as handle:
        prior_log = handle.read()
    success_tokens = [
        "AssetTools: Package ({}) was migrated successfully as ({})".format(
            package, package
        )
        for package in EXPECTED_PACKAGES
    ]
    if any(prior_log.count(token) != 1 for token in success_tokens):
        fail("Prior log does not prove exactly one successful migration per package")
    if prior_log.count("AssetTools: Package (/Game/") != EXPECTED_PACKAGE_COUNT:
        fail("Prior log contains an unexpected AssetTools package count")
    expected_hash_stop = (
        "UE 5.7 migrated bytes differ from staged source: "
        "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint"
    )
    if expected_hash_stop not in prior_log:
        fail("Prior log does not contain the audited post-migration hash-gate stop")
    created = set(expected_files)
    resumed_from_prior_log = True
    unreal.log(
        "CODEX_PROCEDURAL_CONTRACTS57_MIGRATION_RESUME: " + prior_log_path
    )
else:
    before = list_packages(destination)
    options = unreal.MigrationOptions()
    options.set_editor_property("prompt", False)
    options.set_editor_property("ignore_dependencies", True)
    options.set_editor_property(
        "asset_conflict", unreal.AssetMigrationConflict.SKIP
    )
    options.set_editor_property("orphan_folder", "")

    unreal.log("CODEX_PROCEDURAL_CONTRACTS57_MIGRATION_BEGIN: packages=20")
    unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
        list(sorted(EXPECTED_PACKAGES)), destination, options
    )
    migration_invoked_this_run = True

    created = list_packages(destination) - before
    if created != set(expected_files):
        fail(
            "Unexpected target delta; created={!r}, expected={!r}".format(
                sorted(created), sorted(expected_files)
            )
        )

target_rows = {}
package_rows = []
for path in sorted(created):
    package = expected_files[path]
    length = os.path.getsize(path)
    current_hash = sha256(path)
    source_row = receipt_rows[package]
    target_rows[package] = {
        "length": length,
        "sha256": current_hash,
    }
    package_rows.append(
        {
            "package": package,
            "class": EXPECTED_CLASS[package],
            "file": path,
            "length": length,
            "sha256": current_hash,
            "source_length": source_row["length"],
            "source_sha256": source_row["sha256"],
            "bytes_match_source": (
                length == source_row["length"]
                and current_hash == source_row["sha256"]
            ),
        }
    )

evidence_path = os.path.realpath(EVIDENCE_PATH)
if not is_under(evidence_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Migration evidence escapes target Saved/Migration")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_PROCEDURAL_CONTRACTS_MIGRATION_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "harness_content": harness_content,
    "destination": destination,
    "package_count": len(package_rows),
    "created_package_count": len(package_rows),
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "target_bytes_after_assettools": sum(
        row["length"] for row in target_rows.values()
    ),
    "target_fingerprint_after_assettools": fingerprint(target_rows),
    "byte_identical_package_count": sum(
        1 for row in package_rows if row["bytes_match_source"]
    ),
    "class_counts": class_counts,
    "packages": package_rows,
    "dependencies": dependencies,
    "unexpected_game_dependencies": [],
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "ignore_dependencies": True,
    "target_delta_exact": True,
    "migration_invoked_this_run": migration_invoked_this_run,
    "resumed_from_prior_log": resumed_from_prior_log,
    "prior_migration_log": prior_log_path,
    "source_tree_mounted": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_PROCEDURAL_CONTRACTS57_MIGRATION_PASS: " + evidence_path)
