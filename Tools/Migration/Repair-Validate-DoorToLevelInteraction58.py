"""Repair DoorToLevel source parity and its map-owned HUB instance in UE 5.8."""

import datetime
import json
import os
import traceback

import unreal


BP_PACKAGE = "/Game/Procedural/DoorToLevel"
BP_CLASS = BP_PACKAGE + ".DoorToLevel_C"
NATIVE_PARENT = "/Script/EFLevelFlowRuntime.ProjectLevelDoor"
HUB_MAP = "/Game/_Game/Hub/HUB"
DESTINATION_MAP = "/Game/Procedural/Maps/DungeonGeneration"
MESH_PACKAGE = "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors"
OUTPUT = os.path.realpath(
    os.path.join(
        unreal.Paths.project_saved_dir(),
        "Migration",
        "CalystoProcedural",
        "DoorToLevelInteractionRepair58.json",
    )
)
EXPECTED_LOCATION = unreal.Vector(1250.0, 200.0, 110.0)
EXPECTED_ROTATION = unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0)
EXPECTED_SCALE = unreal.Vector(1.0, 1.0, 1.0)
EXPECTED_MESH_LOCATION = unreal.Vector(0.0, 60.0, -110.0)
EXPECTED_MESH_ROTATION = unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0)


def fail(message):
    raise RuntimeError(message)


def object_path(value):
    return value.get_path_name() if value else ""


def vector(value):
    return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}


def rotator(value):
    return {
        "pitch": float(value.pitch),
        "yaw": float(value.yaw),
        "roll": float(value.roll),
    }


def near(a, b, tolerance=0.01):
    return abs(float(a) - float(b)) <= tolerance


def vectors_match(actual, expected):
    return all(
        near(getattr(actual, axis), getattr(expected, axis))
        for axis in ("x", "y", "z")
    )


def rotations_match(actual, expected):
    return all(
        near(getattr(actual, axis), getattr(expected, axis))
        for axis in ("pitch", "yaw", "roll")
    )


def one_component(actor, component_class, label):
    values = list(actor.get_components_by_class(component_class))
    if len(values) != 1:
        fail("Expected exactly one {} component; found {}".format(label, len(values)))
    return values[0]


def configure_sphere(sphere):
    sphere.set_sphere_radius(100.0, False)
    sphere.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    sphere.set_collision_object_type(unreal.CollisionChannel.ECC_PAWN)
    sphere.set_collision_response_to_all_channels(
        unreal.CollisionResponseType.ECR_OVERLAP
    )
    sphere.set_editor_property("generate_overlap_events", True)


def configure_mesh(mesh, mesh_asset, sphere=None):
    if sphere is not None and mesh.get_attach_parent() != sphere:
        mesh.modify()
        attached = mesh.attach_to_component(
            sphere,
            "",
            unreal.AttachmentRule.KEEP_RELATIVE,
            unreal.AttachmentRule.KEEP_RELATIVE,
            unreal.AttachmentRule.KEEP_RELATIVE,
            False,
        )
        if not attached or mesh.get_attach_parent() != sphere:
            fail("Failed to restore StaticMesh attachment to the Sphere root")
    # UE returns False when the requested mesh is already assigned; validate the
    # resulting property below instead of treating that no-op as a failure.
    mesh.set_static_mesh(mesh_asset)
    mesh.set_editor_property("relative_location", EXPECTED_MESH_LOCATION)
    mesh.set_editor_property("relative_rotation", EXPECTED_MESH_ROTATION)
    mesh.set_collision_profile_name("BlockAllDynamic")
    mesh.set_editor_property("generate_overlap_events", True)


def component_snapshot(actor):
    sphere = one_component(actor, unreal.SphereComponent, "Sphere")
    mesh = one_component(actor, unreal.StaticMeshComponent, "StaticMesh")
    root = actor.get_editor_property("root_component")
    sphere_parent = sphere.get_attach_parent()
    mesh_parent = mesh.get_attach_parent()
    return {
        "root": object_path(root),
        "root_name": root.get_name() if root else "",
        "sphere": {
            "name": sphere.get_name(),
            "parent": object_path(sphere_parent),
            "radius": float(sphere.get_unscaled_sphere_radius()),
            "collision_enabled": str(sphere.get_collision_enabled()),
            "object_type": str(sphere.get_collision_object_type()),
            "world_dynamic_response": str(
                sphere.get_collision_response_to_channel(
                    unreal.CollisionChannel.ECC_WORLD_DYNAMIC
                )
            ),
            "pawn_response": str(
                sphere.get_collision_response_to_channel(
                    unreal.CollisionChannel.ECC_PAWN
                )
            ),
            "generate_overlap_events": bool(
                sphere.get_editor_property("generate_overlap_events")
            ),
        },
        "mesh": {
            "name": mesh.get_name(),
            "parent": object_path(mesh_parent),
            "parent_name": mesh_parent.get_name() if mesh_parent else "",
            "asset": object_path(mesh.get_editor_property("static_mesh")),
            "relative_location": vector(mesh.get_editor_property("relative_location")),
            "relative_rotation": rotator(mesh.get_editor_property("relative_rotation")),
            "collision_enabled": str(mesh.get_collision_enabled()),
            "object_type": str(mesh.get_collision_object_type()),
            "world_dynamic_response": str(
                mesh.get_collision_response_to_channel(
                    unreal.CollisionChannel.ECC_WORLD_DYNAMIC
                )
            ),
        },
    }


def validate_components(snapshot):
    if snapshot["root_name"] != "Sphere":
        fail("Door root is not the source-faithful Sphere: " + snapshot["root_name"])
    sphere = snapshot["sphere"]
    if not near(sphere["radius"], 100.0):
        fail("Door Sphere radius is not 100")
    if "QUERY_ONLY" not in sphere["collision_enabled"]:
        fail("Door Sphere is not QueryOnly")
    if "ECC_PAWN" not in sphere["object_type"]:
        fail("Door Sphere object type is not Pawn")
    if "ECR_OVERLAP" not in sphere["world_dynamic_response"]:
        fail("Door Sphere does not overlap the ACF detector's WorldDynamic channel")
    if "ECR_OVERLAP" not in sphere["pawn_response"]:
        fail("Door Sphere does not overlap Pawn")
    if not sphere["generate_overlap_events"]:
        fail("Door Sphere overlap events are disabled")
    mesh = snapshot["mesh"]
    if mesh["parent_name"] != "Sphere":
        fail("Door mesh is not attached to Sphere")
    if mesh["asset"] != MESH_PACKAGE + ".SM_SquaredArchedWoodenDoors":
        fail("Door mesh asset differs: " + mesh["asset"])
    mesh_location = mesh["relative_location"]
    mesh_rotation = mesh["relative_rotation"]
    if not all(
        near(mesh_location[axis], vector(EXPECTED_MESH_LOCATION)[axis])
        for axis in ("x", "y", "z")
    ):
        fail("Door mesh relative location differs: " + repr(mesh_location))
    if not all(
        near(mesh_rotation[axis], rotator(EXPECTED_MESH_ROTATION)[axis])
        for axis in ("pitch", "yaw", "roll")
    ):
        fail("Door mesh relative rotation differs: " + repr(mesh_rotation))


def main():
    result = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_DOOR_TO_LEVEL_INTERACTION_REPAIR_FAIL",
        "source_contract": {
            "actor_location": vector(EXPECTED_LOCATION),
            "sphere_root_radius": 100.0,
            "sphere_object_type": "ECC_Pawn",
            "sphere_collision": "QueryOnly / Overlap",
            "mesh_relative_location": vector(EXPECTED_MESH_LOCATION),
            "mesh_relative_rotation": rotator(EXPECTED_MESH_ROTATION),
        },
        "save_operations": [],
    }
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(BP_PACKAGE)
        parent = unreal.load_class(None, NATIVE_PARENT)
        mesh_asset = unreal.EditorAssetLibrary.load_asset(MESH_PACKAGE)
        destination = unreal.EditorAssetLibrary.load_asset(DESTINATION_MAP)
        if not blueprint or blueprint.get_class().get_name() != "Blueprint":
            fail("DoorToLevel is not a Blueprint asset")
        if not parent or not mesh_asset or not destination:
            fail("Native parent, door mesh, or destination map is absent")
        direct_parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
        if object_path(direct_parent) != NATIVE_PARENT:
            fail("DoorToLevel parent differs: " + object_path(direct_parent))
        event_graph_removed = False
        event_graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
        if event_graph is not None:
            blueprint.modify()
            unreal.BlueprintEditorLibrary.remove_graph(blueprint, event_graph)
            event_graph_removed = True
        if unreal.BlueprintEditorLibrary.find_event_graph(blueprint) is not None:
            fail("Could not remove DoorToLevel's duplicate travel EventGraph")

        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status = str(blueprint.get_editor_property("status"))
        if "UPTODATE" not in "".join(ch for ch in status.upper() if ch.isalnum()):
            fail("DoorToLevel compile status is " + status)
        generated_class = unreal.load_class(None, BP_CLASS)
        if not generated_class or not unreal.MathLibrary.class_is_child_of(
            generated_class, parent
        ):
            fail("DoorToLevel generated class is not a ProjectLevelDoor child")
        cdo = unreal.get_default_object(generated_class)
        cdo.modify()
        cdo.set_editor_property("interactable_name", "Interact")
        cdo.set_editor_property("is_enabled", True)
        cdo.set_editor_property("destination_level", destination)
        cdo.set_editor_property("absolute_travel", True)
        cdo.set_editor_property("travel_options", "")
        cdo_sphere = one_component(cdo, unreal.SphereComponent, "CDO Sphere")
        configure_sphere(cdo_sphere)
        configure_mesh(
            one_component(cdo, unreal.StaticMeshComponent, "CDO StaticMesh"),
            mesh_asset,
            cdo_sphere,
        )
        cdo_snapshot = component_snapshot(cdo)
        validate_components(cdo_snapshot)
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            blueprint, only_if_is_dirty=False
        ):
            fail("Failed to save DoorToLevel Blueprint")
        result["save_operations"].append(BP_PACKAGE)

        world = unreal.EditorLoadingAndSavingUtils.load_map(HUB_MAP)
        if not world:
            fail("Failed to load HUB")
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        doors = [
            actor
            for actor in actor_subsystem.get_all_level_actors()
            if object_path(actor.get_class()) == BP_CLASS
        ]
        if len(doors) != 1:
            fail("HUB must contain exactly one DoorToLevel; found {}".format(len(doors)))
        door = doors[0]
        if door.get_attach_parent_actor() is not None:
            fail("HUB DoorToLevel must be map-owned and unattached before repair")
        door.set_actor_location(EXPECTED_LOCATION, False, False)
        door.set_actor_rotation(EXPECTED_ROTATION, False)
        door.set_actor_scale3d(EXPECTED_SCALE)
        door.set_actor_label("DoorToLevel")
        actor_sphere = one_component(door, unreal.SphereComponent, "actor Sphere")
        configure_sphere(actor_sphere)
        configure_mesh(
            one_component(door, unreal.StaticMeshComponent, "actor StaticMesh"),
            mesh_asset,
            actor_sphere,
        )
        actor_snapshot = component_snapshot(door)
        validate_components(actor_snapshot)
        if not vectors_match(door.get_actor_location(), EXPECTED_LOCATION):
            fail("HUB DoorToLevel location differs")
        if not rotations_match(door.get_actor_rotation(), EXPECTED_ROTATION):
            fail("HUB DoorToLevel rotation differs")
        if not vectors_match(door.get_actor_scale3d(), EXPECTED_SCALE):
            fail("HUB DoorToLevel scale differs")
        if door.get_attach_parent_actor() is not None:
            fail("HUB DoorToLevel remains attached to another actor")
        if not unreal.EditorLevelLibrary.save_current_level():
            fail("Failed to save HUB after DoorToLevel repair")
        result["save_operations"].append(HUB_MAP)

        result.update(
            {
                "status": "UE58_DOOR_TO_LEVEL_INTERACTION_REPAIR_PASS",
                "blueprint_class": object_path(generated_class),
                "native_parent": object_path(direct_parent),
                "blueprint_compile_status": status,
                "event_graph_present": False,
                "event_graph_removed_during_repair": event_graph_removed,
                "cdo": cdo_snapshot,
                "hub_actor": {
                    "path": object_path(door),
                    "label": door.get_actor_label(),
                    "location": vector(door.get_actor_location()),
                    "rotation": rotator(door.get_actor_rotation()),
                    "scale": vector(door.get_actor_scale3d()),
                    "attach_parent_actor": object_path(door.get_attach_parent_actor()),
                    "components": actor_snapshot,
                },
                "player_spawned": False,
                "placement_owner": HUB_MAP + ".HUB:PersistentLevel",
                "runtime_travel_owner": "AProjectLevelDoor::OnInteractedByPawn_Implementation",
            }
        )
    except Exception as exc:
        result["error"] = str(exc)
        result["traceback"] = traceback.format_exc()

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(result, handle, indent=2, sort_keys=True)
        handle.write("\n")
    if result["status"].endswith("_PASS"):
        unreal.log("CODEX_DOOR_INTERACTION_REPAIR_PASS: " + OUTPUT)
    else:
        unreal.log_error(
            "CODEX_DOOR_INTERACTION_REPAIR_FAIL: " + result.get("error", "unknown")
        )
    unreal.SystemLibrary.quit_editor()
    if not result["status"].endswith("_PASS"):
        raise RuntimeError(result.get("error", "DoorToLevel repair failed"))


main()
