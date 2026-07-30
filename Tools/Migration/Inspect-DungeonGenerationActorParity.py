"""Inspect DungeonGeneration actors and dependencies without saving the map."""

import datetime
import hashlib
import json
import os

import unreal


MAP_PACKAGE = "/Game/Procedural/Maps/DungeonGeneration"
OUTPUT_PATH = os.environ["CODEX_DUNGEON_PARITY_OUTPUT"]
EXPECTED_ENGINE_PREFIX = os.environ["CODEX_DUNGEON_PARITY_ENGINE_PREFIX"]


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


def vector(value):
    return {
        "x": float(value.x),
        "y": float(value.y),
        "z": float(value.z),
    }


def rotator(value):
    return {
        "pitch": float(value.pitch),
        "yaw": float(value.yaw),
        "roll": float(value.roll),
    }


def actor_row(actor):
    actor_class = actor.get_class()
    components = []
    try:
        actor_components = actor.get_components_by_class(unreal.ActorComponent)
    except Exception:
        actor_components = []
    for component in actor_components:
        component_class = component.get_class()
        row = {
            "name": component.get_name(),
            "class": component_class.get_name(),
            "class_path": component_class.get_path_name(),
        }
        if isinstance(component, unreal.SceneComponent):
            try:
                row["relative_location"] = vector(
                    component.get_editor_property("relative_location")
                )
                row["relative_rotation"] = rotator(
                    component.get_editor_property("relative_rotation")
                )
                row["relative_scale"] = vector(
                    component.get_editor_property("relative_scale3d")
                )
            except Exception as exc:
                row["relative_transform_probe_error"] = repr(exc)
        components.append(row)
    components.sort(key=lambda item: (item["class_path"], item["name"]))
    return {
        "name": actor.get_name(),
        "label": actor.get_actor_label(),
        "path": actor.get_path_name(),
        "class": actor_class.get_name(),
        "class_path": actor_class.get_path_name(),
        "location": vector(actor.get_actor_location()),
        "rotation": rotator(actor.get_actor_rotation()),
        "scale": vector(actor.get_actor_scale3d()),
        "tags": sorted(str(value) for value in actor.tags),
        "components": components,
    }


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith(EXPECTED_ENGINE_PREFIX):
    raise RuntimeError(
        "Expected engine {} but found {}".format(
            EXPECTED_ENGINE_PREFIX, engine_version
        )
    )

content_root = os.path.realpath(unreal.Paths.project_content_dir())
map_file = os.path.join(content_root, "Procedural", "Maps", "DungeonGeneration.umap")
length_before = os.path.getsize(map_file)
hash_before = sha256(map_file)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Procedural/Maps", "/Game/_Game"], True)
registry.wait_for_completion()
dependencies = sorted(
    str(value)
    for value in registry.get_dependencies(
        MAP_PACKAGE, dependency_options()
    )
)

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PACKAGE)
if world is None:
    raise RuntimeError("DungeonGeneration failed to load")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = sorted(
    (actor_row(actor) for actor in actor_subsystem.get_all_level_actors()),
    key=lambda item: (item["class_path"], item["name"]),
)

dirty_packages = []
try:
    dirty_packages = sorted(
        package.get_name()
        for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    )
except Exception:
    pass

length_after = os.path.getsize(map_file)
hash_after = sha256(map_file)
if length_after != length_before or hash_after != hash_before:
    raise RuntimeError("Read-only inspection changed the map bytes")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "DUNGEON_GENERATION_ACTOR_PARITY_READ_ONLY_PASS",
    "engine_version": engine_version,
    "project": unreal.Paths.get_project_file_path(),
    "map_package": MAP_PACKAGE,
    "map_file": map_file,
    "map_length": length_after,
    "map_sha256": hash_after,
    "dependencies": dependencies,
    "game_dependencies": [
        value for value in dependencies if value.startswith("/Game/")
    ],
    "actor_count": len(actors),
    "actors": actors,
    "dirty_map_packages": dirty_packages,
    "map_save_requested": False,
}

output_path = os.path.realpath(OUTPUT_PATH)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DUNGEON_ACTOR_PARITY_PASS: " + output_path)
