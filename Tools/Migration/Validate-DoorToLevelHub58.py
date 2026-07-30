"""Read-only UE 5.8 validation of the map-owned DoorToLevel in HUB.

Run this in a dedicated NoShellForWinter UE 5.8 editor process. The script
loads HUB, inspects the serialized actor, writes evidence under Saved, and
requests editor shutdown without calling any save API.
"""

import datetime
import hashlib
import json
import os
import traceback

import unreal


HUB_MAP_PACKAGE = "/Game/_Game/Hub/HUB"
HUB_WORLD_OBJECT = HUB_MAP_PACKAGE + ".HUB"
DOOR_BLUEPRINT_PACKAGE = "/Game/Procedural/DoorToLevel"
DOOR_BLUEPRINT_OBJECT = DOOR_BLUEPRINT_PACKAGE + ".DoorToLevel"
DOOR_GENERATED_CLASS = DOOR_BLUEPRINT_PACKAGE + ".DoorToLevel_C"
NATIVE_PARENT_CLASS = "/Script/EFLevelFlowRuntime.ProjectLevelDoor"
EXPECTED_COMPONENT_NAMES = {
    "InteractableComponent",
    "StaticMesh",
    "Sphere",
    "QuestTargetComponent",
    "MapMarkerComponent",
}
EXPECTED_LOCATION = {"x": 1250.0, "y": 200.0, "z": 110.0}
EXPECTED_ROTATION = {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}
EXPECTED_SCALE = {"x": 1.0, "y": 1.0, "z": 1.0}
TRANSFORM_TOLERANCE = 0.001
PASS_STATUS = "UE58_DOOR_TO_LEVEL_HUB_READ_ONLY_PLACEMENT_PASS"
FAIL_STATUS = "UE58_DOOR_TO_LEVEL_HUB_READ_ONLY_PLACEMENT_FAIL"


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def file_snapshot(path):
    real_path = os.path.realpath(path)
    if not os.path.isfile(real_path):
        raise RuntimeError("Required package file is absent: " + real_path)
    stat = os.stat(real_path)
    return {
        "file": real_path,
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(real_path),
    }


def dirty_packages(function_name):
    values = getattr(unreal.EditorLoadingAndSavingUtils, function_name)()
    return sorted({object_path(value) or str(value) for value in values})


def dirty_snapshot():
    return {
        "content": dirty_packages("get_dirty_content_packages"),
        "maps": dirty_packages("get_dirty_map_packages"),
    }


def normalize_component_name(name):
    text = str(name)
    suffix = "_GEN_VARIABLE"
    return text[: -len(suffix)] if text.endswith(suffix) else text


def vector_dict(value):
    return {
        "x": float(value.x),
        "y": float(value.y),
        "z": float(value.z),
    }


def rotator_dict(value):
    return {
        "pitch": float(value.pitch),
        "yaw": float(value.yaw),
        "roll": float(value.roll),
    }


def require_near(actual, expected, label):
    mismatches = {
        axis: {"actual": actual[axis], "expected": expected_value}
        for axis, expected_value in expected.items()
        if abs(actual[axis] - expected_value) > TRANSFORM_TOLERANCE
    }
    if mismatches:
        raise RuntimeError("{} differs: {!r}".format(label, mismatches))


def component_inventory(actor):
    rows = []
    for component in actor.get_components_by_class(unreal.ActorComponent):
        raw_name = component.get_name()
        rows.append(
            {
                "name": normalize_component_name(raw_name),
                "object_name": str(raw_name),
                "class": object_path(component.get_class()),
                "object_path": object_path(component),
            }
        )
    return sorted(rows, key=lambda row: (row["name"], row["class"]))


def attachment_chain(actor):
    rows = []
    seen = set()
    current = actor.get_attach_parent_actor()
    while current is not None:
        current_path = object_path(current)
        if current_path in seen:
            raise RuntimeError("DoorToLevel attachment chain contains a cycle")
        seen.add(current_path)
        rows.append(
            {
                "actor_path": current_path,
                "label": str(current.get_actor_label()),
                "class": object_path(current.get_class()),
            }
        )
        current = current.get_attach_parent_actor()
    return rows


def validate():
    engine_version = unreal.SystemLibrary.get_engine_version()
    if not engine_version.startswith("5.8."):
        raise RuntimeError("Expected UE 5.8 but found " + engine_version)

    project_file = os.path.realpath(unreal.Paths.get_project_file_path())
    if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
        raise RuntimeError(
            "Validator is not running against NoShellForWinter.uproject: "
            + project_file
        )

    content_root = os.path.realpath(unreal.Paths.project_content_dir())
    package_files = {
        HUB_MAP_PACKAGE: os.path.join(content_root, "_Game", "Hub", "HUB.umap"),
        DOOR_BLUEPRINT_PACKAGE: os.path.join(
            content_root, "Procedural", "DoorToLevel.uasset"
        ),
    }
    disk_before = {
        package: file_snapshot(path) for package, path in package_files.items()
    }
    dirty_before = dirty_snapshot()

    blueprint = unreal.EditorAssetLibrary.load_asset(DOOR_BLUEPRINT_PACKAGE)
    if blueprint is None or object_path(blueprint) != DOOR_BLUEPRINT_OBJECT:
        raise RuntimeError(
            "DoorToLevel Blueprint failed exact load: " + object_path(blueprint)
        )
    generated_class = unreal.load_class(None, DOOR_GENERATED_CLASS)
    native_parent = unreal.load_class(None, NATIVE_PARENT_CLASS)
    if generated_class is None or native_parent is None:
        raise RuntimeError("DoorToLevel generated or native parent class failed to load")
    if not unreal.MathLibrary.class_is_child_of(generated_class, native_parent):
        raise RuntimeError(
            "DoorToLevel_C is not derived from " + NATIVE_PARENT_CLASS
        )

    world = unreal.EditorLoadingAndSavingUtils.load_map(HUB_MAP_PACKAGE)
    if world is None or object_path(world) != HUB_WORLD_OBJECT:
        raise RuntimeError("HUB failed exact load: " + object_path(world))

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if actor_subsystem is None:
        raise RuntimeError("EditorActorSubsystem is unavailable")

    derived_candidates = []
    for actor in actor_subsystem.get_all_level_actors():
        actor_class = actor.get_class()
        if unreal.MathLibrary.class_is_child_of(actor_class, native_parent):
            derived_candidates.append(actor)
    if len(derived_candidates) != 1:
        candidate_rows = [
            {
                "actor_path": object_path(actor),
                "label": str(actor.get_actor_label()),
                "class": object_path(actor.get_class()),
            }
            for actor in derived_candidates
        ]
        raise RuntimeError(
            "HUB must contain exactly one ProjectLevelDoor-derived actor: {!r}".format(
                candidate_rows
            )
        )

    door = derived_candidates[0]
    door_class_path = object_path(door.get_class())
    if door_class_path != DOOR_GENERATED_CLASS:
        raise RuntimeError(
            "HUB door is not the exact /Game/Procedural/DoorToLevel asset: "
            + door_class_path
        )

    door_path = object_path(door)
    persistent_level_prefix = HUB_WORLD_OBJECT + ":PersistentLevel."
    if not door_path.startswith(persistent_level_prefix):
        raise RuntimeError(
            "DoorToLevel is not serialized in HUB PersistentLevel: " + door_path
        )

    location = vector_dict(door.get_actor_location())
    rotation = rotator_dict(door.get_actor_rotation())
    scale = vector_dict(door.get_actor_scale3d())
    require_near(location, EXPECTED_LOCATION, "DoorToLevel location")
    require_near(rotation, EXPECTED_ROTATION, "DoorToLevel rotation")
    require_near(scale, EXPECTED_SCALE, "DoorToLevel scale")

    components = component_inventory(door)
    component_counts = {
        expected_name: sum(
            1 for row in components if row["name"] == expected_name
        )
        for expected_name in sorted(EXPECTED_COMPONENT_NAMES)
    }
    invalid_counts = {
        name: count for name, count in component_counts.items() if count != 1
    }
    if invalid_counts:
        raise RuntimeError(
            "DoorToLevel required component inventory differs: counts={!r}, rows={!r}".format(
                invalid_counts, components
            )
        )

    attach_parent = door.get_attach_parent_actor()
    owner = door.get_owner()
    chain = attachment_chain(door)
    root_component = door.get_root_component()
    root_attach_parent = (
        root_component.get_attach_parent() if root_component is not None else None
    )
    if attach_parent is not None or root_attach_parent is not None or chain:
        raise RuntimeError(
            "DoorToLevel is attached instead of map-owned: actor_parent={!r}, "
            "root_parent={!r}, chain={!r}".format(
                object_path(attach_parent), object_path(root_attach_parent), chain
            )
        )
    if owner is not None:
        raise RuntimeError(
            "DoorToLevel has a runtime-style owner instead of being map-owned: "
            + object_path(owner)
        )

    dirty_after = dirty_snapshot()
    new_dirty = {
        kind: sorted(set(dirty_after[kind]) - set(dirty_before[kind]))
        for kind in ("content", "maps")
    }
    if new_dirty["content"] or new_dirty["maps"]:
        raise RuntimeError(
            "Read-only validation dirtied packages: " + repr(new_dirty)
        )

    disk_after = {
        package: file_snapshot(path) for package, path in package_files.items()
    }
    if disk_after != disk_before:
        raise RuntimeError("Read-only validation changed HUB or DoorToLevel on disk")

    return {
        "engine_version": engine_version,
        "project": project_file,
        "hub_map": HUB_MAP_PACKAGE,
        "loaded_world": object_path(world),
        "door_blueprint": object_path(blueprint),
        "door_generated_class": door_class_path,
        "native_parent_class": NATIVE_PARENT_CLASS,
        "derived_actor_count": len(derived_candidates),
        "actor": {
            "actor_path": door_path,
            "label": str(door.get_actor_label()),
            "location": location,
            "rotation": rotation,
            "scale": scale,
            "components": components,
            "required_component_counts": component_counts,
            "attach_parent_actor": object_path(attach_parent),
            "root_attach_parent_component": object_path(root_attach_parent),
            "attachment_chain": chain,
            "owner": object_path(owner),
            "persistent_level_serialized": True,
            "player_attached": False,
        },
        "transform_tolerance": TRANSFORM_TOLERANCE,
        "dirty_packages_before": dirty_before,
        "dirty_packages_after": dirty_after,
        "new_dirty_packages": new_dirty,
        "asset_files_before": disk_before,
        "asset_files_after": disk_after,
        "asset_files_unchanged": True,
        "save_operations": [],
    }


def evidence_path():
    return os.path.realpath(
        os.path.join(
            unreal.Paths.project_saved_dir(),
            "Migration",
            "CalystoProcedural",
            "DoorToLevelHubValidation58.json",
        )
    )


def write_evidence(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
    os.replace(temporary, path)


def main():
    output = evidence_path()
    payload = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(
            datetime.timezone.utc
        ).isoformat(),
        "status": FAIL_STATUS,
        "validation_mode": "fresh_editor_read_only_no_save",
        "evidence": output,
    }
    error = None
    try:
        payload.update(validate())
        payload["status"] = PASS_STATUS
    except Exception as exc:
        error = exc
        payload["error"] = str(exc)
        payload["traceback"] = traceback.format_exc()

    write_evidence(output, payload)
    if error is None:
        unreal.log("CODEX_DOOR_HUB_VALIDATION_PASS: " + output)
    else:
        unreal.log_error(
            "CODEX_DOOR_HUB_VALIDATION_FAIL: {}: {}".format(output, error)
        )

    # Deliberately no save/compile/mutation call occurs anywhere in this script.
    unreal.SystemLibrary.quit_editor()
    if error is not None:
        raise RuntimeError(str(error))


main()
