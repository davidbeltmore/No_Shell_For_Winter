"""Fresh-process, read-only UE 5.8 validation for DoorToLevel."""

import datetime
import hashlib
import json
import os

import unreal


BP_PACKAGE = "/Game/Procedural/DoorToLevel"
BP_GENERATED_CLASS = BP_PACKAGE + ".DoorToLevel_C"
PARENT_CLASS = "/Script/EFLevelFlowRuntime.ProjectLevelDoor"
DESTINATION_LEVEL = (
    "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
)
MESH_PACKAGE = (
    "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors"
)
MESH_OBJECT = MESH_PACKAGE + ".SM_SquaredArchedWoodenDoors"
QUEST_COMPONENT_PACKAGE = (
    "/AscentCombatFramework/Integrations/Components/ACFQuestTargetComponentBP"
)
EXPECTED_DEPENDENCIES = {
    QUEST_COMPONENT_PACKAGE,
    MESH_PACKAGE,
    "/Script/AscentCombatFramework",
    "/Script/AscentMapsSystem",
    "/Script/EFLevelFlowRuntime",
}
ASSETS = {
    BP_PACKAGE: "Blueprint",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial": "Material",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal": "Material",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette": "Texture2D",
    MESH_PACKAGE: "StaticMesh",
}
EXPECTED_SCS_COMPONENTS = {
    ("QuestTargetComponent", "ACFQuestTargetComponentBP_C"),
    ("MapMarkerComponent", "AMSMapMarkerComponent"),
}


def fail(message):
    unreal.log_error("CODEX_DOOR_FRESH_LOAD_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def package_file(content_root, package_name):
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def file_snapshot(path):
    if not os.path.isfile(path):
        fail("On-disk asset file is absent: " + path)
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


def normalize_class_tag(value):
    text = str(value or "")
    if "'" in text:
        parts = text.split("'")
        if len(parts) >= 2:
            text = parts[1]
    if text.startswith("Script/"):
        text = "/" + text
    return text


def normalize_component_name(value):
    text = str(value)
    suffix = "_GEN_VARIABLE"
    return text[: -len(suffix)] if text.endswith(suffix) else text


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for property_name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        try:
            options.set_editor_property(property_name, True)
        except Exception:
            pass
    try:
        options.set_editor_property("include_searchable_names", False)
    except Exception:
        pass
    return options


def gather_scs_components(blueprint):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if subsystem is None:
        fail("SubobjectDataSubsystem is unavailable")
    handles = list(subsystem.k2_gather_subobject_data_for_blueprint(blueprint))
    if not handles:
        fail("DoorToLevel subobject inventory is empty")
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    rows = []
    for handle in handles:
        data = library.get_data(handle)
        if not library.is_component(data) or library.is_inherited_component(data):
            continue
        associated = library.get_object_for_blueprint(data, blueprint)
        rows.append(
            {
                "variable_name": normalize_component_name(
                    library.get_variable_name(data)
                ),
                "display_name": normalize_component_name(
                    library.get_display_name(data)
                ),
                "class": associated.get_class().get_name() if associated else "",
            }
        )
    actual = {
        (row["variable_name"] or row["display_name"], row["class"])
        for row in rows
    }
    if actual != EXPECTED_SCS_COMPONENTS or len(rows) != 2:
        fail("DoorToLevel SCS component inventory differs: " + repr(rows))
    return sorted(rows, key=lambda row: row["variable_name"])


def require_on_disk_asset(registry, package_name, expected_class):
    rows = [
        row
        for row in registry.get_assets_by_package_name(
            package_name, include_only_on_disk_assets=True
        )
        if class_name(row) == expected_class
    ]
    if len(rows) != 1:
        fail(
            "Expected exactly one on-disk {} at {}".format(
                expected_class, package_name
            )
        )
    asset = unreal.EditorAssetLibrary.load_asset(package_name)
    if asset is None or asset.get_class().get_name() != expected_class:
        fail("Fresh load failed for " + package_name)
    return asset, rows[0]


def main():
    evidence_value = os.environ.get("CODEX_DOOR_FRESH_EVIDENCE", "").strip()
    if not evidence_value or not os.path.isabs(evidence_value):
        fail("CODEX_DOOR_FRESH_EVIDENCE must be an absolute path")
    evidence_path = os.path.realpath(evidence_value)

    engine_version = unreal.SystemLibrary.get_engine_version()
    if not engine_version.startswith("5.8."):
        fail("Expected UE 5.8 but found " + engine_version)
    project_file = os.path.realpath(unreal.Paths.get_project_file_path())
    if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
        fail("Commandlet is not running against NoShellForWinter.uproject")
    content_root = os.path.realpath(unreal.Paths.project_content_dir())
    try:
        evidence_in_content = (
            os.path.commonpath([evidence_path, content_root]).lower()
            == content_root.lower()
        )
    except ValueError:
        evidence_in_content = False
    if evidence_in_content:
        fail("Fresh-load evidence must not be written under Content")

    asset_files = {
        package: package_file(content_root, package) for package in ASSETS
    }
    disk_before = {
        package: file_snapshot(path) for package, path in asset_files.items()
    }

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(
        [
            "/Game/Procedural",
            "/Game/Calysto/Dungeon/Demo/LowPoly",
            "/Game/Calysto/Dungeon/Mesh/DungeonMesh",
        ],
        True,
    )
    registry.wait_for_completion()
    loaded = {}
    asset_data = {}
    for package, expected_class in ASSETS.items():
        loaded[package], asset_data[package] = require_on_disk_asset(
            registry, package, expected_class
        )

    blueprint = loaded[BP_PACKAGE]
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    compile_status = str(blueprint.get_editor_property("status"))
    normalized_status = "".join(
        character for character in compile_status.upper() if character.isalnum()
    )
    if "UPTODATE" not in normalized_status:
        fail("DoorToLevel compile status is " + compile_status)

    native_parent_tag = normalize_class_tag(
        asset_data[BP_PACKAGE].get_tag_value("NativeParentClass")
    )
    parent_tag = normalize_class_tag(
        asset_data[BP_PACKAGE].get_tag_value("ParentClass")
    )
    generated_class = unreal.load_class(None, BP_GENERATED_CLASS)
    native_parent = unreal.load_class(None, PARENT_CLASS)
    if generated_class is None or native_parent is None:
        fail("DoorToLevel generated or native parent class could not load")
    if not unreal.MathLibrary.class_is_child_of(generated_class, native_parent):
        fail("DoorToLevel generated class is not a child of AProjectLevelDoor")
    generated_parent = PARENT_CLASS
    if (
        native_parent_tag != PARENT_CLASS
        or parent_tag != PARENT_CLASS
        or generated_parent != PARENT_CLASS
    ):
        fail(
            "DoorToLevel parent differs: tags={!r}/{!r}, generated={!r}".format(
                native_parent_tag, parent_tag, generated_parent
            )
        )

    cdo = unreal.get_default_object(generated_class)
    if cdo is None:
        fail("DoorToLevel generated-class CDO is absent")
    components = list(cdo.get_components_by_class(unreal.ActorComponent))
    static_mesh_components = [
        component
        for component in components
        if component.get_class().get_name() == "StaticMeshComponent"
    ]
    sphere_components = [
        component
        for component in components
        if component.get_class().get_name() == "SphereComponent"
    ]
    if len(static_mesh_components) != 1 or len(sphere_components) != 1:
        fail("DoorToLevel native mesh/sphere component inventory differs")
    mesh = object_path(static_mesh_components[0].get_editor_property("static_mesh"))
    sphere_radius = float(sphere_components[0].get_unscaled_sphere_radius())
    if mesh != MESH_OBJECT:
        fail("DoorToLevel mesh differs: " + mesh)
    if abs(sphere_radius - 180.0) > 0.01:
        fail("DoorToLevel Sphere radius differs: " + str(sphere_radius))

    defaults = {
        "interactable_name": str(cdo.get_editor_property("interactable_name")),
        "is_enabled": bool(cdo.get_editor_property("is_enabled")),
        "destination_level": object_path(
            cdo.get_editor_property("destination_level")
        ),
        "absolute_travel": bool(cdo.get_editor_property("absolute_travel")),
        "travel_options": str(cdo.get_editor_property("travel_options")),
    }
    expected_defaults = {
        "interactable_name": "Interact",
        "is_enabled": True,
        "destination_level": DESTINATION_LEVEL,
        "absolute_travel": True,
        "travel_options": "",
    }
    if defaults != expected_defaults:
        fail("DoorToLevel defaults differ: " + repr(defaults))

    scs_components = gather_scs_components(blueprint)
    dependencies = sorted(
        {
            str(value)
            for value in registry.get_dependencies(
                BP_PACKAGE, dependency_options()
            )
        }
    )
    if set(dependencies) != EXPECTED_DEPENDENCIES:
        fail("DoorToLevel dependencies differ: " + repr(dependencies))

    disk_after = {
        package: file_snapshot(path) for package, path in asset_files.items()
    }
    if disk_after != disk_before:
        fail("Fresh validation changed one or more asset package files")

    payload = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(
            datetime.timezone.utc
        ).isoformat(),
        "status": "UE58_DOOR_TO_LEVEL_FRESH_READ_ONLY_LOAD_PASS",
        "engine_version": engine_version,
        "project": project_file,
        "blueprint": BP_PACKAGE,
        "compile_status": compile_status,
        "native_parent_class": PARENT_CLASS,
        "generated_parent_class": generated_parent,
        "defaults": defaults,
        "static_mesh": mesh,
        "sphere_radius": sphere_radius,
        "scs_components": scs_components,
        "dependencies": dependencies,
        "loaded_from_on_disk_asset_registry": True,
        "asset_save_operations": [],
        "asset_files_unchanged": True,
        "asset_files": disk_after,
    }
    os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
    temporary_path = evidence_path + ".tmp"
    with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
    os.replace(temporary_path, evidence_path)
    unreal.log("CODEX_DOOR_FRESH_LOAD_PASS: " + evidence_path)


main()
