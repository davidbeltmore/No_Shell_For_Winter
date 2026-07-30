"""Read-only UE 5.8 validation of the full post-repair bulk cohort.

Required process environment:
  CODEX_BULK_MIGRATION_EVIDENCE (or CODEX_BULK_EVIDENCE)
  CODEX_FOOD_PARENT_REPAIR_EVIDENCE
  CODEX_POST_REPAIR_BULK_VALIDATION_EVIDENCE

Optional process environment:
  CODEX_BULK_EXPECTED_COUNT (default: 1667)
  CODEX_FOOD_PARENT_EXPECTED_COUNT (default: 81)
  CODEX_KAWAII_QUINN_REPAIR_EVIDENCE
  CODEX_CALYSTO_REMIGRATE_EVIDENCE
  CODEX_CALYSTO_CORE3_REMIGRATE_EVIDENCE
  CODEX_CALYSTO_SMART_SCATTER_REPAIR_EVIDENCE
  CODEX_NULL_PARENT_GAMEPLAY_REPAIR_EVIDENCE

The 81 repaired Food pickup packages are checked against the after-snapshots in
FoodPickupParentRepair58.json. When Kawaii Quinn repair evidence is supplied,
its 29 tracked packages (14 AnimSequences, 14 PoseAssets, and the post-process
AnimBlueprint) receive the same before-to-migration / after-to-current overlay.
When Calysto remigration evidence is supplied, its exact 21 editor Blueprints
are overlaid from target_before to target_after using the same hash chain.
The optional Calysto Core3 remigration overlay uses the exact three guarded
target_after snapshots and proves their detached pre-remigration backup starts
from the bulk migration bytes. The optional null-parent gameplay overlay uses
the exact two repaired Blueprint after-snapshots and requires its before state
to match the bulk migration bytes.
The optional Calysto Smart Scatter overlay is applied after Core3. It guards
exactly ST_SmartScatter and PDA_VegetationCalysto; the latter must chain from
Core3 target_after when both overlays are enabled.
Every package outside enabled overlays is checked against its original UE 5.7
migration hash. All assets are loaded and all Blueprints are compiled in memory;
this script never invokes an asset-save API.
"""

import datetime
import hashlib
import json
import os

import unreal


BULK_COUNT = 1667
FOOD_REPAIR_COUNT = 81
KAWAII_QUINN_TRACKED_COUNT = 29
KAWAII_QUINN_MEMBER_COUNT = 14
CALYSTO_REMIGRATE_COUNT = 21
CALYSTO_CORE3_REMIGRATE_COUNT = 3
CALYSTO_SMART_SCATTER_REPAIR_COUNT = 2
NULL_PARENT_GAMEPLAY_REPAIR_COUNT = 2
FOOD_PICKUP_PREFIX = "/Game/_Game/FoodSystem/Food/Items/PickuableItems/"
KAWAII_QUINN_POSE_PREFIX = (
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/Rigs/Poses/Quinn/"
)
KAWAII_QUINN_POST_PROCESS_ABP = (
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/"
    "Rigs/ABP_Quinn_PostProcess"
)
CALYSTO_EDITOR_BLUEPRINT_PACKAGES = (
    "/Game/Calysto/Shared/Blueprint/Editor/BP_DebugProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_RemoveSpawnerProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_SpawnerOverrideProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_ToolsProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_DecorRoomProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_EditDungeonProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_PaintRoomProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_PlaceLightProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_SpawnerProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_WallOverrideProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_DebugTool",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_DragMaster",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_EditorModularBehabiorTool",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_OverrideSpawner",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_RemoveSpawner",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_DecorateRoom",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_EditDungeon",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_OverrideSpawner",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_OverrideWall",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_PaintRoom",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_PlaceLight",
)
CALYSTO_CORE3_PACKAGES = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController",
    "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto",
)
CALYSTO_SMART_SCATTER_PACKAGES = (
    "/Game/Calysto/Shared/Data/Structure/ST_SmartScatter",
    "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto",
)
NULL_PARENT_GAMEPLAY_PACKAGES = (
    "/Game/Procedural/Blueprints/Altar",
    "/Game/_Game/Lockpicking/Locked",
)
EXPORTED_ANIMATIONS_PREFIX = "/Game/ExportedAnimations"

EXPECTED_COUNT_VALUE = os.environ.get(
    "CODEX_BULK_EXPECTED_COUNT", str(BULK_COUNT)
).strip()
REPAIR_EXPECTED_COUNT_VALUE = os.environ.get(
    "CODEX_FOOD_PARENT_EXPECTED_COUNT",
    os.environ.get("CODEX_FOOD_PICKUP_EXPECTED_COUNT", str(FOOD_REPAIR_COUNT)),
).strip()
MIGRATION_EVIDENCE_VALUE = (
    os.environ.get("CODEX_BULK_MIGRATION_EVIDENCE", "").strip()
    or os.environ.get("CODEX_BULK_EVIDENCE", "").strip()
)
REPAIR_EVIDENCE_VALUE = (
    os.environ.get("CODEX_FOOD_PARENT_REPAIR_EVIDENCE", "").strip()
    or os.environ.get("CODEX_FOOD_PICKUP_REPAIR_EVIDENCE", "").strip()
)
KAWAII_REPAIR_EVIDENCE_VALUE = os.environ.get(
    "CODEX_KAWAII_QUINN_REPAIR_EVIDENCE", ""
).strip()
CALYSTO_REMIGRATE_EVIDENCE_VALUE = os.environ.get(
    "CODEX_CALYSTO_REMIGRATE_EVIDENCE", ""
).strip()
CALYSTO_CORE3_REMIGRATE_EVIDENCE_VALUE = os.environ.get(
    "CODEX_CALYSTO_CORE3_REMIGRATE_EVIDENCE", ""
).strip()
CALYSTO_SMART_SCATTER_REPAIR_EVIDENCE_VALUE = (
    os.environ.get("CODEX_CALYSTO_SMART_SCATTER_REPAIR_EVIDENCE", "").strip()
    or os.environ.get("CODEX_CALYSTO_SMART_REPAIR_EVIDENCE", "").strip()
)
NULL_PARENT_GAMEPLAY_REPAIR_EVIDENCE_VALUE = os.environ.get(
    "CODEX_NULL_PARENT_GAMEPLAY_REPAIR_EVIDENCE", ""
).strip()
VALIDATION_EVIDENCE_VALUE = (
    os.environ.get("CODEX_POST_REPAIR_BULK_VALIDATION_EVIDENCE", "").strip()
    or os.environ.get("CODEX_BULK_POST_REPAIR_VALIDATION_EVIDENCE", "").strip()
)


def fail(message):
    unreal.log_error("CODEX_POST_REPAIR_BULK_PROJECT_CONTENT58_FAIL: " + message)
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


def is_exported_animation(package_name):
    normalized = str(package_name).strip().rstrip("/").lower()
    prefix = EXPORTED_ANIMATIONS_PREFIX.lower()
    return normalized == prefix or normalized.startswith(prefix + "/")


def package_file(content_root, package_name):
    if not package_name.startswith("/Game/"):
        fail("Evidence contains a non-/Game package: " + package_name)
    if is_exported_animation(package_name):
        fail("ExportedAnimations is explicitly excluded: " + package_name)
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def file_snapshot(path):
    if not os.path.isfile(path):
        return None
    stat = os.stat(path)
    return {
        "file": path,
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(path),
    }


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def normalized_blueprint_status(asset):
    status = str(asset.get_editor_property("status"))
    normalized = "".join(
        character for character in status.upper() if character.isalnum()
    )
    return status, normalized


def validate_recorded_snapshot(record, expected_path, description):
    if not isinstance(record, dict):
        fail(description + " is not a snapshot object")
    recorded_path = os.path.realpath(str(record.get("file", "")))
    if recorded_path.lower() != expected_path.lower():
        fail(description + " file path differs from the target package path")
    length = record.get("length")
    digest = str(record.get("sha256", "")).upper()
    if not isinstance(length, int) or length < 0:
        fail(description + " has an invalid length")
    if len(digest) != 64 or any(character not in "0123456789ABCDEF" for character in digest):
        fail(description + " has an invalid SHA-256")
    return {"file": recorded_path, "length": length, "sha256": digest}


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not all(
    (
        MIGRATION_EVIDENCE_VALUE,
        REPAIR_EVIDENCE_VALUE,
        VALIDATION_EVIDENCE_VALUE,
    )
):
    fail("Migration, Food repair, and post-repair validation evidence paths are required")
try:
    expected_count = int(EXPECTED_COUNT_VALUE)
    repair_expected_count = int(REPAIR_EXPECTED_COUNT_VALUE)
except ValueError:
    fail("Expected package counts must be integers")
if expected_count != BULK_COUNT:
    fail("The post-repair bulk contract requires exactly 1667 packages")
if repair_expected_count != FOOD_REPAIR_COUNT:
    fail("The Food pickup overlay contract requires exactly 81 packages")
if (
    len(CALYSTO_EDITOR_BLUEPRINT_PACKAGES) != CALYSTO_REMIGRATE_COUNT
    or len(set(CALYSTO_EDITOR_BLUEPRINT_PACKAGES)) != CALYSTO_REMIGRATE_COUNT
):
    fail("The Calysto remigration overlay contract requires exactly 21 packages")
if any(
    is_exported_animation(package)
    for package in CALYSTO_EDITOR_BLUEPRINT_PACKAGES
):
    fail("The guarded Calysto package set overlaps ExportedAnimations")
if (
    len(CALYSTO_CORE3_PACKAGES) != CALYSTO_CORE3_REMIGRATE_COUNT
    or len(set(CALYSTO_CORE3_PACKAGES)) != CALYSTO_CORE3_REMIGRATE_COUNT
):
    fail("The Calysto Core3 remigration overlay contract requires exactly 3 packages")
if (
    len(NULL_PARENT_GAMEPLAY_PACKAGES) != NULL_PARENT_GAMEPLAY_REPAIR_COUNT
    or len(set(NULL_PARENT_GAMEPLAY_PACKAGES))
    != NULL_PARENT_GAMEPLAY_REPAIR_COUNT
):
    fail("The null-parent gameplay overlay contract requires exactly 2 packages")
if (
    len(CALYSTO_SMART_SCATTER_PACKAGES) != CALYSTO_SMART_SCATTER_REPAIR_COUNT
    or len(set(CALYSTO_SMART_SCATTER_PACKAGES))
    != CALYSTO_SMART_SCATTER_REPAIR_COUNT
):
    fail("The Calysto Smart Scatter overlay contract requires exactly 2 packages")
if any(
    is_exported_animation(package)
    for package in (
        CALYSTO_CORE3_PACKAGES
        + CALYSTO_SMART_SCATTER_PACKAGES
        + NULL_PARENT_GAMEPLAY_PACKAGES
    )
):
    fail("A guarded Core3/Smart Scatter/null-parent package overlaps ExportedAnimations")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if content_root.lower() != os.path.realpath(
    os.path.join(project_root, "Content")
).lower():
    fail("Target Content invariant failed")

saved_migration_root = os.path.realpath(
    os.path.join(project_root, "Saved", "Migration")
)
migration_evidence_path = os.path.realpath(MIGRATION_EVIDENCE_VALUE)
repair_evidence_path = os.path.realpath(REPAIR_EVIDENCE_VALUE)
kawaii_repair_evidence_path = (
    os.path.realpath(KAWAII_REPAIR_EVIDENCE_VALUE)
    if KAWAII_REPAIR_EVIDENCE_VALUE
    else ""
)
calysto_remigrate_evidence_path = (
    os.path.realpath(CALYSTO_REMIGRATE_EVIDENCE_VALUE)
    if CALYSTO_REMIGRATE_EVIDENCE_VALUE
    else ""
)
calysto_core3_remigrate_evidence_path = (
    os.path.realpath(CALYSTO_CORE3_REMIGRATE_EVIDENCE_VALUE)
    if CALYSTO_CORE3_REMIGRATE_EVIDENCE_VALUE
    else ""
)
calysto_smart_scatter_repair_evidence_path = (
    os.path.realpath(CALYSTO_SMART_SCATTER_REPAIR_EVIDENCE_VALUE)
    if CALYSTO_SMART_SCATTER_REPAIR_EVIDENCE_VALUE
    else ""
)
null_parent_gameplay_repair_evidence_path = (
    os.path.realpath(NULL_PARENT_GAMEPLAY_REPAIR_EVIDENCE_VALUE)
    if NULL_PARENT_GAMEPLAY_REPAIR_EVIDENCE_VALUE
    else ""
)
validation_evidence_path = os.path.realpath(VALIDATION_EVIDENCE_VALUE)
evidence_paths = [
    ("Migration evidence", migration_evidence_path),
    ("Food repair evidence", repair_evidence_path),
    ("Validation evidence", validation_evidence_path),
]
if kawaii_repair_evidence_path:
    evidence_paths.append(("Kawaii Quinn repair evidence", kawaii_repair_evidence_path))
if calysto_remigrate_evidence_path:
    evidence_paths.append(
        ("Calysto remigration evidence", calysto_remigrate_evidence_path)
    )
if calysto_core3_remigrate_evidence_path:
    evidence_paths.append(
        ("Calysto Core3 remigration evidence", calysto_core3_remigrate_evidence_path)
    )
if calysto_smart_scatter_repair_evidence_path:
    evidence_paths.append(
        (
            "Calysto Smart Scatter repair evidence",
            calysto_smart_scatter_repair_evidence_path,
        )
    )
if null_parent_gameplay_repair_evidence_path:
    evidence_paths.append(
        (
            "Null-parent gameplay repair evidence",
            null_parent_gameplay_repair_evidence_path,
        )
    )
for label, path in evidence_paths:
    if not is_under(path, saved_migration_root):
        fail(label + " escapes target Saved/Migration")
distinct_evidence_paths = {
    migration_evidence_path.lower(),
    repair_evidence_path.lower(),
    validation_evidence_path.lower(),
}
if kawaii_repair_evidence_path:
    distinct_evidence_paths.add(kawaii_repair_evidence_path.lower())
if calysto_remigrate_evidence_path:
    distinct_evidence_paths.add(calysto_remigrate_evidence_path.lower())
if calysto_core3_remigrate_evidence_path:
    distinct_evidence_paths.add(calysto_core3_remigrate_evidence_path.lower())
if calysto_smart_scatter_repair_evidence_path:
    distinct_evidence_paths.add(calysto_smart_scatter_repair_evidence_path.lower())
if null_parent_gameplay_repair_evidence_path:
    distinct_evidence_paths.add(null_parent_gameplay_repair_evidence_path.lower())
expected_distinct_path_count = (
    3
    + int(bool(kawaii_repair_evidence_path))
    + int(bool(calysto_remigrate_evidence_path))
    + int(bool(calysto_core3_remigrate_evidence_path))
    + int(bool(calysto_smart_scatter_repair_evidence_path))
    + int(bool(null_parent_gameplay_repair_evidence_path))
)
if len(distinct_evidence_paths) != expected_distinct_path_count:
    fail("Input and output evidence paths must be distinct")
if not os.path.isfile(migration_evidence_path):
    fail("UE 5.7 bulk migration evidence is absent")
if not os.path.isfile(repair_evidence_path):
    fail("Food pickup parent repair evidence is absent")
if kawaii_repair_evidence_path and not os.path.isfile(kawaii_repair_evidence_path):
    fail("Kawaii Quinn repair evidence is absent")
if calysto_remigrate_evidence_path and not os.path.isfile(
    calysto_remigrate_evidence_path
):
    fail("Calysto remigration evidence is absent")
if calysto_core3_remigrate_evidence_path and not os.path.isfile(
    calysto_core3_remigrate_evidence_path
):
    fail("Calysto Core3 remigration evidence is absent")
if calysto_smart_scatter_repair_evidence_path and not os.path.isfile(
    calysto_smart_scatter_repair_evidence_path
):
    fail("Calysto Smart Scatter repair evidence is absent")
if null_parent_gameplay_repair_evidence_path and not os.path.isfile(
    null_parent_gameplay_repair_evidence_path
):
    fail("Null-parent gameplay repair evidence is absent")

with open(migration_evidence_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if migration.get("status") != "ASSETTOOLS_EXACT_BULK_PROJECT_CONTENT57_PASS":
    fail("UE 5.7 bulk migration evidence status is not PASS")
if migration.get("target_delta_exact") is not True:
    fail("UE 5.7 bulk migration target delta is not exact")
if migration.get("staging_unchanged") is not True:
    fail("UE 5.7 bulk migration staging invariant is not PASS")
if os.path.realpath(str(migration.get("target_root", ""))).lower() != project_root.lower():
    fail("UE 5.7 migration evidence belongs to a different target project")

declared_exclusions = {
    str(prefix).strip().rstrip("/").lower()
    for prefix in migration.get("user_excluded_prefixes", [])
}
if EXPORTED_ANIMATIONS_PREFIX.lower() not in declared_exclusions:
    fail("UE 5.7 evidence does not explicitly exclude /Game/ExportedAnimations")

migration_rows = list(migration.get("packages", []))
packages = [str(row.get("package", "")).strip() for row in migration_rows]
if len(packages) != expected_count or len(set(packages)) != expected_count:
    fail("Migration evidence must contain exactly 1667 unique packages")
if packages != sorted(packages):
    fail("Migration evidence packages are not deterministically sorted")
if any("/_quarantine/" in package.lower() for package in packages):
    fail("Migration evidence contains a forbidden /_Quarantine/ package")
if any(is_exported_animation(package) for package in packages):
    fail("Migration evidence contains an explicitly excluded ExportedAnimations package")
migration_by_package = {row["package"]: row for row in migration_rows}

with open(repair_evidence_path, "r", encoding="utf-8-sig") as handle:
    repair = json.load(handle)
if repair.get("status") != "UE58_FOOD_PICKUP_PARENT_REPAIR_PASS":
    fail("Food pickup parent repair evidence status is not PASS")
if os.path.realpath(str(repair.get("project", ""))).lower() != project_file.lower():
    fail("Food pickup repair evidence belongs to a different target project")
if repair.get("saved_only_selected_packages") is not True:
    fail("Food pickup repair exact save-set invariant is not PASS")

repair_packages = [str(value).strip() for value in repair.get("packages", [])]
if (
    len(repair_packages) != repair_expected_count
    or len(set(repair_packages)) != repair_expected_count
):
    fail("Food pickup repair evidence must contain exactly 81 unique packages")
if repair_packages != sorted(repair_packages):
    fail("Food pickup repair package list is not deterministically sorted")
if any(not package.startswith(FOOD_PICKUP_PREFIX) for package in repair_packages):
    fail("Food pickup repair evidence contains an out-of-scope package")
if any(is_exported_animation(package) for package in repair_packages):
    fail("Food pickup repair evidence contains ExportedAnimations")
if not set(repair_packages).issubset(set(packages)):
    fail("Food pickup repair package set is not a subset of the bulk cohort")
if repair.get("save_operations") != repair_packages:
    fail("Food pickup repair save set differs from its package set")

repair_before = repair.get("package_files_before", {})
repair_after = repair.get("package_files_after", {})
if set(repair_before) != set(repair_packages):
    fail("Food pickup repair before-snapshot set is not exact")
if set(repair_after) != set(repair_packages):
    fail("Food pickup repair after-snapshot set is not exact")

# Prove the repair evidence starts from the exact bytes recorded by UE 5.7.
validated_repair_before = {}
validated_repair_after = {}
for package in repair_packages:
    path = package_file(content_root, package)
    migration_snapshot = validate_recorded_snapshot(
        migration_by_package[package], path, package + " migration snapshot"
    )
    before_snapshot = validate_recorded_snapshot(
        repair_before[package], path, package + " repair before-snapshot"
    )
    after_snapshot = validate_recorded_snapshot(
        repair_after[package], path, package + " repair after-snapshot"
    )
    if (
        before_snapshot["length"] != migration_snapshot["length"]
        or before_snapshot["sha256"] != migration_snapshot["sha256"]
    ):
        fail("Food pickup repair does not chain from UE 5.7 bytes: " + package)
    validated_repair_before[package] = before_snapshot
    validated_repair_after[package] = after_snapshot

kawaii_repair = None
kawaii_tracked_packages = []
validated_kawaii_before = {}
validated_kawaii_after = {}
kawaii_changed_packages = []
if kawaii_repair_evidence_path:
    with open(kawaii_repair_evidence_path, "r", encoding="utf-8-sig") as handle:
        kawaii_repair = json.load(handle)
    if kawaii_repair.get("status") != "UE58_KAWAII_QUINN_POSE_SKELETON_REPAIR_PASS":
        fail("Kawaii Quinn repair evidence status is not PASS")
    if (
        os.path.realpath(str(kawaii_repair.get("project", ""))).lower()
        != project_file.lower()
    ):
        fail("Kawaii Quinn repair evidence belongs to a different target project")
    if kawaii_repair.get("saved_only_guarded_packages") is not True:
        fail("Kawaii Quinn repair exact save-set invariant is not PASS")
    if (
        str(kawaii_repair.get("excluded_prefix", "")).rstrip("/").lower()
        != EXPORTED_ANIMATIONS_PREFIX.lower()
    ):
        fail("Kawaii Quinn evidence does not explicitly exclude ExportedAnimations")

    kawaii_pose_packages = [
        str(value).strip() for value in kawaii_repair.get("packages", [])
    ]
    kawaii_animation_packages = [
        str(value).strip()
        for value in kawaii_repair.get("source_animation_packages", [])
    ]
    kawaii_post_process_abp = str(
        kawaii_repair.get("post_process_anim_blueprint", "")
    ).strip()
    if (
        len(kawaii_pose_packages) != KAWAII_QUINN_MEMBER_COUNT
        or len(set(kawaii_pose_packages)) != KAWAII_QUINN_MEMBER_COUNT
    ):
        fail("Kawaii Quinn evidence must contain exactly 14 unique PoseAssets")
    if (
        len(kawaii_animation_packages) != KAWAII_QUINN_MEMBER_COUNT
        or len(set(kawaii_animation_packages)) != KAWAII_QUINN_MEMBER_COUNT
    ):
        fail("Kawaii Quinn evidence must contain exactly 14 unique AnimSequences")
    if kawaii_pose_packages != sorted(kawaii_pose_packages):
        fail("Kawaii Quinn PoseAsset list is not deterministically sorted")
    if kawaii_animation_packages != sorted(kawaii_animation_packages):
        fail("Kawaii Quinn AnimSequence list is not deterministically sorted")
    if any(
        not package.startswith(KAWAII_QUINN_POSE_PREFIX)
        or not package.endswith("_pose")
        for package in kawaii_pose_packages
    ):
        fail("Kawaii Quinn evidence contains an out-of-scope PoseAsset")
    if any(
        not package.startswith(KAWAII_QUINN_POSE_PREFIX)
        or not package.endswith("_anim")
        for package in kawaii_animation_packages
    ):
        fail("Kawaii Quinn evidence contains an out-of-scope AnimSequence")
    pose_stems = {
        package[: -len("_pose")] for package in kawaii_pose_packages
    }
    animation_stems = {
        package[: -len("_anim")] for package in kawaii_animation_packages
    }
    if pose_stems != animation_stems:
        fail("Kawaii Quinn PoseAsset and AnimSequence member sets differ")
    if kawaii_post_process_abp != KAWAII_QUINN_POST_PROCESS_ABP:
        fail("Kawaii Quinn post-process AnimBlueprint path differs")

    kawaii_tracked_packages = (
        kawaii_animation_packages
        + kawaii_pose_packages
        + [kawaii_post_process_abp]
    )
    if (
        len(kawaii_tracked_packages) != KAWAII_QUINN_TRACKED_COUNT
        or len(set(kawaii_tracked_packages)) != KAWAII_QUINN_TRACKED_COUNT
    ):
        fail("Kawaii Quinn tracked package set must contain exactly 29 packages")
    if any(is_exported_animation(package) for package in kawaii_tracked_packages):
        fail("Kawaii Quinn repair evidence contains ExportedAnimations")
    if not set(kawaii_tracked_packages).issubset(set(packages)):
        fail("Kawaii Quinn tracked package set is not a subset of the bulk cohort")
    if set(kawaii_tracked_packages).intersection(repair_packages):
        fail("Food and Kawaii repair overlays overlap")

    expected_kawaii_save_operations = (
        kawaii_animation_packages
        + kawaii_pose_packages
        + ([kawaii_post_process_abp] if kawaii_repair.get("post_process_saved") else [])
    )
    if kawaii_repair.get("save_operations") != expected_kawaii_save_operations:
        fail("Kawaii Quinn repair save set differs from its guarded contract")

    kawaii_before = kawaii_repair.get("tracked_package_files_before", {})
    kawaii_after = kawaii_repair.get("tracked_package_files_after", {})
    if set(kawaii_before) != set(kawaii_tracked_packages):
        fail("Kawaii Quinn before-snapshot set is not exact")
    if set(kawaii_after) != set(kawaii_tracked_packages):
        fail("Kawaii Quinn after-snapshot set is not exact")
    for package in kawaii_tracked_packages:
        path = package_file(content_root, package)
        migration_snapshot = validate_recorded_snapshot(
            migration_by_package[package], path, package + " migration snapshot"
        )
        before_snapshot = validate_recorded_snapshot(
            kawaii_before[package], path, package + " Kawaii repair before-snapshot"
        )
        after_snapshot = validate_recorded_snapshot(
            kawaii_after[package], path, package + " Kawaii repair after-snapshot"
        )
        if (
            before_snapshot["length"] != migration_snapshot["length"]
            or before_snapshot["sha256"] != migration_snapshot["sha256"]
        ):
            fail("Kawaii Quinn repair does not chain from UE 5.7 bytes: " + package)
        validated_kawaii_before[package] = before_snapshot
        validated_kawaii_after[package] = after_snapshot

    kawaii_changed_packages = [
        package
        for package in kawaii_tracked_packages
        if validated_kawaii_before[package]["sha256"]
        != validated_kawaii_after[package]["sha256"]
    ]
    if (
        kawaii_repair.get("changed_tracked_package_files")
        != kawaii_changed_packages
    ):
        fail("Kawaii Quinn changed-file list differs from its tracked snapshots")

calysto_remigrate = None
calysto_packages = []
validated_calysto_before = {}
validated_calysto_after = {}
calysto_changed_packages = []
if calysto_remigrate_evidence_path:
    with open(
        calysto_remigrate_evidence_path, "r", encoding="utf-8-sig"
    ) as handle:
        calysto_remigrate = json.load(handle)
    if (
        calysto_remigrate.get("status")
        != "ASSETTOOLS_EXACT_CALYSTO_EDITOR_BP57_OVERWRITE_PASS"
    ):
        fail("Calysto remigration evidence status is not PASS")
    if (
        os.path.realpath(str(calysto_remigrate.get("target_root", ""))).lower()
        != project_root.lower()
    ):
        fail("Calysto remigration evidence belongs to a different target project")
    if (
        os.path.realpath(str(calysto_remigrate.get("destination", ""))).lower()
        != content_root.lower()
    ):
        fail("Calysto remigration destination differs from target Content")
    for field in ("target_delta_exact", "harness_unchanged"):
        if calysto_remigrate.get(field) is not True:
            fail("Calysto remigration invariant is not PASS: " + field)
    if calysto_remigrate.get("ignore_dependencies") is not True:
        fail("Calysto remigration did not preserve ignore_dependencies")
    if calysto_remigrate.get("asset_conflict") != "OVERWRITE":
        fail("Calysto remigration conflict policy was not OVERWRITE")
    if calysto_remigrate.get("raw_copy_to_target_content") is not False:
        fail("Calysto remigration evidence does not prohibit raw Content copy")
    if calysto_remigrate.get("exported_animations_excluded") is not True:
        fail("Calysto remigration does not explicitly exclude ExportedAnimations")

    calysto_packages = [
        str(value).strip() for value in calysto_remigrate.get("packages", [])
    ]
    if calysto_packages != list(CALYSTO_EDITOR_BLUEPRINT_PACKAGES):
        fail("Calysto remigration package list differs from the guarded 21")
    if calysto_remigrate.get("package_count") != CALYSTO_REMIGRATE_COUNT:
        fail("Calysto remigration package_count is not 21")
    if any(is_exported_animation(package) for package in calysto_packages):
        fail("Calysto remigration evidence contains ExportedAnimations")
    if not set(calysto_packages).issubset(set(packages)):
        fail("Calysto remigration package set is not a subset of the bulk cohort")
    if set(calysto_packages).intersection(repair_packages):
        fail("Food and Calysto repair overlays overlap")
    if set(calysto_packages).intersection(kawaii_tracked_packages):
        fail("Kawaii and Calysto repair overlays overlap")

    if calysto_remigrate.get("created_package_files") != []:
        fail("Calysto remigration unexpectedly created target package files")
    if calysto_remigrate.get("removed_package_files") != []:
        fail("Calysto remigration unexpectedly removed target package files")
    expected_calysto_modified_files = [
        package[len("/Game/") :].lower() + ".uasset"
        for package in calysto_packages
    ]
    if sorted(calysto_remigrate.get("modified_package_files", [])) != sorted(
        expected_calysto_modified_files
    ):
        fail("Calysto remigration modified-file set is not the exact 21")

    calysto_before = calysto_remigrate.get("target_before", {})
    calysto_after = calysto_remigrate.get("target_after", {})
    if set(calysto_before) != set(calysto_packages):
        fail("Calysto target_before snapshot set is not exact")
    if set(calysto_after) != set(calysto_packages):
        fail("Calysto target_after snapshot set is not exact")
    for package in calysto_packages:
        path = package_file(content_root, package)
        migration_snapshot = validate_recorded_snapshot(
            migration_by_package[package], path, package + " migration snapshot"
        )
        before_snapshot = validate_recorded_snapshot(
            calysto_before[package], path, package + " Calysto target_before"
        )
        after_snapshot = validate_recorded_snapshot(
            calysto_after[package], path, package + " Calysto target_after"
        )
        if (
            before_snapshot["length"] != migration_snapshot["length"]
            or before_snapshot["sha256"] != migration_snapshot["sha256"]
        ):
            fail("Calysto target_before does not match UE 5.7 bulk bytes: " + package)
        if (
            after_snapshot["length"] == before_snapshot["length"]
            and after_snapshot["sha256"] == before_snapshot["sha256"]
        ):
            fail("Calysto target_after did not change remigrated bytes: " + package)
        validated_calysto_before[package] = before_snapshot
        validated_calysto_after[package] = after_snapshot
    calysto_changed_packages = [
        package
        for package in calysto_packages
        if validated_calysto_before[package]["sha256"]
        != validated_calysto_after[package]["sha256"]
    ]
    if len(calysto_changed_packages) != CALYSTO_REMIGRATE_COUNT:
        fail("Calysto remigration did not change all 21 guarded packages")

calysto_core3_remigrate = None
calysto_core3_packages = []
validated_calysto_core3_after = {}
calysto_core3_changed_packages = []
calysto_core3_retry_preexisting_changed_packages = []
if calysto_core3_remigrate_evidence_path:
    with open(
        calysto_core3_remigrate_evidence_path, "r", encoding="utf-8-sig"
    ) as handle:
        calysto_core3_remigrate = json.load(handle)
    if (
        calysto_core3_remigrate.get("status")
        != "ASSETTOOLS_EXACT_CALYSTO_CORE3_OVERWRITE57_PASS"
    ):
        fail("Calysto Core3 remigration evidence status is not PASS")
    if (
        os.path.realpath(
            str(calysto_core3_remigrate.get("target_root", ""))
        ).lower()
        != project_root.lower()
    ):
        fail("Calysto Core3 remigration evidence belongs to a different target")
    for field in ("target_delta_exact", "harness_unchanged"):
        if calysto_core3_remigrate.get(field) is not True:
            fail("Calysto Core3 remigration invariant is not PASS: " + field)
    if calysto_core3_remigrate.get("ignore_dependencies") is not True:
        fail("Calysto Core3 remigration did not preserve ignore_dependencies")
    if calysto_core3_remigrate.get("asset_conflict") != "OVERWRITE":
        fail("Calysto Core3 remigration conflict policy was not OVERWRITE")
    if calysto_core3_remigrate.get("exported_animations_excluded") is not True:
        fail("Calysto Core3 evidence does not explicitly exclude ExportedAnimations")
    if calysto_core3_remigrate.get("source_project_touched") is not False:
        fail("Calysto Core3 evidence does not preserve the source read-only invariant")

    calysto_core3_packages = [
        str(value).strip()
        for value in calysto_core3_remigrate.get("packages", [])
    ]
    if calysto_core3_packages != list(CALYSTO_CORE3_PACKAGES):
        fail("Calysto Core3 package list differs from the guarded three")
    if (
        calysto_core3_remigrate.get("package_count")
        != CALYSTO_CORE3_REMIGRATE_COUNT
    ):
        fail("Calysto Core3 remigration package_count is not 3")
    if any(is_exported_animation(package) for package in calysto_core3_packages):
        fail("Calysto Core3 evidence contains ExportedAnimations")
    if not set(calysto_core3_packages).issubset(set(packages)):
        fail("Calysto Core3 package set is not a subset of the bulk cohort")
    if set(calysto_core3_packages).intersection(repair_packages):
        fail("Food and Calysto Core3 overlays overlap")
    if set(calysto_core3_packages).intersection(kawaii_tracked_packages):
        fail("Kawaii and Calysto Core3 overlays overlap")
    if set(calysto_core3_packages).intersection(calysto_packages):
        fail("Calysto editor and Core3 overlays overlap")

    if calysto_core3_remigrate.get("created_package_files") != []:
        fail("Calysto Core3 remigration unexpectedly created target packages")
    if calysto_core3_remigrate.get("removed_package_files") != []:
        fail("Calysto Core3 remigration unexpectedly removed target packages")
    expected_core3_modified_files = [
        package[len("/Game/") :].lower() + ".uasset"
        for package in calysto_core3_packages
    ]
    if sorted(
        calysto_core3_remigrate.get("modified_package_files", [])
    ) != sorted(expected_core3_modified_files):
        fail("Calysto Core3 modified-file set is not the exact guarded three")

    core3_backup_path = os.path.realpath(
        str(calysto_core3_remigrate.get("backup_evidence", ""))
    )
    if (
        not is_under(core3_backup_path, saved_migration_root)
        or not os.path.isfile(core3_backup_path)
        or core3_backup_path.lower()
        == calysto_core3_remigrate_evidence_path.lower()
    ):
        fail("Calysto Core3 backup evidence is absent, out of bounds, or ambiguous")
    if (
        str(calysto_core3_remigrate.get("backup_evidence_sha256", "")).upper()
        != sha256(core3_backup_path)
    ):
        fail("Calysto Core3 backup evidence hash differs")
    with open(core3_backup_path, "r", encoding="utf-8-sig") as handle:
        core3_backup = json.load(handle)
    if core3_backup.get("status") != "EXACT_CALYSTO_CORE3_TARGET_BACKUP_PASS":
        fail("Calysto Core3 backup evidence status is not PASS")
    if (
        os.path.realpath(str(core3_backup.get("target_root", ""))).lower()
        != project_root.lower()
    ):
        fail("Calysto Core3 backup belongs to a different target")
    if (
        core3_backup.get("packages") != calysto_core3_packages
        or core3_backup.get("package_count") != CALYSTO_CORE3_REMIGRATE_COUNT
    ):
        fail("Calysto Core3 backup package cohort differs")
    if core3_backup.get("live_target_bytes_unchanged") is not True:
        fail("Calysto Core3 backup changed live target bytes")
    if core3_backup.get("exported_animations_excluded") is not True:
        fail("Calysto Core3 backup does not exclude ExportedAnimations")
    if core3_backup.get("source_project_touched") is not False:
        fail("Calysto Core3 backup does not preserve source read-only")

    core3_backup_before = core3_backup.get("target_before", {})
    core3_backup_after = core3_backup.get("target_after", {})
    core3_backup_files = core3_backup.get("backup_files", {})
    if set(core3_backup_before) != set(calysto_core3_packages):
        fail("Calysto Core3 backup target_before set is not exact")
    if set(core3_backup_after) != set(calysto_core3_packages):
        fail("Calysto Core3 backup target_after set is not exact")
    if set(core3_backup_files) != set(calysto_core3_packages):
        fail("Calysto Core3 detached backup-file set is not exact")
    core3_backup_root = os.path.realpath(str(core3_backup.get("backup_root", "")))
    if not is_under(core3_backup_root, saved_migration_root):
        fail("Calysto Core3 detached backup root escapes Saved/Migration")
    validated_core3_origin = {}
    for package in calysto_core3_packages:
        path = package_file(content_root, package)
        migration_snapshot = validate_recorded_snapshot(
            migration_by_package[package], path, package + " migration snapshot"
        )
        backup_before_snapshot = validate_recorded_snapshot(
            core3_backup_before[package], path, package + " Core3 backup before"
        )
        backup_after_snapshot = validate_recorded_snapshot(
            core3_backup_after[package], path, package + " Core3 backup after"
        )
        if (
            backup_before_snapshot["length"] != migration_snapshot["length"]
            or backup_before_snapshot["sha256"] != migration_snapshot["sha256"]
            or backup_after_snapshot != backup_before_snapshot
        ):
            fail("Calysto Core3 backup does not chain from bulk bytes: " + package)
        detached_record = core3_backup_files[package]
        detached_path = os.path.realpath(str(detached_record.get("file", "")))
        if not is_under(detached_path, core3_backup_root):
            fail("Calysto Core3 detached backup file escapes its root: " + package)
        detached_snapshot = validate_recorded_snapshot(
            detached_record, detached_path, package + " Core3 detached backup"
        )
        detached_live_snapshot = file_snapshot(detached_path)
        if (
            detached_live_snapshot is None
            or detached_live_snapshot["length"] != migration_snapshot["length"]
            or detached_live_snapshot["sha256"] != migration_snapshot["sha256"]
            or detached_snapshot["length"] != migration_snapshot["length"]
            or detached_snapshot["sha256"] != migration_snapshot["sha256"]
        ):
            fail("Calysto Core3 detached backup bytes differ: " + package)
        validated_core3_origin[package] = migration_snapshot

    core3_before = calysto_core3_remigrate.get("target_before", {})
    core3_after = calysto_core3_remigrate.get("target_after", {})
    if set(core3_before) != set(calysto_core3_packages):
        fail("Calysto Core3 target_before snapshot set is not exact")
    if set(core3_after) != set(calysto_core3_packages):
        fail("Calysto Core3 target_after snapshot set is not exact")
    validated_core3_before = {}
    target_matches_backup_before_run = {}
    for package in calysto_core3_packages:
        path = package_file(content_root, package)
        before_snapshot = validate_recorded_snapshot(
            core3_before[package], path, package + " Core3 target_before"
        )
        after_snapshot = validate_recorded_snapshot(
            core3_after[package], path, package + " Core3 target_after"
        )
        validated_core3_before[package] = before_snapshot
        validated_calysto_core3_after[package] = after_snapshot
        target_matches_backup_before_run[package] = (
            before_snapshot["length"] == validated_core3_origin[package]["length"]
            and before_snapshot["sha256"]
            == validated_core3_origin[package]["sha256"]
        )
    if (
        calysto_core3_remigrate.get("target_matches_backup_before_run")
        != target_matches_backup_before_run
    ):
        fail("Calysto Core3 retry preflight comparison map differs")
    if calysto_core3_remigrate.get("all_targets_matched_backup_before_run") is not all(
        target_matches_backup_before_run.values()
    ):
        fail("Calysto Core3 retry preflight aggregate differs")

    calysto_core3_changed_packages = [
        package
        for package in calysto_core3_packages
        if (
            validated_core3_before[package]["length"]
            != validated_calysto_core3_after[package]["length"]
            or validated_core3_before[package]["sha256"]
            != validated_calysto_core3_after[package]["sha256"]
        )
    ]
    calysto_core3_unchanged_rewritten_packages = [
        package
        for package in calysto_core3_packages
        if package not in set(calysto_core3_changed_packages)
    ]
    if (
        calysto_core3_remigrate.get("byte_changed_packages")
        != calysto_core3_changed_packages
        or calysto_core3_remigrate.get("byte_unchanged_rewritten_packages")
        != calysto_core3_unchanged_rewritten_packages
    ):
        fail("Calysto Core3 byte-change classification differs from snapshots")
    calysto_core3_retry_preexisting_changed_packages = [
        package
        for package in calysto_core3_packages
        if not target_matches_backup_before_run[package]
    ]

calysto_smart_scatter_repair = None
calysto_smart_scatter_packages = []
validated_calysto_smart_scatter_after = {}
calysto_smart_scatter_changed_packages = []
if calysto_smart_scatter_repair_evidence_path:
    with open(
        calysto_smart_scatter_repair_evidence_path, "r", encoding="utf-8-sig"
    ) as handle:
        calysto_smart_scatter_repair = json.load(handle)
    if (
        calysto_smart_scatter_repair.get("status")
        != "UE58_CALYSTO_SMART_SCATTER_REPAIR_PASS"
    ):
        fail("Calysto Smart Scatter repair evidence status is not PASS")
    if (
        os.path.realpath(
            str(calysto_smart_scatter_repair.get("project", ""))
        ).lower()
        != project_file.lower()
        or os.path.realpath(
            str(calysto_smart_scatter_repair.get("target_root", ""))
        ).lower()
        != project_root.lower()
    ):
        fail("Calysto Smart Scatter repair belongs to a different target")
    for field in ("target_delta_exact", "source_hashes_unchanged"):
        if calysto_smart_scatter_repair.get(field) is not True:
            fail("Calysto Smart Scatter invariant is not PASS: " + field)
    if calysto_smart_scatter_repair.get("exported_animations_excluded") is not True:
        fail("Calysto Smart Scatter repair does not exclude ExportedAnimations")
    if calysto_smart_scatter_repair.get("runtime_marketplace_dependency_added") is not False:
        fail("Calysto Smart Scatter repair added a runtime Marketplace dependency")

    calysto_smart_scatter_packages = [
        str(value).strip()
        for value in calysto_smart_scatter_repair.get("packages", [])
    ]
    if calysto_smart_scatter_packages != list(CALYSTO_SMART_SCATTER_PACKAGES):
        fail("Calysto Smart Scatter package list differs from the guarded two")
    if (
        calysto_smart_scatter_repair.get("package_count")
        != CALYSTO_SMART_SCATTER_REPAIR_COUNT
        or calysto_smart_scatter_repair.get("saved_package_count")
        != CALYSTO_SMART_SCATTER_REPAIR_COUNT
    ):
        fail("Calysto Smart Scatter repair package/save count is not 2")
    if any(
        is_exported_animation(package)
        for package in calysto_smart_scatter_packages
    ):
        fail("Calysto Smart Scatter evidence contains ExportedAnimations")
    if not set(calysto_smart_scatter_packages).issubset(set(packages)):
        fail("Calysto Smart Scatter set is not a subset of the bulk cohort")
    if set(calysto_smart_scatter_packages).intersection(repair_packages):
        fail("Food and Calysto Smart Scatter overlays overlap")
    if set(calysto_smart_scatter_packages).intersection(kawaii_tracked_packages):
        fail("Kawaii and Calysto Smart Scatter overlays overlap")
    if set(calysto_smart_scatter_packages).intersection(calysto_packages):
        fail("Calysto editor and Smart Scatter overlays overlap")
    expected_core3_smart_overlap = (
        {"/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto"}
        if calysto_core3_remigrate_evidence_path
        else set()
    )
    if (
        set(calysto_smart_scatter_packages).intersection(calysto_core3_packages)
        != expected_core3_smart_overlap
    ):
        fail("Calysto Core3/Smart Scatter overlap differs from the exact PDA chain")

    expected_save_operations = [
        {"package": package, "saved": True}
        for package in calysto_smart_scatter_packages
    ]
    if calysto_smart_scatter_repair.get("save_operations") != expected_save_operations:
        fail("Calysto Smart Scatter save set differs from the exact guarded two")
    expected_smart_delta_files = sorted(
        package[len("/Game/") :].lower() + ".uasset"
        for package in calysto_smart_scatter_packages
    )
    smart_content_delta = calysto_smart_scatter_repair.get("content_delta", [])
    actual_smart_delta_files = sorted(
        str(row.get("relative", "")).replace("\\", "/").lower()
        for row in smart_content_delta
    )
    if actual_smart_delta_files != expected_smart_delta_files:
        fail("Calysto Smart Scatter Content delta is not the exact guarded two")

    smart_backup_path = os.path.realpath(
        str(calysto_smart_scatter_repair.get("backup_evidence", ""))
    )
    if (
        not is_under(smart_backup_path, saved_migration_root)
        or not os.path.isfile(smart_backup_path)
        or smart_backup_path.lower()
        == calysto_smart_scatter_repair_evidence_path.lower()
    ):
        fail("Calysto Smart Scatter backup is absent, out of bounds, or ambiguous")
    if (
        str(
            calysto_smart_scatter_repair.get("backup_evidence_sha256", "")
        ).upper()
        != sha256(smart_backup_path)
    ):
        fail("Calysto Smart Scatter backup evidence hash differs")
    with open(smart_backup_path, "r", encoding="utf-8-sig") as handle:
        smart_backup = json.load(handle)
    if smart_backup.get("status") != "CALYSTO_SMART_SCATTER_PRE_REPAIR_BACKUP_PASS":
        fail("Calysto Smart Scatter backup evidence status is not PASS")
    if (
        os.path.realpath(str(smart_backup.get("target_root", ""))).lower()
        != project_root.lower()
        or smart_backup.get("package_count")
        != CALYSTO_SMART_SCATTER_REPAIR_COUNT
    ):
        fail("Calysto Smart Scatter backup belongs to a different target/cohort")
    for field in (
        "source_hashes_unchanged",
        "backup_hashes_match_target",
        "exported_animations_excluded",
    ):
        if smart_backup.get(field) is not True:
            fail("Calysto Smart Scatter backup invariant is not PASS: " + field)

    smart_before = calysto_smart_scatter_repair.get("target_before", {})
    smart_after = calysto_smart_scatter_repair.get("target_after", {})
    if set(smart_before) != set(calysto_smart_scatter_packages):
        fail("Calysto Smart Scatter before-snapshot set is not exact")
    if set(smart_after) != set(calysto_smart_scatter_packages):
        fail("Calysto Smart Scatter after-snapshot set is not exact")
    smart_backup_relative_files = [
        os.path.normpath(
            os.path.join(
                "Content",
                package[len("/Game/") :].replace("/", os.sep) + ".uasset",
            )
        )
        for package in calysto_smart_scatter_packages
    ]
    if [
        os.path.normpath(str(value))
        for value in smart_backup.get("relative_files", [])
    ] != smart_backup_relative_files:
        fail("Calysto Smart Scatter backup relative-file cohort differs")
    smart_backup_before = smart_backup.get("target_before", {})
    smart_backup_after = smart_backup.get("backup_after", {})
    if set(smart_backup_before) != set(smart_backup_relative_files):
        fail("Calysto Smart Scatter backup target-before set is not exact")
    if set(smart_backup_after) != set(smart_backup_relative_files):
        fail("Calysto Smart Scatter detached backup set is not exact")

    for package, relative_file in zip(
        calysto_smart_scatter_packages, smart_backup_relative_files
    ):
        path = package_file(content_root, package)
        if package in validated_calysto_core3_after:
            expected_origin = validated_calysto_core3_after[package]
            expected_origin_label = "Core3 target_after"
        else:
            expected_origin = validate_recorded_snapshot(
                migration_by_package[package], path, package + " migration snapshot"
            )
            expected_origin_label = "bulk migration"
        before_snapshot = validate_recorded_snapshot(
            smart_before[package], path, package + " Smart Scatter target_before"
        )
        after_snapshot = validate_recorded_snapshot(
            smart_after[package], path, package + " Smart Scatter target_after"
        )
        backup_before_snapshot = validate_recorded_snapshot(
            smart_backup_before[relative_file],
            path,
            package + " Smart Scatter backup target_before",
        )
        if (
            before_snapshot["length"] != expected_origin["length"]
            or before_snapshot["sha256"] != expected_origin["sha256"]
            or backup_before_snapshot != before_snapshot
        ):
            fail(
                "Calysto Smart Scatter does not chain from {}: {}".format(
                    expected_origin_label, package
                )
            )
        detached_record = smart_backup_after[relative_file]
        detached_path = os.path.realpath(str(detached_record.get("file", "")))
        if not is_under(detached_path, os.path.dirname(smart_backup_path)):
            fail("Calysto Smart Scatter detached backup escapes evidence root: " + package)
        detached_snapshot = validate_recorded_snapshot(
            detached_record,
            detached_path,
            package + " Smart Scatter detached backup",
        )
        detached_live_snapshot = file_snapshot(detached_path)
        if (
            detached_live_snapshot is None
            or detached_snapshot["length"] != before_snapshot["length"]
            or detached_snapshot["sha256"] != before_snapshot["sha256"]
            or detached_live_snapshot["length"] != before_snapshot["length"]
            or detached_live_snapshot["sha256"] != before_snapshot["sha256"]
        ):
            fail("Calysto Smart Scatter detached backup bytes differ: " + package)
        if (
            after_snapshot["length"] == before_snapshot["length"]
            and after_snapshot["sha256"] == before_snapshot["sha256"]
        ):
            fail("Calysto Smart Scatter repair did not change bytes: " + package)
        validated_calysto_smart_scatter_after[package] = after_snapshot
        calysto_smart_scatter_changed_packages.append(package)

null_parent_gameplay_repair = None
null_parent_gameplay_packages = []
validated_null_parent_after = {}
null_parent_changed_packages = []
if null_parent_gameplay_repair_evidence_path:
    with open(
        null_parent_gameplay_repair_evidence_path, "r", encoding="utf-8-sig"
    ) as handle:
        null_parent_gameplay_repair = json.load(handle)
    if (
        null_parent_gameplay_repair.get("status")
        != "UE58_NULL_PARENT_GAMEPLAY_BLUEPRINT_REPAIR_PASS"
    ):
        fail("Null-parent gameplay repair evidence status is not PASS")
    if (
        os.path.realpath(
            str(null_parent_gameplay_repair.get("project", ""))
        ).lower()
        != project_file.lower()
    ):
        fail("Null-parent gameplay repair belongs to a different target")
    for field in ("saved_only_selected_packages", "override_event_links_preserved"):
        if null_parent_gameplay_repair.get(field) is not True:
            fail("Null-parent gameplay repair invariant is not PASS: " + field)
    if null_parent_gameplay_repair.get("exported_animations_excluded") is not True:
        fail("Null-parent gameplay repair does not exclude ExportedAnimations")
    if null_parent_gameplay_repair.get("raw_asset_file_edits") != []:
        fail("Null-parent gameplay repair reports raw asset-file edits")
    if null_parent_gameplay_repair.get("marketplace_or_engine_plugin_edits") != []:
        fail("Null-parent gameplay repair reports protected plugin edits")

    null_parent_gameplay_packages = [
        str(value).strip()
        for value in null_parent_gameplay_repair.get("packages", [])
    ]
    if null_parent_gameplay_packages != list(NULL_PARENT_GAMEPLAY_PACKAGES):
        fail("Null-parent gameplay package list differs from the guarded two")
    if (
        null_parent_gameplay_repair.get("package_count")
        != NULL_PARENT_GAMEPLAY_REPAIR_COUNT
        or null_parent_gameplay_repair.get("save_operations")
        != null_parent_gameplay_packages
    ):
        fail("Null-parent gameplay repair package/save set is not exact")
    if any(
        is_exported_animation(package) for package in null_parent_gameplay_packages
    ):
        fail("Null-parent gameplay evidence contains ExportedAnimations")
    if not set(null_parent_gameplay_packages).issubset(set(packages)):
        fail("Null-parent gameplay package set is not a subset of the bulk cohort")
    prior_overlay_packages = (
        set(repair_packages)
        | set(kawaii_tracked_packages)
        | set(calysto_packages)
        | set(calysto_core3_packages)
        | set(calysto_smart_scatter_packages)
    )
    if set(null_parent_gameplay_packages).intersection(prior_overlay_packages):
        fail("Null-parent gameplay overlay overlaps another repair overlay")

    recorded_migration_path = os.path.realpath(
        str(null_parent_gameplay_repair.get("migration_evidence", ""))
    )
    if (
        recorded_migration_path.lower() != migration_evidence_path.lower()
        or str(
            null_parent_gameplay_repair.get("migration_evidence_sha256", "")
        ).upper()
        != sha256(migration_evidence_path)
    ):
        fail("Null-parent gameplay repair migration evidence link differs")
    null_parent_backup_path = os.path.realpath(
        str(null_parent_gameplay_repair.get("backup_evidence", ""))
    )
    if (
        not is_under(null_parent_backup_path, saved_migration_root)
        or not os.path.isfile(null_parent_backup_path)
        or null_parent_backup_path.lower()
        == null_parent_gameplay_repair_evidence_path.lower()
    ):
        fail("Null-parent backup evidence is absent, out of bounds, or ambiguous")
    if (
        str(
            null_parent_gameplay_repair.get("backup_evidence_sha256", "")
        ).upper()
        != sha256(null_parent_backup_path)
    ):
        fail("Null-parent backup evidence hash differs")
    with open(null_parent_backup_path, "r", encoding="utf-8-sig") as handle:
        null_parent_backup = json.load(handle)
    if null_parent_backup.get("status") != "NULL_PARENT_GAMEPLAY_BLUEPRINT_BACKUP_PASS":
        fail("Null-parent backup evidence status is not PASS")
    if (
        os.path.realpath(str(null_parent_backup.get("project", ""))).lower()
        != project_file.lower()
    ):
        fail("Null-parent backup belongs to a different target")
    if (
        null_parent_backup.get("packages") != null_parent_gameplay_packages
        or null_parent_backup.get("package_count")
        != NULL_PARENT_GAMEPLAY_REPAIR_COUNT
    ):
        fail("Null-parent backup package cohort differs")
    if null_parent_backup.get("exported_animations_excluded") is not True:
        fail("Null-parent backup does not exclude ExportedAnimations")
    if null_parent_backup.get("source_asset_write_operations") != []:
        fail("Null-parent backup reports source asset writes")

    backup_rows = {
        str(row.get("package", "")): row
        for row in null_parent_backup.get("files", [])
    }
    if set(backup_rows) != set(null_parent_gameplay_packages):
        fail("Null-parent backup file-row set is not exact")
    null_parent_before = null_parent_gameplay_repair.get(
        "package_files_before", {}
    )
    null_parent_after = null_parent_gameplay_repair.get("package_files_after", {})
    if set(null_parent_before) != set(null_parent_gameplay_packages):
        fail("Null-parent repair before-snapshot set is not exact")
    if set(null_parent_after) != set(null_parent_gameplay_packages):
        fail("Null-parent repair after-snapshot set is not exact")
    for package in null_parent_gameplay_packages:
        path = package_file(content_root, package)
        migration_snapshot = validate_recorded_snapshot(
            migration_by_package[package], path, package + " migration snapshot"
        )
        backup_row = backup_rows[package]
        if backup_row.get("bytes_match") is not True:
            fail("Null-parent detached backup byte equality is not PASS: " + package)
        backup_source = validate_recorded_snapshot(
            backup_row.get("source"), path, package + " null-parent backup source"
        )
        detached_record = backup_row.get("backup", {})
        detached_path = os.path.realpath(str(detached_record.get("file", "")))
        backup_root = os.path.realpath(str(null_parent_backup.get("backup_root", "")))
        if not is_under(backup_root, saved_migration_root) or not is_under(
            detached_path, backup_root
        ):
            fail("Null-parent detached backup path is out of bounds: " + package)
        detached_snapshot = validate_recorded_snapshot(
            detached_record, detached_path, package + " null-parent detached backup"
        )
        detached_live_snapshot = file_snapshot(detached_path)
        before_snapshot = validate_recorded_snapshot(
            null_parent_before[package], path, package + " null-parent repair before"
        )
        after_snapshot = validate_recorded_snapshot(
            null_parent_after[package], path, package + " null-parent repair after"
        )
        for origin_snapshot in (backup_source, detached_snapshot, before_snapshot):
            if (
                origin_snapshot["length"] != migration_snapshot["length"]
                or origin_snapshot["sha256"] != migration_snapshot["sha256"]
            ):
                fail("Null-parent repair does not chain from bulk bytes: " + package)
        if (
            detached_live_snapshot is None
            or detached_live_snapshot["length"] != migration_snapshot["length"]
            or detached_live_snapshot["sha256"] != migration_snapshot["sha256"]
        ):
            fail("Null-parent detached backup bytes differ: " + package)
        if (
            after_snapshot["length"] == before_snapshot["length"]
            and after_snapshot["sha256"] == before_snapshot["sha256"]
        ):
            fail("Null-parent repair did not change package bytes: " + package)
        validated_null_parent_after[package] = after_snapshot
    null_parent_changed_packages = list(null_parent_gameplay_packages)

    null_parent_repair_rows = {
        str(row.get("package", "")): row
        for row in null_parent_gameplay_repair.get("repairs", [])
    }
    if set(null_parent_repair_rows) != set(null_parent_gameplay_packages):
        fail("Null-parent repair result-row set is not exact")
    for package in null_parent_gameplay_packages:
        row = null_parent_repair_rows[package]
        for field in (
            "override_event_links_preserved",
            "project_editor_helper_repaired",
            "existing_subsystem_editor_library_configured",
            "project_editor_helper_validated",
            "generated_class_is_child_of_target_parent",
        ):
            if row.get(field) is not True:
                fail("Null-parent repair row invariant is not PASS: {} {}".format(package, field))
        if (
            row.get("parent_before") != ""
            or row.get("parent_after") != row.get("target_parent")
            or row.get("override_event_links_before")
            != row.get("override_event_links_after")
        ):
            fail("Null-parent parent/event-link repair row differs: " + package)

repair_package_set = set(repair_packages)
kawaii_package_set = set(kawaii_tracked_packages)
calysto_package_set = set(calysto_packages)
calysto_core3_package_set = set(calysto_core3_packages)
calysto_smart_scatter_package_set = set(calysto_smart_scatter_packages)
null_parent_gameplay_package_set = set(null_parent_gameplay_packages)
all_overlay_package_set = (
    repair_package_set
    | kawaii_package_set
    | calysto_package_set
    | calysto_core3_package_set
    | calysto_smart_scatter_package_set
    | null_parent_gameplay_package_set
)
expected_snapshots = {}
hash_expectation_sources = {}
for package in packages:
    path = package_file(content_root, package)
    if package in repair_package_set:
        expected_snapshots[package] = validated_repair_after[package]
        hash_expectation_sources[package] = "UE58_FOOD_PICKUP_REPAIR_AFTER"
    elif package in kawaii_package_set:
        expected_snapshots[package] = validated_kawaii_after[package]
        hash_expectation_sources[package] = "UE58_KAWAII_QUINN_REPAIR_AFTER"
    elif package in calysto_package_set:
        expected_snapshots[package] = validated_calysto_after[package]
        hash_expectation_sources[package] = "UE57_CALYSTO_REMIGRATE_TARGET_AFTER"
    elif package in calysto_smart_scatter_package_set:
        expected_snapshots[package] = validated_calysto_smart_scatter_after[package]
        hash_expectation_sources[package] = (
            "UE58_CALYSTO_SMART_SCATTER_REPAIR_AFTER"
        )
    elif package in calysto_core3_package_set:
        expected_snapshots[package] = validated_calysto_core3_after[package]
        hash_expectation_sources[package] = (
            "UE57_CALYSTO_CORE3_REMIGRATE_TARGET_AFTER"
        )
    elif package in null_parent_gameplay_package_set:
        expected_snapshots[package] = validated_null_parent_after[package]
        hash_expectation_sources[package] = (
            "UE58_NULL_PARENT_GAMEPLAY_REPAIR_AFTER"
        )
    else:
        expected_snapshots[package] = validate_recorded_snapshot(
            migration_by_package[package], path, package + " migration snapshot"
        )
        hash_expectation_sources[package] = "UE57_MIGRATION"

hash_expectation_source_counts = {}
for source in hash_expectation_sources.values():
    hash_expectation_source_counts[source] = (
        hash_expectation_source_counts.get(source, 0) + 1
    )
for source in (
    "UE57_MIGRATION",
    "UE58_FOOD_PICKUP_REPAIR_AFTER",
    "UE58_KAWAII_QUINN_REPAIR_AFTER",
    "UE57_CALYSTO_REMIGRATE_TARGET_AFTER",
    "UE57_CALYSTO_CORE3_REMIGRATE_TARGET_AFTER",
    "UE58_CALYSTO_SMART_SCATTER_REPAIR_AFTER",
    "UE58_NULL_PARENT_GAMEPLAY_REPAIR_AFTER",
):
    hash_expectation_source_counts.setdefault(source, 0)

disk_before = {}
preflight_failures = []
for package in packages:
    path = package_file(content_root, package)
    snapshot = file_snapshot(path)
    disk_before[package] = snapshot
    expected = expected_snapshots[package]
    if snapshot is None:
        preflight_failures.append(
            {"package": package, "reason": "TARGET_PACKAGE_FILE_ABSENT"}
        )
    elif (
        snapshot["length"] != expected["length"]
        or snapshot["sha256"] != expected["sha256"]
    ):
        preflight_failures.append(
            {
                "package": package,
                "reason": "TARGET_BYTES_DIFFER_FROM_EXPECTED_POST_REPAIR_STATE",
                "expected_hash_source": hash_expectation_sources[package],
            }
        )

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game"], True)
registry.wait_for_completion()

loaded_results = []
failed_results = list(preflight_failures)
preflight_failed_packages = {row["package"] for row in preflight_failures}
blueprint_compile_results = []
for package in packages:
    expectation_source = hash_expectation_sources[package]
    if package in preflight_failed_packages:
        loaded_results.append(
            {
                "package": package,
                "loaded": False,
                "class": "",
                "registry_classes": [],
                "reason": "PREFLIGHT_FAILED",
                "expected_hash_source": expectation_source,
            }
        )
        continue
    registry_rows = list(
        registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
    )
    registry_classes = sorted({class_name(row) for row in registry_rows})
    if not registry_rows:
        loaded_results.append(
            {
                "package": package,
                "loaded": False,
                "class": "",
                "registry_classes": [],
                "reason": "ASSET_REGISTRY_ROW_ABSENT",
                "expected_hash_source": expectation_source,
            }
        )
        failed_results.append(
            {"package": package, "reason": "ASSET_REGISTRY_ROW_ABSENT"}
        )
        continue
    try:
        asset = unreal.EditorAssetLibrary.load_asset(package)
    except Exception as exc:
        asset = None
        load_error = repr(exc)
    else:
        load_error = ""
    if asset is None:
        loaded_results.append(
            {
                "package": package,
                "loaded": False,
                "class": "",
                "registry_classes": registry_classes,
                "reason": "LOAD_RETURNED_NONE",
                "error": load_error,
                "expected_hash_source": expectation_source,
            }
        )
        failed_results.append(
            {"package": package, "reason": "LOAD_RETURNED_NONE", "error": load_error}
        )
        continue

    actual_class = asset.get_class().get_name()
    loaded_results.append(
        {
            "package": package,
            "loaded": True,
            "class": actual_class,
            "registry_classes": registry_classes,
            "reason": "",
            "expected_hash_source": expectation_source,
        }
    )
    if actual_class == "Blueprint" or actual_class.endswith("Blueprint"):
        compile_row = {
            "package": package,
            "class": actual_class,
            "compile_requested": True,
            "status": "",
            "result": "FAIL",
            "error": "",
        }
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            status, normalized = normalized_blueprint_status(asset)
            compile_row["status"] = status
            if "UPTODATE" in normalized:
                compile_row["result"] = "PASS_UP_TO_DATE"
            else:
                compile_row["error"] = "Blueprint did not reach UP_TO_DATE"
                failed_results.append(
                    {
                        "package": package,
                        "reason": "BLUEPRINT_NOT_UP_TO_DATE",
                        "status": status,
                    }
                )
        except Exception as exc:
            compile_row["error"] = repr(exc)
            failed_results.append(
                {
                    "package": package,
                    "reason": "BLUEPRINT_COMPILE_EXCEPTION",
                    "error": repr(exc),
                }
            )
        blueprint_compile_results.append(compile_row)

disk_after = {
    package: file_snapshot(package_file(content_root, package))
    for package in packages
}
changed_package_files = [
    package for package in packages if disk_after[package] != disk_before[package]
]
for package in changed_package_files:
    failed_results.append(
        {"package": package, "reason": "PACKAGE_FILE_CHANGED_DURING_VALIDATION"}
    )

all_loaded = (
    len(loaded_results) == expected_count
    and all(row["loaded"] for row in loaded_results)
)
blueprints_up_to_date = all(
    row["result"] == "PASS_UP_TO_DATE" for row in blueprint_compile_results
)
passed = (
    all_loaded
    and blueprints_up_to_date
    and not failed_results
    and not changed_package_files
)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_POST_REPAIR_BULK_PROJECT_CONTENT_READ_ONLY_LOAD_COMPILE_PASS"
        if passed
        else "UE58_POST_REPAIR_BULK_PROJECT_CONTENT_READ_ONLY_LOAD_COMPILE_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "migration_evidence": migration_evidence_path,
    "migration_evidence_sha256": sha256(migration_evidence_path),
    "food_pickup_repair_evidence": repair_evidence_path,
    "food_pickup_repair_evidence_sha256": sha256(repair_evidence_path),
    "kawaii_quinn_repair_overlay_enabled": bool(kawaii_repair_evidence_path),
    "kawaii_quinn_repair_evidence": kawaii_repair_evidence_path,
    "kawaii_quinn_repair_evidence_sha256": (
        sha256(kawaii_repair_evidence_path)
        if kawaii_repair_evidence_path
        else ""
    ),
    "calysto_remigrate_overlay_enabled": bool(calysto_remigrate_evidence_path),
    "calysto_remigrate_evidence": calysto_remigrate_evidence_path,
    "calysto_remigrate_evidence_sha256": (
        sha256(calysto_remigrate_evidence_path)
        if calysto_remigrate_evidence_path
        else ""
    ),
    "calysto_core3_remigrate_overlay_enabled": bool(
        calysto_core3_remigrate_evidence_path
    ),
    "calysto_core3_remigrate_evidence": calysto_core3_remigrate_evidence_path,
    "calysto_core3_remigrate_evidence_sha256": (
        sha256(calysto_core3_remigrate_evidence_path)
        if calysto_core3_remigrate_evidence_path
        else ""
    ),
    "calysto_smart_scatter_repair_overlay_enabled": bool(
        calysto_smart_scatter_repair_evidence_path
    ),
    "calysto_smart_scatter_repair_evidence": (
        calysto_smart_scatter_repair_evidence_path
    ),
    "calysto_smart_scatter_repair_evidence_sha256": (
        sha256(calysto_smart_scatter_repair_evidence_path)
        if calysto_smart_scatter_repair_evidence_path
        else ""
    ),
    "null_parent_gameplay_repair_overlay_enabled": bool(
        null_parent_gameplay_repair_evidence_path
    ),
    "null_parent_gameplay_repair_evidence": (
        null_parent_gameplay_repair_evidence_path
    ),
    "null_parent_gameplay_repair_evidence_sha256": (
        sha256(null_parent_gameplay_repair_evidence_path)
        if null_parent_gameplay_repair_evidence_path
        else ""
    ),
    "expected_count": expected_count,
    "package_count": len(packages),
    "untouched_package_count": expected_count - len(all_overlay_package_set),
    "repair_overlay_package_count": len(all_overlay_package_set),
    "food_pickup_overlay_package_count": repair_expected_count,
    "kawaii_quinn_overlay_package_count": len(kawaii_tracked_packages),
    "kawaii_quinn_changed_package_count": len(kawaii_changed_packages),
    "calysto_remigrate_overlay_package_count": len(calysto_packages),
    "calysto_remigrate_changed_package_count": len(calysto_changed_packages),
    "calysto_core3_remigrate_overlay_package_count": len(
        calysto_core3_packages
    ),
    "calysto_core3_remigrate_changed_package_count": len(
        calysto_core3_changed_packages
    ),
    "calysto_core3_retry_preexisting_changed_packages": (
        calysto_core3_retry_preexisting_changed_packages
    ),
    "calysto_smart_scatter_overlay_package_count": len(
        calysto_smart_scatter_packages
    ),
    "calysto_smart_scatter_changed_package_count": len(
        calysto_smart_scatter_changed_packages
    ),
    "null_parent_gameplay_overlay_package_count": len(
        null_parent_gameplay_packages
    ),
    "null_parent_gameplay_changed_package_count": len(
        null_parent_changed_packages
    ),
    "untouched_hash_source": "UE57_MIGRATION",
    "food_pickup_hash_source": "UE58_FOOD_PICKUP_REPAIR_AFTER",
    "kawaii_quinn_hash_source": (
        "UE58_KAWAII_QUINN_REPAIR_AFTER"
        if kawaii_repair_evidence_path
        else ""
    ),
    "calysto_remigrate_hash_source": (
        "UE57_CALYSTO_REMIGRATE_TARGET_AFTER"
        if calysto_remigrate_evidence_path
        else ""
    ),
    "calysto_core3_remigrate_hash_source": (
        "UE57_CALYSTO_CORE3_REMIGRATE_TARGET_AFTER"
        if calysto_core3_remigrate_evidence_path
        else ""
    ),
    "calysto_smart_scatter_hash_source": (
        "UE58_CALYSTO_SMART_SCATTER_REPAIR_AFTER"
        if calysto_smart_scatter_repair_evidence_path
        else ""
    ),
    "null_parent_gameplay_hash_source": (
        "UE58_NULL_PARENT_GAMEPLAY_REPAIR_AFTER"
        if null_parent_gameplay_repair_evidence_path
        else ""
    ),
    "hash_expectation_source_counts": hash_expectation_source_counts,
    "all_overlay_before_snapshots_match_migration": not bool(
        calysto_core3_retry_preexisting_changed_packages
    ),
    "all_overlay_origin_snapshots_match_migration": True,
    "excluded_package_prefixes": [EXPORTED_ANIMATIONS_PREFIX],
    "exported_animations_excluded": True,
    "exported_animations_selected_count": 0,
    "quarantine_excluded": True,
    "loaded_count": sum(1 for row in loaded_results if row["loaded"]),
    "failed_count": len(failed_results),
    "blueprint_count": len(blueprint_compile_results),
    "blueprint_up_to_date_count": sum(
        1
        for row in blueprint_compile_results
        if row["result"] == "PASS_UP_TO_DATE"
    ),
    "all_packages_loaded": all_loaded,
    "all_blueprints_up_to_date": blueprints_up_to_date,
    "loaded": loaded_results,
    "failed": failed_results,
    "blueprint_compile_results": blueprint_compile_results,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_files": changed_package_files,
    "package_files_unchanged": not changed_package_files,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
    "blueprint_compilation_in_memory_only": True,
}
os.makedirs(os.path.dirname(validation_evidence_path), exist_ok=True)
temporary_path = validation_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, validation_evidence_path)

if not passed:
    fail(
        "Read-only post-repair validation failed: loaded={}/{}, "
        "blueprints_up_to_date={}/{}, failures={}, changed_files={}".format(
            sum(1 for row in loaded_results if row["loaded"]),
            expected_count,
            sum(
                1
                for row in blueprint_compile_results
                if row["result"] == "PASS_UP_TO_DATE"
            ),
            len(blueprint_compile_results),
            len(failed_results),
            len(changed_package_files),
        )
    )
unreal.log(
    "CODEX_POST_REPAIR_BULK_PROJECT_CONTENT58_PASS: "
    + validation_evidence_path
)
