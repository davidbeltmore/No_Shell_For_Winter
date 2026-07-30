"""Read-only structural inspection of the original UE 5.7 DoorToLevel Blueprint."""

import json
import os
import traceback

import unreal


OUTPUT = os.path.realpath(
    os.environ.get(
        "CODEX_DOOR_SOURCE57_OUTPUT",
        os.path.join(
            unreal.Paths.project_saved_dir(),
            "Migration",
            "CalystoProcedural",
            "DoorToLevelSource57.json",
        ),
    )
)
BP_PATH = "/Game/Procedural/DoorToLevel.DoorToLevel"
BP_CLASS_PATH = "/Game/Procedural/DoorToLevel.DoorToLevel_C"
HUB_MAP = "/Game/_Game/Hub/HUB"


def object_path(value):
    if not value:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return ""


def vector(value):
    return {"x": value.x, "y": value.y, "z": value.z}


def rotator(value):
    return {"pitch": value.pitch, "yaw": value.yaw, "roll": value.roll}


def property_value(value, name):
    try:
        result = value.get_editor_property(name)
    except Exception:
        return None
    if isinstance(result, (bool, int, float, str)) or result is None:
        return result
    return object_path(result) or str(result)


def parent_class_path(generated_class):
    for method_name in ("get_super_class", "get_super_struct"):
        method = getattr(generated_class, method_name, None)
        if callable(method):
            try:
                return object_path(method())
            except Exception:
                pass
    return "UNAVAILABLE"


def implemented_interface_paths(blueprint):
    try:
        values = blueprint.get_editor_property("implemented_interfaces")
    except Exception:
        return []
    rows = []
    for value in values:
        interface_class = getattr(value, "interface", None)
        rows.append(object_path(interface_class) or str(value))
    return rows


def graph_names(blueprint):
    list_graphs = getattr(unreal.BlueprintEditorLibrary, "list_graphs", None)
    if callable(list_graphs):
        try:
            return [graph.get_name() for graph in list_graphs(blueprint)]
        except Exception:
            pass
    return ["UNAVAILABLE_IN_UE57_PYTHON_API"]


def component_row(component):
    row = {
        "name": component.get_name(),
        "class": component.get_class().get_name(),
        "class_path": component.get_class().get_path_name(),
    }
    if isinstance(component, unreal.SceneComponent):
        row.update(
            {
                "relative_location": vector(component.relative_location),
                "relative_rotation": rotator(component.relative_rotation),
                "relative_scale": vector(component.relative_scale3d),
                "absolute_location": property_value(component, "absolute_location"),
                "absolute_rotation": property_value(component, "absolute_rotation"),
                "absolute_scale": property_value(component, "absolute_scale"),
                "attach_parent": object_path(component.get_attach_parent()),
            }
        )
    if isinstance(component, unreal.PrimitiveComponent):
        probes = {
            "collision_profile_name": "collision_profile_name",
            "generate_overlap_events": "generate_overlap_events",
            "can_character_step_up_on": "can_character_step_up_on",
        }
        row["primitive_properties"] = {
            output_name: property_value(component, property_name)
            for output_name, property_name in probes.items()
        }
        for method_name, output_name in (
            ("get_collision_enabled", "collision_enabled"),
            ("get_collision_object_type", "collision_object_type"),
        ):
            try:
                row["primitive_properties"][output_name] = str(
                    getattr(component, method_name)()
                )
            except Exception as exc:
                row["primitive_properties"][output_name] = "UNAVAILABLE: " + str(exc)
        row["collision_responses"] = {}
        for channel_name in (
            "ECC_WORLD_STATIC",
            "ECC_WORLD_DYNAMIC",
            "ECC_PAWN",
            "ECC_VISIBILITY",
            "ECC_CAMERA",
            "ECC_PHYSICS_BODY",
            "ECC_VEHICLE",
            "ECC_DESTRUCTIBLE",
            "ECC_GAME_TRACE_CHANNEL1",
            "ECC_GAME_TRACE_CHANNEL12",
            "ECC_GAME_TRACE_CHANNEL13",
            "ECC_GAME_TRACE_CHANNEL15",
        ):
            try:
                channel = getattr(unreal.CollisionChannel, channel_name)
                response = component.get_collision_response_to_channel(channel)
                row["collision_responses"][channel_name] = str(response)
            except Exception as exc:
                row["collision_responses"][channel_name] = "UNAVAILABLE: " + str(exc)
    if isinstance(component, unreal.SphereComponent):
        row["sphere_radius"] = float(component.get_unscaled_sphere_radius())
    if isinstance(component, unreal.StaticMeshComponent):
        row["static_mesh"] = object_path(component.static_mesh)
    return row


def main():
    payload = {
        "status": "UE57_DOOR_TO_LEVEL_SOURCE_READ_ONLY_FAIL",
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "project": os.path.realpath(unreal.Paths.get_project_file_path()),
        "blueprint": BP_PATH,
    }
    try:
        if not payload["engine_version"].startswith("5.7."):
            raise RuntimeError("Expected Unreal Engine 5.7")
        blueprint = unreal.load_asset(BP_PATH)
        generated_class = unreal.load_class(None, BP_CLASS_PATH)
        if not blueprint or not generated_class:
            raise RuntimeError("DoorToLevel Blueprint or generated class did not load")
        cdo = unreal.get_default_object(generated_class)
        world = unreal.EditorLoadingAndSavingUtils.load_map(HUB_MAP)
        if not world:
            raise RuntimeError("Source HUB did not load")
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        doors = [
            actor
            for actor in actor_subsystem.get_all_level_actors()
            if actor.get_class().get_name() == "DoorToLevel_C"
        ]
        if len(doors) != 1:
            raise RuntimeError("Expected exactly one DoorToLevel actor in source HUB")
        door = doors[0]
        transform = door.get_actor_transform()
        payload.update(
            {
                "blueprint_class": blueprint.get_class().get_name(),
                "generated_class": generated_class.get_path_name(),
                "parent_class": parent_class_path(generated_class),
                "implemented_interfaces": implemented_interface_paths(blueprint),
                "graphs": graph_names(blueprint),
                "cdo_components": [
                    component_row(component)
                    for component in cdo.get_components_by_class(unreal.ActorComponent)
                ],
                "hub_actor": {
                    "object_path": object_path(door),
                    "location": vector(transform.translation),
                    "rotation": rotator(transform.rotation.rotator()),
                    "scale": vector(transform.scale3d),
                    "root_component": object_path(
                        door.get_editor_property("root_component")
                    ),
                    "components": [
                        component_row(component)
                        for component in door.get_components_by_class(
                            unreal.ActorComponent
                        )
                    ],
                },
                "status": "UE57_DOOR_TO_LEVEL_SOURCE_READ_ONLY_PASS",
            }
        )
    except Exception as exc:
        payload["error"] = str(exc)
        payload["traceback"] = traceback.format_exc()

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
    if payload["status"].endswith("_PASS"):
        unreal.log("CODEX_DOOR_SOURCE57_PASS: " + OUTPUT)
    else:
        unreal.log_error("CODEX_DOOR_SOURCE57_FAIL: " + payload.get("error", "unknown"))


main()
