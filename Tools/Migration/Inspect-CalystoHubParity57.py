"""Read-only UE 5.7 inspection of the isolated source HUB harness.

Required environment:
  CODEX_HUB_PARITY_OUTPUT

The script only loads the copied harness map and writes JSON outside Content.
It never saves a package.
"""

import datetime
import hashlib
import json
import os

import unreal


MAP_PACKAGE = "/Game/_Game/Hub/HUB"
OUTPUT = os.path.realpath(os.environ.get("CODEX_HUB_PARITY_OUTPUT", "").strip())


def fail(message):
    unreal.log_error("CODEX_HUB_PARITY57_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def vector(value):
    return {"x": value.x, "y": value.y, "z": value.z}


def rotator(value):
    return {"pitch": value.pitch, "yaw": value.yaw, "roll": value.roll}


def safe_property(instance, name):
    try:
        value = instance.get_editor_property(name)
    except Exception:
        return None
    if isinstance(value, unreal.Name):
        return str(value)
    if isinstance(value, unreal.Text):
        return str(value)
    if isinstance(value, unreal.SoftObjectPath):
        return str(value)
    if isinstance(value, (bool, int, float, str)) or value is None:
        return value
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


if not OUTPUT:
    fail("CODEX_HUB_PARITY_OUTPUT is required")
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "essentialmaps57harness.uproject":
    fail("Expected the isolated EssentialMaps57Harness project")
if not unreal.SystemLibrary.get_engine_version().startswith("5.7."):
    fail("Expected Unreal Engine 5.7")

content_root = os.path.realpath(unreal.Paths.project_content_dir())
map_file = os.path.join(content_root, "_Game", "Hub", "HUB.umap")
if not os.path.isfile(map_file):
    fail("Harness HUB map is absent")
map_before = {"length": os.path.getsize(map_file), "sha256": sha256(map_file)}

world = unreal.EditorLoadingAndSavingUtils.load_map(MAP_PACKAGE)
if world is None:
    fail("Could not load the HUB map")

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = []
property_candidates = (
    "is_enabled",
    "destination_level",
    "destination_map",
    "level_name",
    "level_to_load",
    "target_level",
    "interaction_name",
    "actor_tags",
)
for actor in actor_subsystem.get_all_level_actors():
    transform = actor.get_actor_transform()
    actor_class = actor.get_class()
    class_name = actor_class.get_name()
    row = {
        "name": actor.get_name(),
        "label": actor.get_actor_label(),
        "object_path": actor.get_path_name(),
        "class_name": class_name,
        "class_path": actor_class.get_path_name(),
        "location": vector(transform.translation),
        "rotation": rotator(transform.rotation.rotator()),
        "scale": vector(transform.scale3d),
        "tags": [str(value) for value in actor.tags],
        "folder_path": str(actor.get_folder_path()),
    }
    if class_name in {
        "DoorToLevel_C",
        "0001Scene_C",
        "DummyMale_C",
        "MeleeMale_C",
    }:
        row["properties"] = {
            name: value
            for name in property_candidates
            for value in [safe_property(actor, name)]
            if value is not None
        }
        components = []
        for component in actor.get_components_by_class(unreal.ActorComponent):
            component_row = {
                "name": component.get_name(),
                "class_name": component.get_class().get_name(),
                "class_path": component.get_class().get_path_name(),
            }
            if isinstance(component, unreal.SceneComponent):
                component_row["relative_location"] = vector(component.relative_location)
                component_row["relative_rotation"] = rotator(component.relative_rotation)
                component_row["relative_scale"] = vector(component.relative_scale3d)
            components.append(component_row)
        row["components"] = components
    actors.append(row)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
options = unreal.AssetRegistryDependencyOptions()
for name in (
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
):
    try:
        options.set_editor_property(name, True)
    except Exception:
        pass
dependencies = sorted(str(value) for value in registry.get_dependencies(MAP_PACKAGE, options))

world_settings = world.get_world_settings()
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_ISOLATED_HUB_PARITY_READ_ONLY_PASS",
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project": project_file,
    "map_package": MAP_PACKAGE,
    "map_file": map_file,
    "map_before": map_before,
    "actor_count": len(actors),
    "actors": sorted(actors, key=lambda row: (row["class_name"], row["name"])),
    "dependencies": dependencies,
    "default_game_mode": safe_property(world_settings, "default_game_mode"),
    "package_saves": 0,
}

map_after = {"length": os.path.getsize(map_file), "sha256": sha256(map_file)}
if map_after != map_before:
    fail("Read-only inspection changed the harness map")
payload["map_after"] = map_after

os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
temporary = OUTPUT + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, OUTPUT)
unreal.log("CODEX_HUB_PARITY57_PASS: " + OUTPUT)
