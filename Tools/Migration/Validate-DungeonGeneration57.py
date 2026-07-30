"""Load the isolated DungeonGeneration map in UE 5.7 without saving it."""

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
EXPECTED_ACTOR_CLASSES = {"PlayerStart", "NavMeshBoundsVolume", "PCGWorldActor"}

RECEIPT_PATH = os.environ.get("CODEX_DUNGEON_MAP_RECEIPT", "").strip()
OUTPUT_PATH = os.environ.get(
    "CODEX_DUNGEON_MAP_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_DUNGEON_GENERATION57_VALIDATION_FAIL: " + message)
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


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def object_path(asset_data):
    try:
        return str(asset_data.get_soft_object_path())
    except Exception:
        return "{}.{}".format(asset_data.package_name, asset_data.asset_name)


def dependency_options(hard=False, soft=False, hard_manage=False, soft_manage=False):
    options = unreal.AssetRegistryDependencyOptions()
    values = {
        "include_hard_package_references": hard,
        "include_soft_package_references": soft,
        "include_hard_management_references": hard_manage,
        "include_soft_management_references": soft_manage,
        "include_searchable_names": False,
    }
    for property_name, value in values.items():
        try:
            options.set_editor_property(property_name, value)
        except Exception:
            pass
    return options


def dirty_map_packages():
    try:
        packages = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    except Exception as exc:
        return [], repr(exc)
    result = []
    for package in packages:
        try:
            result.append(package.get_name())
        except Exception:
            result.append(str(package))
    return sorted(set(result)), ""


def inspect_world_partition(world):
    probes = []
    try:
        value = world.get_editor_property("world_partition")
        probes.append("world.world_partition")
        return value is not None, probes, ""
    except Exception as exc:
        probes.append("world.world_partition_unavailable=" + repr(exc))
    try:
        value = world.get_world_partition()
        probes.append("world.get_world_partition")
        return value is not None, probes, ""
    except Exception as exc:
        probes.append("world.get_world_partition_unavailable=" + repr(exc))
    return False, probes, "UNAVAILABLE_STATIC_AND_FILESYSTEM_GATES_APPLY"


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (RECEIPT_PATH, OUTPUT_PATH, EXPECTED_TARGET_ROOT, EXPECTED_HARNESS_CONTENT)
):
    fail("All DungeonGeneration validation environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_harness_content = os.path.realpath(EXPECTED_HARNESS_CONTENT)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
if harness_content.lower() != expected_harness_content.lower():
    fail("Commandlet is not running in the audited isolated harness")
if not is_under(
    harness_content,
    os.path.join(target_root, "Saved", "Migration", "Phase4", "DungeonGeneration"),
):
    fail("Harness Content escapes the DungeonGeneration Saved/Migration root")
if is_under(harness_content, target_content):
    fail("Harness Content unexpectedly lives under live target Content")
if os.path.basename(unreal.Paths.get_project_file_path()).lower() != (
    "dungeongeneration57harness.uproject"
):
    fail("Unexpected harness project descriptor")

receipt_path = os.path.realpath(RECEIPT_PATH)
if not is_under(
    receipt_path,
    os.path.join(target_root, "Saved", "Migration", "Phase4", "DungeonGeneration"),
):
    fail("Harness receipt escapes the batch evidence root")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if (
    receipt.get("status") != "ISOLATED_DUNGEON_GENERATION57_HARNESS_PASS"
    or receipt.get("package_count") != 1
    or receipt.get("package") != MAP_PACKAGE
):
    fail("Harness receipt does not match the one-map contract")
if os.path.realpath(receipt.get("harness_content", "")).lower() != harness_content.lower():
    fail("Harness Content differs from its receipt")

source_row = receipt.get("source_map", {})
staged_row = receipt.get("staged_map", {})
source_file = os.path.realpath(source_row.get("file", ""))
staged_file = os.path.realpath(staged_row.get("file", ""))
target_file = os.path.realpath(receipt.get("destination_map", ""))
expected_target_file = os.path.realpath(
    os.path.join(target_content, "Procedural", "Maps", "DungeonGeneration.umap")
)
if target_file.lower() != expected_target_file.lower():
    fail("Receipt destination is not the canonical target map path")
if not is_under(staged_file, harness_content):
    fail("Staged map escapes harness Content")
if is_under(source_file, target_root):
    fail("Receipt source map unexpectedly points inside the target")
for label, path, row in (
    ("source", source_file, source_row),
    ("staged", staged_file, staged_row),
):
    if not os.path.isfile(path):
        fail(label + " map is absent")
    if (
        os.path.getsize(path) != EXPECTED_LENGTH
        or sha256(path) != EXPECTED_SHA256
        or row.get("length") != EXPECTED_LENGTH
        or row.get("sha256") != EXPECTED_SHA256
    ):
        fail(label + " map differs from the frozen baseline")
if os.path.exists(target_file):
    fail("Target map collision exists before UE 5.7 validation")

pre_stage = receipt.get("pre_stage_safety", {})
pre_stage_path = os.path.realpath(pre_stage.get("evidence", ""))
if not is_under(pre_stage_path, os.path.join(target_root, "Saved", "Migration")):
    fail("PRE_STAGE safety evidence escapes target Saved/Migration")
if not os.path.isfile(pre_stage_path) or sha256(pre_stage_path) != pre_stage.get(
    "evidence_sha256"
):
    fail("PRE_STAGE safety evidence is absent or changed")
with open(pre_stage_path, "r", encoding="utf-8-sig") as handle:
    pre_stage_payload = json.load(handle)
if (
    pre_stage_payload.get("status")
    != "DUNGEON_GENERATION_SOURCE_PROTECTED_SAFETY_PASS"
    or pre_stage_payload.get("stage") != "PRE_STAGE"
):
    fail("PRE_STAGE source/protected safety gate is not PASS")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Procedural/Maps"], True)
registry.wait_for_completion()
assets = registry.get_assets_by_package_name(
    MAP_PACKAGE, include_only_on_disk_assets=True
)
registered_classes = sorted({class_name(item) for item in assets})
registered_objects = sorted({object_path(item) for item in assets})
if "World" not in registered_classes:
    fail("The staged package does not register a World asset")

all_options = dependency_options(True, True, True, True)
hard_options = dependency_options(hard=True)
soft_options = dependency_options(soft=True)
dependencies = sorted(
    {str(value) for value in registry.get_dependencies(MAP_PACKAGE, all_options)}
)
hard_dependencies = sorted(
    {str(value) for value in registry.get_dependencies(MAP_PACKAGE, hard_options)}
)
soft_dependencies = sorted(
    {str(value) for value in registry.get_dependencies(MAP_PACKAGE, soft_options)}
)
if set(dependencies) != EXPECTED_DEPENDENCIES:
    fail("Combined dependency set differs: " + repr(dependencies))
if set(hard_dependencies) != EXPECTED_DEPENDENCIES:
    fail("Hard dependency set differs: " + repr(hard_dependencies))
if soft_dependencies:
    fail("Unexpected soft dependencies: " + repr(soft_dependencies))
if any(value.startswith("/Game/") for value in dependencies):
    fail("The map unexpectedly depends on project content")
referencers = sorted(
    {str(value) for value in registry.get_referencers(MAP_PACKAGE, all_options)}
)
unexpected_referencers = [
    value for value in referencers if value.startswith("/Game/") and value != MAP_PACKAGE
]
if unexpected_referencers:
    fail("Unexpected package in the isolated harness refers to the map")

dirty_before, dirty_before_error = dirty_map_packages()
staged_hash_before_load = sha256(staged_file)
world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PACKAGE)
if world is None:
    fail("EditorLoadingAndSavingUtils.load_map returned None")
world_path = world.get_path_name()
if not world_path.startswith(MAP_PACKAGE + "."):
    fail("Loaded World path differs: " + world_path)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actor_subsystem.get_all_level_actors()
actor_classes = sorted(actor.get_class().get_name() for actor in actors)
actor_class_set = set(actor_classes)
missing_actor_classes = sorted(EXPECTED_ACTOR_CLASSES - actor_class_set)
if missing_actor_classes:
    fail("Expected map actors are absent: " + repr(missing_actor_classes))

streaming_levels = []
try:
    streaming_levels = [str(value) for value in world.get_streaming_levels()]
except Exception:
    streaming_levels = []
if streaming_levels:
    fail("Unexpected streaming levels: " + repr(streaming_levels))

world_partitioned, world_partition_probes, world_partition_probe_note = (
    inspect_world_partition(world)
)
if world_partitioned:
    fail("Map reports World Partition but the one-package sidecar audit is empty")

dirty_after, dirty_after_error = dirty_map_packages()
if MAP_PACKAGE in dirty_after:
    fail("Read-only UE 5.7 load marked DungeonGeneration dirty")
if os.path.getsize(staged_file) != EXPECTED_LENGTH or sha256(staged_file) != (
    staged_hash_before_load
):
    fail("Staged map bytes changed during read-only load")
if os.path.getsize(source_file) != EXPECTED_LENGTH or sha256(source_file) != (
    EXPECTED_SHA256
):
    fail("Source map bytes changed during read-only load")
if os.path.exists(target_file):
    fail("Target map appeared during read-only validation")

output_path = os.path.realpath(OUTPUT_PATH)
if not is_under(output_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Validation evidence escapes target Saved/Migration")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_DUNGEON_GENERATION_READ_ONLY_LOAD_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "harness_content": harness_content,
    "package": MAP_PACKAGE,
    "source_file": source_file,
    "staged_file": staged_file,
    "source_length": EXPECTED_LENGTH,
    "source_sha256": EXPECTED_SHA256,
    "registered_classes": registered_classes,
    "registered_objects": registered_objects,
    "dependencies": dependencies,
    "hard_dependencies": hard_dependencies,
    "soft_dependencies": soft_dependencies,
    "game_dependencies": [],
    "isolated_harness_referencers": referencers,
    "world_path": world_path,
    "actor_count": len(actors),
    "actor_classes": actor_classes,
    "required_actor_classes": sorted(EXPECTED_ACTOR_CLASSES),
    "missing_required_actor_classes": [],
    "streaming_levels": streaming_levels,
    "world_partitioned": False,
    "world_partition_probes": world_partition_probes,
    "world_partition_probe_note": world_partition_probe_note,
    "dirty_map_packages_before_load": dirty_before,
    "dirty_map_packages_after_load": dirty_after,
    "dirty_map_probe_error_before": dirty_before_error,
    "dirty_map_probe_error_after": dirty_after_error,
    "map_load_requested": True,
    "map_save_requested": False,
    "packages_saved": 0,
    "source_tree_mounted": False,
    "target_content_writes": 0,
    "pre_stage_source_protected_gate": "PASS",
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DUNGEON_GENERATION57_VALIDATION_PASS: " + output_path)
