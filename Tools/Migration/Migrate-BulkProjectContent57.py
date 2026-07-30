"""Migrate the manifest-selected project-content batch with UE 5.7 AssetTools.

Required process environment:
  CODEX_BULK_MANIFEST
  CODEX_BULK_EXPECTED_COUNT
  CODEX_BULK_TARGET_ROOT (or CODEX_EXPECTED_TARGET_ROOT)
  CODEX_BULK_MIGRATION_EVIDENCE (or CODEX_BULK_EVIDENCE)
  CODEX_BULK_HARNESS_CONTENT (or CODEX_EXPECTED_HARNESS_CONTENT)
  CODEX_BULK_PREEXISTING_EXCLUSIONS (semicolon-separated package names)
  CODEX_BULK_EXCLUDED_PREFIXES (semicolon-separated /Game prefixes)

Optional process environment:
  CODEX_BULK_CHUNK_SIZE (default: 25)
"""

import csv
import datetime
import hashlib
import json
import os

import unreal


MANIFEST_PATH = os.environ.get("CODEX_BULK_MANIFEST", "").strip()
EXPECTED_COUNT_VALUE = os.environ.get("CODEX_BULK_EXPECTED_COUNT", "").strip()
TARGET_ROOT_VALUE = (
    os.environ.get("CODEX_BULK_TARGET_ROOT", "").strip()
    or os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
)
EVIDENCE_PATH_VALUE = (
    os.environ.get("CODEX_BULK_MIGRATION_EVIDENCE", "").strip()
    or os.environ.get("CODEX_BULK_EVIDENCE", "").strip()
)
EXPECTED_HARNESS_CONTENT_VALUE = (
    os.environ.get("CODEX_BULK_HARNESS_CONTENT", "").strip()
    or os.environ.get("CODEX_EXPECTED_HARNESS_CONTENT", "").strip()
)
CHUNK_SIZE_VALUE = os.environ.get("CODEX_BULK_CHUNK_SIZE", "25").strip()
PREEXISTING_EXCLUSIONS_VALUE = os.environ.get(
    "CODEX_BULK_PREEXISTING_EXCLUSIONS", ""
).strip()
PREEXISTING_EXCLUSIONS = {
    package.strip().lower()
    for package in PREEXISTING_EXCLUSIONS_VALUE.split(";")
    if package.strip()
}
EXCLUDED_PREFIXES_VALUE = os.environ.get(
    "CODEX_BULK_EXCLUDED_PREFIXES", ""
).strip()
EXCLUDED_PREFIXES = {
    prefix.strip().rstrip("/").lower()
    for prefix in EXCLUDED_PREFIXES_VALUE.split(";")
    if prefix.strip()
}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")

# These eight DataAssets are instances of project Blueprint classes that were
# omitted from the detached harness because those class packages already exist
# in the UE 5.8 target.  Stage this exact support closure in Harness57, then
# load it in dependency order before AssetTools opens any Calysto Blueprints.
CALYSTO_DATA_ASSET_PACKAGES = (
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge",
    "/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawnerBlue",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawnerGreen",
)
CALYSTO_CLASS_SUPPORT_LOAD_ORDER = (
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
)
CALYSTO_PARENT_BLUEPRINT_PACKAGES = {
    package
    for package in CALYSTO_CLASS_SUPPORT_LOAD_ORDER
    if package.rsplit("/", 1)[-1].startswith("PDA_")
}


def fail(message):
    unreal.log_error("CODEX_BULK_PROJECT_CONTENT57_FAIL: " + message)
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


def package_file(content_root, package_name):
    if not package_name.startswith("/Game/"):
        fail("Manifest PackageName is not rooted at /Game: " + package_name)
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def source_relative(source_file):
    normalized = str(source_file or "").strip().replace("\\", "/")
    if not normalized.lower().endswith(".uasset"):
        fail("Selected SourceFile is not a .uasset: " + normalized)
    lower = normalized.lower()
    if lower.startswith("content/"):
        relative = normalized[len("Content/") :]
    elif "/content/" in lower:
        marker = lower.rfind("/content/")
        relative = normalized[marker + len("/content/") :]
    else:
        fail("Selected SourceFile is not rooted below Content: " + normalized)
    if not relative or relative.startswith("/"):
        fail("Selected SourceFile has an invalid Content-relative path: " + normalized)
    return relative.replace("/", os.sep)


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
        MANIFEST_PATH,
        EXPECTED_COUNT_VALUE,
        TARGET_ROOT_VALUE,
        EVIDENCE_PATH_VALUE,
        EXPECTED_HARNESS_CONTENT_VALUE,
        PREEXISTING_EXCLUSIONS_VALUE,
        EXCLUDED_PREFIXES_VALUE,
    )
):
    fail("All documented bulk migration environment variables are required")
try:
    expected_count = int(EXPECTED_COUNT_VALUE)
except ValueError:
    fail("CODEX_BULK_EXPECTED_COUNT is not an integer")
if expected_count <= 0:
    fail("CODEX_BULK_EXPECTED_COUNT must be positive")
try:
    chunk_size = int(CHUNK_SIZE_VALUE)
except ValueError:
    fail("CODEX_BULK_CHUNK_SIZE is not an integer")
if chunk_size <= 0:
    fail("CODEX_BULK_CHUNK_SIZE must be positive")
if any(not prefix.startswith("/game/") for prefix in EXCLUDED_PREFIXES):
    fail("Every CODEX_BULK_EXCLUDED_PREFIXES entry must start with /Game/")

target_root = os.path.realpath(TARGET_ROOT_VALUE)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_harness_content = os.path.realpath(EXPECTED_HARNESS_CONTENT_VALUE)
manifest_path = os.path.realpath(MANIFEST_PATH)
evidence_path = os.path.realpath(EVIDENCE_PATH_VALUE)
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Target root is not NoShellForWinter")
if not os.path.isdir(target_content) or not is_under(target_content, target_root):
    fail("Target Content is absent or escapes the target project")
if harness_content.lower() != expected_harness_content.lower():
    fail("UE 5.7 commandlet is not running in the audited harness")
if is_under(harness_content, target_content):
    fail("Harness Content must be detached from live target Content")
if not os.path.isfile(manifest_path):
    fail("Bulk CSV manifest is absent: " + manifest_path)
saved_migration_root = os.path.realpath(
    os.path.join(target_root, "Saved", "Migration")
)
if not is_under(evidence_path, saved_migration_root):
    fail("Migration evidence path escapes target Saved/Migration")

with open(manifest_path, "r", encoding="utf-8-sig", newline="") as handle:
    reader = csv.DictReader(handle)
    required_columns = {
        "PackageName",
        "SourceFile",
        "SourceLength",
        "SourceSHA256",
        "Classification",
        "Result",
    }
    if not reader.fieldnames or not required_columns.issubset(reader.fieldnames):
        fail("Bulk manifest is missing required columns")
    manifest_rows = list(reader)

selected = []
seen_packages = set()
seen_staged_files = set()
matched_exclusions = set()
matched_prefix_counts = {prefix: 0 for prefix in EXCLUDED_PREFIXES}
for row in manifest_rows:
    if str(row.get("Result", "")).strip().upper() != "PENDING":
        continue
    if (
        str(row.get("Classification", "")).strip().upper()
        != "SOURCE_ONLY_PROJECT_CONTENT"
    ):
        continue
    source_file_value = str(row.get("SourceFile", "")).strip()
    if not source_file_value.lower().endswith(".uasset"):
        continue
    package = str(row.get("PackageName", "")).strip()
    if "/_quarantine/" in package.lower():
        continue
    matching_prefix = next(
        (
            prefix
            for prefix in EXCLUDED_PREFIXES
            if package.lower() == prefix
            or package.lower().startswith(prefix + "/")
        ),
        "",
    )
    if matching_prefix:
        matched_prefix_counts[matching_prefix] += 1
        continue
    if package.lower() in PREEXISTING_EXCLUSIONS:
        matched_exclusions.add(package.lower())
        continue
    target_file = package_file(target_content, package)
    try:
        expected_length = int(str(row.get("SourceLength", "")).strip())
    except ValueError:
        fail("Invalid SourceLength for " + package)
    expected_hash = str(row.get("SourceSHA256", "")).strip().upper()
    if len(expected_hash) != 64:
        fail("Invalid SourceSHA256 for " + package)
    if os.path.exists(target_file) and not os.path.isfile(target_file):
        fail("Target package path exists but is not a file: " + package)
    already_present_at_start = os.path.isfile(target_file)
    if not already_present_at_start:
        for extension in (".uexp", ".ubulk", ".uptnl"):
            if os.path.exists(os.path.splitext(target_file)[0] + extension):
                fail("Target sidecar collision exists for " + package)
    relative = source_relative(source_file_value)
    staged_file = os.path.realpath(os.path.join(harness_content, relative))
    if not is_under(staged_file, harness_content):
        fail("Staged file escapes harness Content for " + package)
    expected_relative = (
        package[len("/Game/") :].replace("/", os.sep) + ".uasset"
    )
    if os.path.normcase(relative) != os.path.normcase(expected_relative):
        fail("PackageName and SourceFile differ for " + package)
    if package in seen_packages or staged_file.lower() in seen_staged_files:
        fail("Duplicate selected package or staged file: " + package)
    seen_packages.add(package)
    seen_staged_files.add(staged_file.lower())
    if not os.path.isfile(staged_file):
        fail("Staged harness file is absent for " + package)
    actual_length = os.path.getsize(staged_file)
    actual_hash = sha256(staged_file)
    if actual_length != expected_length or actual_hash != expected_hash:
        fail("Staged harness bytes differ from manifest for " + package)
    selected.append(
        {
            "package": package,
            "staged_file": staged_file,
            "target_file": target_file,
            "source_length": expected_length,
            "source_sha256": expected_hash,
            "source_file_manifest": source_file_value,
            "already_present_at_start": already_present_at_start,
        }
    )

selected.sort(key=lambda row: row["package"])
if matched_exclusions != PREEXISTING_EXCLUSIONS:
    fail(
        "Preexisting exclusions did not all match manifest candidates: {}".format(
            sorted(PREEXISTING_EXCLUSIONS - matched_exclusions)
        )
    )
if any(count <= 0 for count in matched_prefix_counts.values()):
    fail(
        "Excluded prefixes did not match manifest candidates: {}".format(
            sorted(
                prefix
                for prefix, count in matched_prefix_counts.items()
                if count <= 0
            )
        )
    )
if len(selected) != expected_count:
    fail(
        "Selected package count differs: {} != {}".format(
            len(selected), expected_count
        )
    )
if any("/_quarantine/" in row["package"].lower() for row in selected):
    fail("Quarantine package reached the selected batch")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()

calysto_support_report = []
selected_package_names = {row["package"] for row in selected}
if selected_package_names.intersection(CALYSTO_DATA_ASSET_PACKAGES):
    manifest_by_package = {
        str(row.get("PackageName", "")).strip(): row for row in manifest_rows
    }
    primary_data_asset_class = unreal.load_class(
        None, "/Script/Engine.PrimaryDataAsset"
    )
    if primary_data_asset_class is None:
        fail("PrimaryDataAsset native class did not load")
    for package in CALYSTO_CLASS_SUPPORT_LOAD_ORDER:
        support_file = package_file(harness_content, package)
        manifest_row = manifest_by_package.get(package)
        if manifest_row is None:
            fail("Calysto class support package is absent from manifest: " + package)
        if not os.path.isfile(support_file):
            fail(
                "Calysto class support package is absent from Harness57; run "
                "Stage-CalystoDataAssetSupport57.ps1 after Unreal exits: "
                + package
            )
        try:
            expected_length = int(
                str(manifest_row.get("SourceLength", "")).strip()
            )
        except ValueError:
            fail("Invalid support SourceLength for " + package)
        expected_hash = str(
            manifest_row.get("SourceSHA256", "")
        ).strip().upper()
        if (
            os.path.getsize(support_file) != expected_length
            or sha256(support_file) != expected_hash
        ):
            fail("Staged Calysto support bytes differ from manifest: " + package)
        asset = unreal.EditorAssetLibrary.load_asset(package)
        if asset is None:
            fail("Calysto class support asset did not load: " + package)
        asset_class = asset.get_class().get_name()
        support_row = {
            "package": package,
            "asset_class": asset_class,
            "compile_requested": False,
        }
        if package in CALYSTO_PARENT_BLUEPRINT_PACKAGES:
            if asset_class != "Blueprint":
                fail("Calysto PDA support asset is not a Blueprint: " + package)
            object_name = package.rsplit("/", 1)[-1]
            generated_class_path = package + "." + object_name + "_C"
            generated_class = unreal.load_class(None, generated_class_path)
            if generated_class is None:
                unreal.BlueprintEditorLibrary.compile_blueprint(asset)
                support_row["compile_requested"] = True
                generated_class = unreal.load_class(None, generated_class_path)
            if generated_class is None:
                fail("Calysto PDA generated class did not load: " + package)
            if not unreal.MathLibrary.class_is_child_of(
                generated_class, primary_data_asset_class
            ):
                fail("Calysto PDA class is not a PrimaryDataAsset: " + package)
            support_row["generated_class"] = generated_class.get_path_name()
        calysto_support_report.append(support_row)

    # Construct each instance while every PDA_*_C is resident. This turns the
    # prior AssetTools "package didn't contain an asset" into an early,
    # package-specific failure if any class dependency is still incomplete.
    for package in CALYSTO_DATA_ASSET_PACKAGES:
        if package not in selected_package_names:
            continue
        asset = unreal.EditorAssetLibrary.load_asset(package)
        if asset is None:
            fail("Calysto DataAsset did not load after class prewarm: " + package)
    unreal.log(
        "CODEX_CALYSTO_DATA_ASSET_PREWARM_PASS: support={} instances={}".format(
            len(calysto_support_report),
            len(
                selected_package_names.intersection(
                    CALYSTO_DATA_ASSET_PACKAGES
                )
            ),
        )
    )

for row in selected:
    assets = list(
        registry.get_assets_by_package_name(
            row["package"], include_only_on_disk_assets=True
        )
    )
    if not assets:
        fail("Harness Asset Registry has no on-disk asset for " + row["package"])
    row["asset_classes"] = sorted({class_name(asset) for asset in assets})

before = inventory(target_content)
already_present_rows = [
    row for row in selected if row["already_present_at_start"]
]
rows_to_migrate = [
    row for row in selected if not row["already_present_at_start"]
]
expected_created = {
    os.path.relpath(row["target_file"], target_content)
    .replace(os.sep, "/")
    .lower()
    for row in rows_to_migrate
}
migration_options = unreal.MigrationOptions()
migration_options.set_editor_property("prompt", False)
migration_options.set_editor_property("ignore_dependencies", True)
migration_options.set_editor_property(
    "asset_conflict", unreal.AssetMigrationConflict.SKIP
)
migration_options.set_editor_property("orphan_folder", "")

chunk_count = (len(rows_to_migrate) + chunk_size - 1) // chunk_size
unreal.log(
    "CODEX_BULK_PROJECT_CONTENT57_BEGIN: cohort={} already_present={} migrate={} chunk_size={} chunks={}".format(
        len(selected),
        len(already_present_rows),
        len(rows_to_migrate),
        chunk_size,
        chunk_count,
    )
)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
chunk_reports = []
current_inventory = before


def migrate_attempt(batch, phase, attempt_index, attempt_count, prior_inventory):
    expected_chunk_created = {
        os.path.relpath(row["target_file"], target_content)
        .replace(os.sep, "/")
        .lower()
        for row in batch
    }
    unreal.log(
        "CODEX_BULK_PROJECT_CONTENT57_ATTEMPT_BEGIN: phase={} attempt={}/{} packages={} first={} last={}".format(
            phase,
            attempt_index,
            attempt_count,
            len(batch),
            batch[0]["package"],
            batch[-1]["package"],
        )
    )
    call_error = ""
    try:
        asset_tools.migrate_packages(
            [row["package"] for row in batch], target_content, migration_options
        )
    except Exception as exception:
        call_error = repr(exception)
        unreal.log_warning(
            "CODEX_BULK_PROJECT_CONTENT57_ATTEMPT_EXCEPTION: phase={} attempt={}/{} error={}".format(
                phase, attempt_index, attempt_count, call_error
            )
        )
    chunk_after = inventory(target_content)
    chunk_created = sorted(set(chunk_after) - set(prior_inventory))
    chunk_removed = sorted(set(prior_inventory) - set(chunk_after))
    chunk_modified = sorted(
        relative
        for relative in set(prior_inventory).intersection(chunk_after)
        if prior_inventory[relative] != chunk_after[relative]
    )
    if (
        not set(chunk_created).issubset(expected_chunk_created)
        or chunk_removed
        or chunk_modified
    ):
        fail(
            "Attempt {} {}/{} target delta is unsafe; created={}, allowed={}, removed={}, modified={}".format(
                phase,
                attempt_index,
                attempt_count,
                len(chunk_created),
                len(expected_chunk_created),
                chunk_removed[:20],
                chunk_modified[:20],
            )
        )
    missing = [row for row in batch if not os.path.isfile(row["target_file"])]
    unreal.log(
        "CODEX_BULK_PROJECT_CONTENT57_ATTEMPT_END: phase={} attempt={}/{} created={} missing={}".format(
            phase,
            attempt_index,
            attempt_count,
            len(chunk_created),
            len(missing),
        )
    )
    return missing, chunk_after, {
        "phase": phase,
        "attempt_index": attempt_index,
        "package_count": len(batch),
        "first_package": batch[0]["package"],
        "last_package": batch[-1]["package"],
        "created_package_files": chunk_created,
        "missing_packages_after_attempt": [row["package"] for row in missing],
        "asset_tools_exception": call_error,
        "target_delta_safe_subset": True,
    }


for chunk_index, start in enumerate(range(0, len(rows_to_migrate), chunk_size), 1):
    chunk = rows_to_migrate[start : start + chunk_size]
    _missing, current_inventory, report = migrate_attempt(
        chunk, "primary", chunk_index, chunk_count, current_inventory
    )
    chunk_reports.append(report)

missing_rows = [
    row for row in rows_to_migrate if not os.path.isfile(row["target_file"])
]
for retry_round in range(1, 3):
    if not missing_rows:
        break
    retry_batches = [
        missing_rows[start : start + chunk_size]
        for start in range(0, len(missing_rows), chunk_size)
    ]
    for retry_index, retry_batch in enumerate(retry_batches, 1):
        _missing, current_inventory, report = migrate_attempt(
            retry_batch,
            "retry_round_{}".format(retry_round),
            retry_index,
            len(retry_batches),
            current_inventory,
        )
        chunk_reports.append(report)
    missing_rows = [
        row for row in rows_to_migrate if not os.path.isfile(row["target_file"])
    ]

if missing_rows:
    individual_count = len(missing_rows)
    for individual_index, row in enumerate(list(missing_rows), 1):
        _missing, current_inventory, report = migrate_attempt(
            [row],
            "individual",
            individual_index,
            individual_count,
            current_inventory,
        )
        chunk_reports.append(report)
    missing_rows = [
        row for row in rows_to_migrate if not os.path.isfile(row["target_file"])
    ]

if missing_rows:
    fail(
        "AssetTools could not create {} cohort packages after retries: {}".format(
            len(missing_rows), [row["package"] for row in missing_rows[:50]]
        )
    )

after = current_inventory
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
            len(created), len(expected_created), removed[:20], modified[:20]
        )
    )

package_rows = []
for row in selected:
    staged_length = os.path.getsize(row["staged_file"])
    staged_hash = sha256(row["staged_file"])
    if (
        staged_length != row["source_length"]
        or staged_hash != row["source_sha256"]
    ):
        fail("Staged source changed during migration: " + row["package"])
    if not os.path.isfile(row["target_file"]):
        fail("AssetTools did not create target package: " + row["package"])
    package_rows.append(
        {
            "package": row["package"],
            "asset_classes": row["asset_classes"],
            "source_file_manifest": row["source_file_manifest"],
            "already_present_at_start": row["already_present_at_start"],
            "staged_file": row["staged_file"],
            "staged_length": staged_length,
            "staged_sha256": staged_hash,
            "file": row["target_file"],
            "length": os.path.getsize(row["target_file"]),
            "sha256": sha256(row["target_file"]),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_BULK_PROJECT_CONTENT57_PASS",
    "engine_version": engine_version,
    "manifest": manifest_path,
    "expected_count": expected_count,
    "package_count": len(package_rows),
    "chunk_size": chunk_size,
    "chunk_count": chunk_count,
    "chunks": chunk_reports,
    "already_present_at_start": [
        row["package"] for row in already_present_rows
    ],
    "created_this_run": created,
    "preexisting_exclusions": sorted(PREEXISTING_EXCLUSIONS),
    "user_excluded_prefixes": sorted(EXCLUDED_PREFIXES),
    "user_excluded_prefix_counts": matched_prefix_counts,
    "quarantine_excluded": True,
    "quarantine_package_count_selected": 0,
    "selection_contract": {
        "result": "PENDING",
        "classification": "SOURCE_ONLY_PROJECT_CONTENT",
        "source_extension": ".uasset",
        "cohort_condition": "all matching rows except explicit preexisting exclusions",
        "target_must_be_absent_to_migrate": True,
        "package_substring_excluded_case_insensitive": "/_Quarantine/",
        "user_excluded_prefixes": sorted(EXCLUDED_PREFIXES),
    },
    "harness_project": os.path.realpath(unreal.Paths.get_project_file_path()),
    "harness_content": harness_content,
    "calysto_data_asset_class_support": calysto_support_report,
    "target_root": target_root,
    "destination": target_content,
    "packages": package_rows,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_existing_package_files": modified,
    "target_delta_exact": True,
    "staging_unchanged": True,
    "ignore_dependencies": True,
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "raw_copy_to_target_content": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary_path = evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, evidence_path)
unreal.log("CODEX_BULK_PROJECT_CONTENT57_PASS: " + evidence_path)
