"""Load, resave, reload, and validate DungeonGeneration in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


MAP_PACKAGE = "/Game/Procedural/Maps/DungeonGeneration"
EXPECTED_SOURCE_LENGTH = 58016
EXPECTED_SOURCE_SHA256 = (
    "B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F"
)
EXPECTED_DEPENDENCIES = {
    "/Engine/EngineMaterials/WorldGridMaterial",
    "/Script/NavigationSystem",
    "/Script/PCG",
}
EXPECTED_ACTOR_CLASSES = {"PlayerStart", "NavMeshBoundsVolume", "PCGWorldActor"}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_DUNGEON_GENERATION58_RESAVE_FAIL: " + message)
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


def registry_dependencies(registry):
    combined = sorted(
        {
            str(value)
            for value in registry.get_dependencies(
                MAP_PACKAGE, dependency_options(True, True, True, True)
            )
        }
    )
    hard = sorted(
        {
            str(value)
            for value in registry.get_dependencies(
                MAP_PACKAGE, dependency_options(hard=True)
            )
        }
    )
    soft = sorted(
        {
            str(value)
            for value in registry.get_dependencies(
                MAP_PACKAGE, dependency_options(soft=True)
            )
        }
    )
    return combined, hard, soft


def dirty_map_packages():
    try:
        packages = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    except Exception as exc:
        return [], repr(exc)
    names = []
    for package in packages:
        try:
            names.append(package.get_name())
        except Exception:
            names.append(str(package))
    return sorted(set(names)), ""


def inspect_world(world):
    if world is None:
        fail("Loaded World is None")
    world_path = world.get_path_name()
    if not world_path.startswith(MAP_PACKAGE + "."):
        fail("Loaded World path differs: " + world_path)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actor_subsystem.get_all_level_actors()
    actor_classes = sorted(actor.get_class().get_name() for actor in actors)
    missing = sorted(EXPECTED_ACTOR_CLASSES - set(actor_classes))
    if missing:
        fail("Expected map actors are absent: " + repr(missing))
    streaming_levels = []
    try:
        streaming_levels = [str(value) for value in world.get_streaming_levels()]
    except Exception:
        streaming_levels = []
    if streaming_levels:
        fail("Unexpected streaming levels: " + repr(streaming_levels))
    partitioned = False
    partition_probes = []
    try:
        partitioned = world.get_editor_property("world_partition") is not None
        partition_probes.append("world.world_partition")
    except Exception as exc:
        partition_probes.append("world.world_partition_unavailable=" + repr(exc))
        try:
            partitioned = world.get_world_partition() is not None
            partition_probes.append("world.get_world_partition")
        except Exception as inner:
            partition_probes.append(
                "world.get_world_partition_unavailable=" + repr(inner)
            )
    if partitioned:
        fail("Map reports World Partition but the exact batch has no external packages")
    return {
        "world_path": world_path,
        "actor_count": len(actors),
        "actor_classes": actor_classes,
        "required_actor_classes": sorted(EXPECTED_ACTOR_CLASSES),
        "missing_required_actor_classes": [],
        "streaming_levels": streaming_levels,
        "world_partitioned": False,
        "world_partition_probes": partition_probes,
    }


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
if content_root.lower() != os.path.realpath(
    os.path.join(project_root, "Content")
).lower():
    fail("Project Content invariant failed")

phase_root = os.path.join(
    project_root, "Saved", "Migration", "Phase4", "DungeonGeneration"
)
migration_evidence_path = os.path.join(
    phase_root, "DungeonGeneration57Migration.json"
)
post_migration_gate_path = os.path.join(
    phase_root, "Gates", "POST_MIGRATION57_SafetyGate.json"
)
evidence_path = os.path.join(phase_root, "DungeonGeneration58Resave.json")
target_file = os.path.realpath(
    os.path.join(content_root, "Procedural", "Maps", "DungeonGeneration.umap")
)
expected_relative = "procedural/maps/dungeongeneration.umap"

for required in (migration_evidence_path, post_migration_gate_path, target_file):
    if not os.path.isfile(required):
        fail("Required migration input is absent: " + required)
with open(migration_evidence_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
with open(post_migration_gate_path, "r", encoding="utf-8-sig") as handle:
    post_migration_gate = json.load(handle)
if (
    migration.get("status")
    != "ASSETTOOLS_EXACT_DUNGEON_GENERATION_MIGRATION_PASS"
    or migration.get("package_count") != 1
    or migration.get("package", {}).get("package") != MAP_PACKAGE
    or migration.get("package", {}).get("source_length") != EXPECTED_SOURCE_LENGTH
    or migration.get("package", {}).get("source_sha256") != EXPECTED_SOURCE_SHA256
    or not isinstance(migration.get("package", {}).get("bytes_match_source"), bool)
    or migration.get("target_delta_exact") is not True
    or migration.get("created_package_files") != [expected_relative]
    or migration.get("removed_package_files")
    or migration.get("modified_existing_package_files")
):
    fail("UE 5.7 AssetTools migration evidence does not match the exact batch")
if (
    post_migration_gate.get("status")
    != "DUNGEON_GENERATION_SOURCE_PROTECTED_SAFETY_PASS"
    or post_migration_gate.get("stage") != "POST_MIGRATION57"
):
    fail("POST_MIGRATION57 source/protected gate is not PASS")

migration_row = migration["package"]
if os.path.realpath(migration_row.get("file", "")).lower() != target_file.lower():
    fail("Migration evidence names a different target file")
if (
    os.path.getsize(target_file) != migration_row.get("length")
    or sha256(target_file) != migration_row.get("sha256")
):
    fail("Target map differs from UE 5.7 migration evidence before resave")

for forbidden in (
    os.path.splitext(target_file)[0] + ".uexp",
    os.path.splitext(target_file)[0] + ".ubulk",
    os.path.splitext(target_file)[0] + ".uptnl",
    os.path.join(content_root, "Procedural", "Maps", "DungeonGeneration_BuiltData.uasset"),
    os.path.join(
        content_root,
        "__ExternalActors__",
        "Procedural",
        "Maps",
        "DungeonGeneration",
    ),
    os.path.join(
        content_root,
        "__ExternalObjects__",
        "Procedural",
        "Maps",
        "DungeonGeneration",
    ),
):
    if os.path.exists(forbidden):
        fail("Unexpected pre-resave sidecar or external package exists: " + forbidden)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Procedural/Maps"], True)
registry.wait_for_completion()
dependencies_before, hard_before, soft_before = registry_dependencies(registry)
if set(dependencies_before) != EXPECTED_DEPENDENCIES:
    fail("UE 5.8 pre-resave dependency set differs: " + repr(dependencies_before))
if set(hard_before) != EXPECTED_DEPENDENCIES or soft_before:
    fail("UE 5.8 pre-resave hard/soft dependency classification differs")

inventory_before = snapshot_package_files(content_root)
fingerprint_before = snapshot_fingerprint(inventory_before)
sha_before = sha256(target_file)
length_before = os.path.getsize(target_file)
dirty_before, dirty_before_error = dirty_map_packages()

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PACKAGE)
loaded_before_save = inspect_world(world)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PACKAGE):
    fail("UE 5.8 SaveMap returned false")

inventory_after_save = snapshot_package_files(content_root)
fingerprint_after_save = snapshot_fingerprint(inventory_after_save)
created = sorted(set(inventory_after_save) - set(inventory_before))
removed = sorted(set(inventory_before) - set(inventory_after_save))
modified = sorted(
    name
    for name in set(inventory_before).intersection(inventory_after_save)
    if inventory_before[name] != inventory_after_save[name]
)
if created or removed or modified != [expected_relative]:
    fail(
        "UE 5.8 resave target delta is not exactly the map; created={!r}, "
        "removed={!r}, modified={!r}".format(created, removed, modified)
    )

length_after = os.path.getsize(target_file)
sha_after = sha256(target_file)
if not length_after or not sha_after:
    fail("UE 5.8 resave produced an empty or unhashed map")
for forbidden in (
    os.path.splitext(target_file)[0] + ".uexp",
    os.path.splitext(target_file)[0] + ".ubulk",
    os.path.splitext(target_file)[0] + ".uptnl",
    os.path.join(content_root, "Procedural", "Maps", "DungeonGeneration_BuiltData.uasset"),
    os.path.join(
        content_root,
        "__ExternalActors__",
        "Procedural",
        "Maps",
        "DungeonGeneration",
    ),
    os.path.join(
        content_root,
        "__ExternalObjects__",
        "Procedural",
        "Maps",
        "DungeonGeneration",
    ),
):
    if os.path.exists(forbidden):
        fail("UE 5.8 resave created an unexpected sidecar or external package")

try:
    registry.scan_modified_asset_files([target_file])
except Exception:
    registry.scan_paths_synchronous(["/Game/Procedural/Maps"], True)
registry.wait_for_completion()
dependencies_after, hard_after, soft_after = registry_dependencies(registry)
if set(dependencies_after) != EXPECTED_DEPENDENCIES:
    fail("UE 5.8 post-resave dependency set differs: " + repr(dependencies_after))
if set(hard_after) != EXPECTED_DEPENDENCIES or soft_after:
    fail("UE 5.8 post-resave hard/soft dependency classification differs")

world_reloaded = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PACKAGE)
loaded_after_reload = inspect_world(world_reloaded)
if sha256(target_file) != sha_after or os.path.getsize(target_file) != length_after:
    fail("Reload changed the resaved map on disk")
inventory_after_reload = snapshot_package_files(content_root)
if inventory_after_reload != inventory_after_save:
    fail("Reload changed target Content package inventory")
dirty_after, dirty_after_error = dirty_map_packages()
if MAP_PACKAGE in dirty_after:
    fail("Resaved map remains dirty after reload")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_DUNGEON_GENERATION_LOAD_RESAVE_RELOAD_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "package": MAP_PACKAGE,
    "target_file": target_file,
    "length_before_resave": length_before,
    "sha256_before_resave": sha_before,
    "length_after_resave": length_after,
    "sha256_after_resave": sha_after,
    "source_length": EXPECTED_SOURCE_LENGTH,
    "source_sha256": EXPECTED_SOURCE_SHA256,
    "dependencies_before_resave": dependencies_before,
    "dependencies_after_resave": dependencies_after,
    "hard_dependencies_after_resave": hard_after,
    "soft_dependencies_after_resave": soft_after,
    "game_dependencies": [],
    "loaded_before_save": loaded_before_save,
    "loaded_after_reload": loaded_after_reload,
    "dirty_map_packages_before_load": dirty_before,
    "dirty_map_packages_after_reload": dirty_after,
    "dirty_map_probe_error_before": dirty_before_error,
    "dirty_map_probe_error_after": dirty_after_error,
    "map_load_requested": True,
    "map_save_requested": True,
    "map_reload_requested": True,
    "target_inventory_algorithm": "lowercase relative package path|length|mtime_ns",
    "target_package_file_count_before": len(inventory_before),
    "target_package_file_count_after": len(inventory_after_save),
    "target_inventory_fingerprint_before": fingerprint_before,
    "target_inventory_fingerprint_after": fingerprint_after_save,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_package_files": modified,
    "target_delta_exact": True,
    "sidecars_or_external_packages_created": [],
    "post_migration_source_protected_gate": "PASS",
    "post_resave_source_protected_gate": "PENDING_EXTERNAL_GATE",
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DUNGEON_GENERATION58_RESAVE_PASS: " + evidence_path)
