"""Restore source actor parity for DungeonGeneration under UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


MAP_PACKAGE = "/Game/Procedural/Maps/DungeonGeneration"
OUTPUT_PATH = os.environ["CODEX_DUNGEON_PARITY_REPAIR_OUTPUT"]
EXPECTED_KEEP = {
    ("/Script/Engine.PlayerStart", "PlayerStart_0"),
    ("/Script/NavigationSystem.NavMeshBoundsVolume", "NavMeshBoundsVolume_0"),
    ("/Script/NavigationSystem.RecastNavMesh", "RecastNavMesh-Default"),
    ("/Script/PCG.PCGWorldActor", "PCGWorldActor_0"),
}
EXPECTED_TRANSFORMS = {
    ("/Script/Engine.PlayerStart", "PlayerStart_0"): {
        "location": (1950.0, -4350.0, 130.0),
        "rotation": (0.0, 0.0, 0.0),
        "scale": (1.0, 1.0, 1.0),
    },
    ("/Script/NavigationSystem.NavMeshBoundsVolume", "NavMeshBoundsVolume_0"): {
        "location": (0.0, 0.0, 0.0),
        "rotation": (0.0, 0.0, 0.0),
        "scale": (200.0, 200.0, 1.0),
    },
    ("/Script/NavigationSystem.RecastNavMesh", "RecastNavMesh-Default"): {
        "location": (0.0, 0.0, 0.0),
        "rotation": (0.0, 0.0, 0.0),
        "scale": (1.0, 1.0, 1.0),
    },
    ("/Script/PCG.PCGWorldActor", "PCGWorldActor_0"): {
        "location": (0.0, 0.0, 0.0),
        "rotation": (0.0, 0.0, 0.0),
        "scale": (1.0, 1.0, 1.0),
    },
}


def fail(message):
    unreal.log_error("CODEX_DUNGEON_SOURCE_PARITY_REPAIR_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for name, value in {
        "include_hard_package_references": True,
        "include_soft_package_references": True,
        "include_hard_management_references": True,
        "include_soft_management_references": True,
        "include_searchable_names": False,
    }.items():
        try:
            options.set_editor_property(name, value)
        except Exception:
            pass
    return options


def actor_key(actor):
    return actor.get_class().get_path_name(), actor.get_name()


def actor_transform(actor):
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    return {
        "location": (float(location.x), float(location.y), float(location.z)),
        "rotation": (float(rotation.pitch), float(rotation.yaw), float(rotation.roll)),
        "scale": (float(scale.x), float(scale.y), float(scale.z)),
    }


def near_tuple(actual, expected, tolerance=0.01):
    return all(abs(left - right) <= tolerance for left, right in zip(actual, expected))


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if os.path.basename(unreal.Paths.get_project_file_path()).lower() != (
    "noshellforwinter.uproject"
):
    fail("Repair is not running against NoShellForWinter")

content_root = os.path.realpath(unreal.Paths.project_content_dir())
map_file = os.path.join(content_root, "Procedural", "Maps", "DungeonGeneration.umap")
hash_before = sha256(map_file)
length_before = os.path.getsize(map_file)

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PACKAGE)
if world is None:
    fail("DungeonGeneration failed to load")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors_before = list(actor_subsystem.get_all_level_actors())
keys_before = {actor_key(actor) for actor in actors_before}
missing_keep = sorted(EXPECTED_KEEP - keys_before)
if missing_keep:
    fail("Source baseline actors are missing: " + repr(missing_keep))

for actor in actors_before:
    key = actor_key(actor)
    if key not in EXPECTED_TRANSFORMS:
        continue
    actual = actor_transform(actor)
    expected = EXPECTED_TRANSFORMS[key]
    for field in ("location", "rotation", "scale"):
        if not near_tuple(actual[field], expected[field]):
            fail(
                "Source baseline transform differs for {} {}: {} != {}".format(
                    key, field, actual[field], expected[field]
                )
            )

extra_actors = [
    actor for actor in actors_before if actor_key(actor) not in EXPECTED_KEEP
]
extra_rows = sorted(
    [
        {
            "label": actor.get_actor_label(),
            "name": actor.get_name(),
            "class_path": actor.get_class().get_path_name(),
        }
        for actor in extra_actors
    ],
    key=lambda row: (row["class_path"], row["name"]),
)
the_dungeon = [
    row
    for row in extra_rows
    if row["class_path"] == "/Game/_Game/TheDungeon.TheDungeon_C"
]
if len(the_dungeon) != 1:
    fail(
        "Expected exactly one placed TheDungeon actor before repair, found {}".format(
            len(the_dungeon)
        )
    )

if not actor_subsystem.destroy_actors(extra_actors):
    fail("EditorActorSubsystem.destroy_actors returned false")

actors_after_destroy = list(actor_subsystem.get_all_level_actors())
keys_after_destroy = {actor_key(actor) for actor in actors_after_destroy}
if keys_after_destroy != EXPECTED_KEEP:
    fail(
        "Actor keys after cleanup differ from source: {}".format(
            sorted(keys_after_destroy)
        )
    )

if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PACKAGE):
    fail("UE 5.8 save_map returned false")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
try:
    registry.scan_modified_asset_files([map_file])
except Exception:
    registry.scan_paths_synchronous(["/Game/Procedural/Maps"], True)
registry.wait_for_completion()
dependencies = sorted(
    str(value)
    for value in registry.get_dependencies(
        MAP_PACKAGE, dependency_options()
    )
)
game_dependencies = [
    value for value in dependencies if value.startswith("/Game/")
]
if game_dependencies:
    fail("Game dependencies remain after repair: " + repr(game_dependencies))

world_reloaded = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PACKAGE)
if world_reloaded is None:
    fail("DungeonGeneration failed to reload")
actors_reloaded = list(actor_subsystem.get_all_level_actors())
keys_reloaded = {actor_key(actor) for actor in actors_reloaded}
if keys_reloaded != EXPECTED_KEEP:
    fail(
        "Reloaded actor keys differ from source: {}".format(sorted(keys_reloaded))
    )

dirty_packages = []
try:
    dirty_packages = sorted(
        package.get_name()
        for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    )
except Exception:
    pass
if MAP_PACKAGE in dirty_packages:
    fail("DungeonGeneration remains dirty after save/reload")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_DUNGEON_GENERATION_SOURCE_ACTOR_PARITY_REPAIR_PASS",
    "engine_version": engine_version,
    "project": unreal.Paths.get_project_file_path(),
    "map_package": MAP_PACKAGE,
    "map_file": map_file,
    "map_length_before": length_before,
    "map_sha256_before": hash_before,
    "map_length_after": os.path.getsize(map_file),
    "map_sha256_after": sha256(map_file),
    "actor_count_before": len(actors_before),
    "actor_count_after": len(actors_reloaded),
    "removed_actor_count": len(extra_rows),
    "removed_actors": extra_rows,
    "kept_actor_keys": sorted(
        [
            {"class_path": class_path, "name": name}
            for class_path, name in EXPECTED_KEEP
        ],
        key=lambda row: (row["class_path"], row["name"]),
    ),
    "dependencies_after": dependencies,
    "game_dependencies_after": game_dependencies,
    "dirty_map_packages_after_reload": dirty_packages,
    "map_save_requested": True,
    "source_tree_mounted": False,
    "source_tree_writes": 0,
}

output_path = os.path.realpath(OUTPUT_PATH)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DUNGEON_SOURCE_PARITY_REPAIR_PASS: " + output_path)
