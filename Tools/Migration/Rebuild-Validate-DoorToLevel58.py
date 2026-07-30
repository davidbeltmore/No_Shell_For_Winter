"""Resave the four visual assets and rebuild DoorToLevel as a thin UE 5.8 Blueprint."""

import datetime
import hashlib
import json
import os

import unreal


VISUAL_PACKAGE_COUNT = 4
SOURCE_BYTES = 189704
SOURCE_FINGERPRINT = (
    "4477D83F3722FA80674C18791BB2A85DCCE5DBE19FD57D17B52C20BE716212CC"
)
VISUALS = {
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial": {
        "class": "Material",
        "source_length": 15251,
        "source_sha256": "1DA354F36752F99E8741529372A580CB4B174C390DB080A62037E55CC8771941",
        "relative": "calysto/dungeon/demo/lowpoly/material/m_basematerial.uasset",
        "game_dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette"
        },
    },
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal": {
        "class": "Material",
        "source_length": 19454,
        "source_sha256": "8722C616F22B81315E266A471785B4169F45F2E9CCEF5229F08E27A6ED824B23",
        "relative": "calysto/dungeon/demo/lowpoly/material/m_metal.uasset",
        "game_dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette"
        },
    },
    "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette": {
        "class": "Texture2D",
        "source_length": 100800,
        "source_sha256": "515E0A851F19035D612620101F32133243442FFAEF0F0DC93FA049F0C931D26B",
        "relative": "calysto/dungeon/demo/lowpoly/texture/t_palette.uasset",
        "game_dependencies": set(),
    },
    "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors": {
        "class": "StaticMesh",
        "source_length": 54199,
        "source_sha256": "40AF92CF0E91C356B13AB171065C17A649CD874BBB6724A29793A7B91EB8A3A7",
        "relative": "calysto/dungeon/mesh/dungeonmesh/sm_squaredarchedwoodendoors.uasset",
        "game_dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial",
            "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal",
        },
    },
}
LOAD_ORDER = (
    "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal",
    "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors",
)
ALLOWED_VISUAL_EXTERNAL_DEPENDENCIES = {
    "/Script/Engine",
    "/Script/InterchangeEngine",
    "/Script/MeshDescription",
    "/Script/NavigationSystem",
    "/Script/PhysicsCore",
    "/Script/StaticMeshDescription",
    "/Script/UnrealEd",
}
BP_PACKAGE = "/Game/Procedural/DoorToLevel"
BP_OBJECT = BP_PACKAGE + ".DoorToLevel"
BP_GENERATED_CLASS = BP_PACKAGE + ".DoorToLevel_C"
BP_RELATIVE = "procedural/doortolevel.uasset"
PARENT_CLASS_PATH = "/Script/EFLevelFlowRuntime.ProjectLevelDoor"
MAP_PACKAGE = "/Game/Procedural/Maps/DungeonGeneration"
MAP_OBJECT = MAP_PACKAGE + ".DungeonGeneration"
MESH_PACKAGE = "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors"
QUEST_COMPONENT_CLASS_PATH = (
    "/AscentCombatFramework/Integrations/Components/"
    "ACFQuestTargetComponentBP.ACFQuestTargetComponentBP_C"
)
QUEST_COMPONENT_PACKAGE = (
    "/AscentCombatFramework/Integrations/Components/ACFQuestTargetComponentBP"
)
MAP_MARKER_CLASS_PATH = "/Script/AscentMapsSystem.AMSMapMarkerComponent"
EXPECTED_COMPONENT_CLASSES = {
    "SceneComponent": 1,
    "ACFInteractableComponent": 1,
    "StaticMeshComponent": 1,
    "SphereComponent": 1,
    "ACFQuestTargetComponentBP_C": 1,
    "AMSMapMarkerComponent": 1,
}
EXPECTED_COMPONENT_NAMES = {
    "SceneRoot",
    "InteractableComponent",
    "StaticMesh",
    "Sphere",
    "QuestTargetComponent",
    "MapMarkerComponent",
}
INHERITED_COMPONENT_CLASSES = {
    "SceneComponent": 1,
    "ACFInteractableComponent": 1,
    "StaticMeshComponent": 1,
    "SphereComponent": 1,
}
INHERITED_COMPONENT_NAMES = {
    "SceneRoot",
    "InteractableComponent",
    "StaticMesh",
    "Sphere",
}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")
RUN_ID = os.environ.get("CODEX_DOOR_RUN_ID", "").strip()
EXPECTED_RUN_ROOT = os.environ.get("CODEX_DOOR_RUN_ROOT", "").strip()
RESUME_AFTER_FAILED_RESAVE = (
    os.environ.get("CODEX_DOOR_RESUME_AFTER_FAILED_RESAVE", "").strip() == "1"
)
RESUME_EXISTING_BLUEPRINT = (
    os.environ.get("CODEX_DOOR_RESUME_EXISTING_BLUEPRINT", "").strip() == "1"
)


def fail(message):
    unreal.log_error("CODEX_DOOR_TO_LEVEL58_REBUILD_FAIL: " + message)
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


def package_file(content_root, package_name, extension=".uasset"):
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + extension,
        )
    )


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
                "sha256": sha256(path),
            }
    return rows


def snapshot_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(name, rows[name]["length"], rows[name]["sha256"])
        for name in sorted(rows)
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def target_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(package, rows[package]["length"], rows[package]["sha256"])
        for package in sorted(rows)
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


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


def registry_dependencies(registry, package_name):
    return sorted(
        {str(value) for value in registry.get_dependencies(package_name, dependency_options())}
    )


def normalize_class_tag(value):
    text = str(value or "")
    if "'" in text:
        pieces = text.split("'")
        if len(pieces) >= 2:
            text = pieces[1]
    if text.startswith("Script/"):
        text = "/" + text
    return text


def normalize_component_name(name):
    text = str(name)
    suffix = "_GEN_VARIABLE"
    return text[: -len(suffix)] if text.endswith(suffix) else text


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def dirty_content_packages():
    try:
        packages = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    except Exception as exc:
        fail("Dirty-content probe failed closed: {!r}".format(exc))
    names = []
    for package in packages:
        try:
            names.append(package.get_name())
        except Exception as exc:
            fail("Could not name a dirty content package: {!r}".format(exc))
    return sorted(set(names))


def gather_subobjects(blueprint):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if subsystem is None:
        fail("SubobjectDataSubsystem is unavailable")
    handles = list(subsystem.k2_gather_subobject_data_for_blueprint(blueprint))
    if not handles:
        fail("SubobjectDataSubsystem returned no Blueprint handles")
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    rows = []
    root_handle = None
    scene_root_handle = None
    for handle in handles:
        data = library.get_data(handle)
        variable_name = str(library.get_variable_name(data))
        display_name = str(library.get_display_name(data))
        if library.is_root_actor(data):
            root_handle = handle
        if variable_name == "SceneRoot" or display_name == "SceneRoot":
            scene_root_handle = handle
        associated = library.get_object_for_blueprint(data, blueprint)
        rows.append(
            {
                "variable_name": variable_name,
                "display_name": display_name,
                "class": associated.get_class().get_name() if associated else "",
                "is_root_actor": bool(library.is_root_actor(data)),
                "is_component": bool(library.is_component(data)),
                "is_inherited": bool(library.is_inherited_component(data)),
            }
        )
    if root_handle is None:
        fail("Blueprint root actor subobject handle is absent")
    if scene_root_handle is None:
        fail("Inherited SceneRoot subobject handle is absent")
    return subsystem, root_handle, scene_root_handle, rows


def add_blueprint_component(blueprint, component_class, parent_handle, name):
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", parent_handle)
    params.set_editor_property("new_class", component_class)
    params.set_editor_property("blueprint_context", blueprint)
    result = subsystem.add_new_subobject(params)
    if isinstance(result, tuple):
        handle = result[0]
        reason = str(result[1]) if len(result) > 1 else ""
    else:
        handle = result
        reason = ""
    data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle)
    if not unreal.SubobjectDataBlueprintFunctionLibrary.is_valid(data):
        fail("Failed to add {}: {}".format(name, reason))
    if not subsystem.rename_subobject(handle, name):
        fail("Failed to rename new component to " + name)
    return handle


def blueprint_status(blueprint):
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in status.upper() if character.isalnum())
    if "UPTODATE" not in normalized:
        fail("DoorToLevel compile status is " + status)
    return status


def inspect_blueprint(blueprint, expected_mesh, require_project_components=True):
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        fail("DoorToLevel is not a Blueprint asset")
    event_graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
    if event_graph is not None:
        fail("DoorToLevel owns an EventGraph; native AProjectLevelDoor must be the only travel owner")
    graph_rows = []
    graph_names = []
    for graph in unreal.BlueprintEditorLibrary.list_graphs(blueprint):
        name = graph.get_name()
        graph_names.append(name)
        graph_rows.append(
            {
                "name": name,
                "node_count": "UNAVAILABLE_PROTECTED_API",
                "node_classes": [],
            }
        )
    if set(graph_names) - {"ConstructionScript", "UserConstructionScript"}:
        fail("Thin DoorToLevel contains unexpected graphs: " + repr(graph_names))

    generated_class = unreal.load_class(None, BP_GENERATED_CLASS)
    parent_class = unreal.load_class(None, PARENT_CLASS_PATH)
    if generated_class is None or parent_class is None:
        fail("DoorToLevel generated or native parent class could not load")
    if not unreal.MathLibrary.class_is_child_of(generated_class, parent_class):
        fail("DoorToLevel_C is not a child of AProjectLevelDoor")
    cdo = unreal.get_default_object(generated_class)
    if cdo is None:
        fail("DoorToLevel generated-class CDO is absent")

    components = list(cdo.get_components_by_class(unreal.ActorComponent))
    component_rows = []
    class_counts = {}
    names = set()
    by_class = {}
    for component in components:
        component_class = component.get_class().get_name()
        component_name = normalize_component_name(component.get_name())
        class_counts[component_class] = class_counts.get(component_class, 0) + 1
        names.add(component_name)
        by_class.setdefault(component_class, []).append(component)
        attach_parent = ""
        if component_class in {
            "SceneComponent",
            "StaticMeshComponent",
            "SphereComponent",
            "AMSMapMarkerComponent",
        }:
            try:
                attach_parent = normalize_component_name(
                    component.get_attach_parent().get_name()
                ) if component.get_attach_parent() else ""
            except Exception:
                attach_parent = ""
        component_rows.append(
            {
                "name": component_name,
                "raw_name": component.get_name(),
                "class": component_class,
                "attach_parent": attach_parent,
            }
        )
    if class_counts != INHERITED_COMPONENT_CLASSES:
        fail("DoorToLevel component class inventory differs: " + repr(class_counts))
    if names != INHERITED_COMPONENT_NAMES:
        fail("DoorToLevel component name inventory differs: " + repr(sorted(names)))
    subobject_rows = []
    if require_project_components:
        _subsystem, _root, _scene_root, subobject_rows = gather_subobjects(blueprint)
        subobject_pairs = {
            (
                row["variable_name"] or row["display_name"],
                row["class"],
            )
            for row in subobject_rows
        }
        required_pairs = {
            ("QuestTargetComponent", "ACFQuestTargetComponentBP_C"),
            ("MapMarkerComponent", "AMSMapMarkerComponent"),
        }
        if not required_pairs.issubset(subobject_pairs):
            fail("DoorToLevel SCS component inventory differs: " + repr(subobject_rows))
    for component_class in ("StaticMeshComponent", "SphereComponent"):
        row = next(item for item in component_rows if item["class"] == component_class)
        if row["attach_parent"] != "SceneRoot":
            fail("{} is not attached to SceneRoot".format(component_class))

    static_mesh_component = by_class["StaticMeshComponent"][0]
    actual_mesh = static_mesh_component.get_editor_property("static_mesh")
    if actual_mesh is None or actual_mesh.get_path_name() != expected_mesh.get_path_name():
        fail("DoorToLevel StaticMesh component does not use the audited door mesh")
    sphere = by_class["SphereComponent"][0]
    if abs(float(sphere.get_unscaled_sphere_radius()) - 180.0) > 0.01:
        fail("DoorToLevel Sphere radius differs from the native 180 cm contract")

    interactable_name = str(cdo.get_editor_property("interactable_name"))
    destination = object_path(cdo.get_editor_property("destination_level"))
    if interactable_name != "Interact":
        fail("DoorToLevel InteractableName differs: " + interactable_name)
    if destination != MAP_OBJECT:
        fail("DoorToLevel DestinationLevel differs: " + destination)
    if cdo.get_editor_property("is_enabled") is not True:
        fail("DoorToLevel IsEnabled is not true")
    if cdo.get_editor_property("absolute_travel") is not True:
        fail("DoorToLevel bAbsoluteTravel is not true")
    if str(cdo.get_editor_property("travel_options")) != "":
        fail("DoorToLevel TravelOptions is not empty")

    return {
        "object_path": blueprint.get_path_name(),
        "generated_class": generated_class.get_path_name(),
        "native_parent_class": parent_class.get_path_name(),
        "compile_status": blueprint_status(blueprint),
        "event_graph_present": False,
        "graphs": graph_rows,
        "components": sorted(component_rows, key=lambda row: row["name"]),
        "component_class_counts": class_counts,
        "blueprint_subobjects": subobject_rows,
        "defaults": {
            "interactable_name": interactable_name,
            "is_enabled": True,
            "destination_level": destination,
            "absolute_travel": True,
            "travel_options": "",
            "sphere_radius": float(sphere.get_unscaled_sphere_radius()),
            "static_mesh": actual_mesh.get_path_name(),
        },
        "runtime_travel_owner": "AProjectLevelDoor::OnInteractedByPawn_Implementation",
    }


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not RUN_ID or not EXPECTED_RUN_ROOT:
    fail("CODEX_DOOR_RUN_ID and CODEX_DOOR_RUN_ROOT are required")
if not (8 <= len(RUN_ID) <= 64) or not all(
    character.isalnum() or character in "_-" for character in RUN_ID
):
    fail("DoorToLevel run_id is invalid")
project_file = os.path.realpath(unreal.Paths.get_project_file_path())
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
if content_root.lower() != os.path.realpath(os.path.join(project_root, "Content")).lower():
    fail("Project Content invariant failed")
if not is_under(content_root, project_root):
    fail("Project Content resolves outside the target project root")

phase_root = os.path.realpath(
    os.path.join(project_root, "Saved", "Migration", "Phase4", "DoorToLevel")
)
run_root = os.path.realpath(os.path.join(phase_root, "Runs", RUN_ID))
if run_root.lower() != os.path.realpath(EXPECTED_RUN_ROOT).lower():
    fail("DoorToLevel run root differs from the orchestrated immutable run")
if not is_under(run_root, os.path.join(phase_root, "Runs")):
    fail("DoorToLevel run root escapes the target migration evidence root")
receipt_path = os.path.join(run_root, "DoorToLevel57HarnessReceipt.json")
validation_path = os.path.join(run_root, "DoorToLevelVisualAssets57Validation.json")
migration_path = os.path.join(run_root, "DoorToLevelVisualAssets57Migration.json")
post_migration_gate_path = os.path.join(
    run_root, "Gates", "POST_MIGRATION57_SafetyGate.json"
)
evidence_path = os.path.join(run_root, "DoorToLevel58Rebuild.json")
blueprint_file = package_file(content_root, BP_PACKAGE)
for required in (
    receipt_path,
    validation_path,
    migration_path,
    post_migration_gate_path,
):
    if not os.path.isfile(required):
        fail("Required migration input is absent: " + required)
if os.path.exists(evidence_path):
    fail("UE 5.8 rebuild evidence already exists for this immutable run")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
with open(validation_path, "r", encoding="utf-8-sig") as handle:
    validation = json.load(handle)
with open(migration_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
with open(post_migration_gate_path, "r", encoding="utf-8-sig") as handle:
    post_migration_gate = json.load(handle)
receipt_sha256 = sha256(receipt_path)
validation_sha256 = sha256(validation_path)
migration_sha256 = sha256(migration_path)
post_migration_gate_sha256 = sha256(post_migration_gate_path)
if (
    receipt.get("status") != "ISOLATED_DOOR_VISUAL57_HARNESS_PASS"
    or receipt.get("run_id") != RUN_ID
    or receipt.get("source_fingerprint") != SOURCE_FINGERPRINT
    or receipt.get("legacy_blueprint_reference", {}).get("migration_requested") is not False
):
    fail("DoorToLevel harness receipt is not the approved rebuild contract")
if (
    os.path.realpath(receipt.get("target_root", "")).lower() != project_root.lower()
    or os.path.realpath(receipt.get("run_root", "")).lower() != run_root.lower()
):
    fail("DoorToLevel receipt roots differ from the current target run")
if (
    validation.get("status") != "UE57_DOOR_VISUAL_READ_ONLY_LOAD_PASS"
    or validation.get("run_id") != RUN_ID
    or validation.get("receipt_sha256") != receipt_sha256
):
    fail("UE 5.7 validation evidence chain differs")
if (
    migration.get("status") != "ASSETTOOLS_EXACT_DOOR_VISUAL_MIGRATION_PASS"
    or migration.get("run_id") != RUN_ID
    or migration.get("package_count") != VISUAL_PACKAGE_COUNT
    or migration.get("source_bytes") != SOURCE_BYTES
    or migration.get("source_fingerprint") != SOURCE_FINGERPRINT
    or migration.get("target_delta_exact") is not True
    or migration.get("legacy_blueprint_migrated") is not False
    or migration.get("global_dirty_save_gate") != "PASS_EMPTY_CONTENT_AND_MAPS"
    or migration.get("dirty_content_packages_immediately_before_migration") != []
    or migration.get("dirty_map_packages_immediately_before_migration") != []
    or migration.get("dirty_content_packages_immediately_after_migration") != []
    or migration.get("dirty_map_packages_immediately_after_migration") != []
    or migration.get("receipt_sha256") != receipt_sha256
    or migration.get("validation_sha256") != validation_sha256
):
    fail("UE 5.7 exact visual migration evidence is not PASS")
if (
    post_migration_gate.get("status")
    != "DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS"
    or post_migration_gate.get("stage") != "POST_MIGRATION57"
    or post_migration_gate.get("run_id") != RUN_ID
):
    fail("POST_MIGRATION57 source/protected gate is not PASS")
for label, node_name in (
    ("source", "source_read_only"),
    ("protected", "protected_invariants"),
):
    node = post_migration_gate.get(node_name, {})
    nested_path = os.path.realpath(node.get("evidence", ""))
    if (
        not os.path.isfile(nested_path)
        or node.get("evidence_sha256") != sha256(nested_path)
    ):
        fail("POST_MIGRATION57 nested {} evidence hash differs".format(label))
post_chain = post_migration_gate.get("input_chain", {})
if (
    post_chain.get("receipt", {}).get("sha256") != receipt_sha256
    or post_chain.get("validation", {}).get("sha256") != validation_sha256
    or post_chain.get("migration", {}).get("sha256") != migration_sha256
    or migration.get("pre_migration_gate_sha256")
    != post_chain.get("pre_migration_gate", {}).get("sha256")
):
    fail("POST_MIGRATION57 chained hashes differ from the current run inputs")
blueprint_already_exists = (
    os.path.exists(blueprint_file)
    or unreal.EditorAssetLibrary.does_asset_exist(BP_PACKAGE)
)
if blueprint_already_exists and not RESUME_EXISTING_BLUEPRINT:
    fail("Target DoorToLevel already exists; refusing to overwrite a live Blueprint")
if RESUME_EXISTING_BLUEPRINT and not blueprint_already_exists:
    fail("Requested DoorToLevel Blueprint resume, but the asset is absent")

migration_rows = {row["package"]: row for row in migration.get("packages", [])}
if set(migration_rows) != set(VISUALS):
    fail("UE 5.7 migration package set differs from the visual allowlist")
visual_input_rows = []
for package in sorted(VISUALS):
    path = package_file(content_root, package)
    row = migration_rows[package]
    matches_migration_evidence = (
        os.path.isfile(path)
        and os.path.getsize(path) == row.get("length")
        and sha256(path) == row.get("sha256")
    )
    if not matches_migration_evidence and not RESUME_AFTER_FAILED_RESAVE:
        fail("Target visual asset differs from UE 5.7 evidence: " + package)
    if not os.path.isfile(path):
        fail("Target visual asset is absent: " + package)
    visual_input_rows.append(
        {
            "package": package,
            "path": path,
            "length": os.path.getsize(path),
            "sha256": sha256(path),
            "matches_ue57_migration_evidence": matches_migration_evidence,
        }
    )
    if not matches_migration_evidence:
        unreal.log_warning(
            "Resuming after the prior failed UE 5.8 resave for " + package
        )

legacy_source = os.path.realpath(
    receipt.get("legacy_blueprint_reference", {}).get("source", "")
)
if (
    not os.path.isfile(legacy_source)
    or os.path.getsize(legacy_source) != 53280
    or sha256(legacy_source)
    != "7EDF9F4A24D14F03AF2AE3F6A111696CF4AAC79052C225BCE90429D06935D016"
):
    fail("Legacy DoorToLevel read-only reference differs from the frozen baseline")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    [
        "/Game/Calysto/Dungeon/Demo/LowPoly",
        "/Game/Calysto/Dungeon/Mesh/DungeonMesh",
        "/Game/Procedural",
    ],
    True,
)
registry.wait_for_completion()

visual_dependencies_before = {}
loaded_visuals = {}
for package in LOAD_ORDER:
    expected = VISUALS[package]
    assets = [
        data
        for data in registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
        if class_name(data) == expected["class"]
    ]
    if len(assets) != 1:
        fail("Expected one registered {} for {}".format(expected["class"], package))
    dependencies = registry_dependencies(registry, package)
    game_dependencies = {value for value in dependencies if value.startswith("/Game/")}
    external_dependencies = set(dependencies) - game_dependencies
    if game_dependencies != expected["game_dependencies"]:
        fail("UE 5.8 pre-resave /Game dependencies differ for " + package)
    if external_dependencies - ALLOWED_VISUAL_EXTERNAL_DEPENDENCIES:
        fail("Unexpected external dependencies for {}: {!r}".format(
            package, sorted(external_dependencies - ALLOWED_VISUAL_EXTERNAL_DEPENDENCIES)
        ))
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None or asset.get_class().get_name() != expected["class"]:
        fail("UE 5.8 visual load/class gate failed for " + package)
    loaded_visuals[package] = asset
    visual_dependencies_before[package] = dependencies

dirty_before = dirty_content_packages()
allowed_dirty_before = set(VISUALS)
unexpected_dirty_before = sorted(set(dirty_before) - allowed_dirty_before)
if unexpected_dirty_before:
    fail(
        "Unrelated or protected content packages are dirty before rebuild: "
        + repr(unexpected_dirty_before)
    )
inventory_before = snapshot_package_files(content_root)
inventory_fingerprint_before = snapshot_fingerprint(inventory_before)

compile_rows = []
for package in (
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal",
):
    try:
        unreal.MaterialEditingLibrary.recompile_material(loaded_visuals[package])
    except Exception as exc:
        fail("Material recompile failed for {}: {!r}".format(package, exc))
    compile_rows.append(
        {
            "package": package,
            "api": "MaterialEditingLibrary.recompile_material",
            "result": "NO_EXCEPTION",
        }
    )

visual_save_rows = []
for package in LOAD_ORDER:
    path = package_file(content_root, package)
    before_hash = sha256(path)
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("UE 5.8 visual save failed: " + package)
    visual_save_rows.append(
        {
            "package": package,
            "sha256_before_resave": before_hash,
            "length_after_resave": os.path.getsize(path),
            "sha256_after_resave": sha256(path),
        }
    )

parent_class = unreal.load_class(None, PARENT_CLASS_PATH)
quest_component_class = unreal.load_class(None, QUEST_COMPONENT_CLASS_PATH)
map_marker_class = unreal.load_class(None, MAP_MARKER_CLASS_PATH)
if parent_class is None:
    fail("AProjectLevelDoor native parent class could not load")
if quest_component_class is None:
    fail("Current ACFU quest-target component class could not load")
if map_marker_class is None:
    fail("Current ACFU map-marker component class could not load")

if RESUME_EXISTING_BLUEPRINT:
    blueprint = unreal.EditorAssetLibrary.load_asset(BP_PACKAGE)
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        fail("Existing /Game/Procedural/DoorToLevel could not be loaded as a Blueprint")
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DoorToLevel", "/Game/Procedural", None, factory
    )
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        fail("BlueprintFactory failed to create /Game/Procedural/DoorToLevel")

event_graph = unreal.BlueprintEditorLibrary.find_event_graph(blueprint)
if event_graph is not None:
    unreal.BlueprintEditorLibrary.remove_graph(blueprint, event_graph)
if unreal.BlueprintEditorLibrary.find_event_graph(blueprint) is not None:
    fail("Could not remove the generated DoorToLevel EventGraph")

subsystem, root_handle, scene_root_handle, subobjects_before = gather_subobjects(
    blueprint
)
if not RESUME_EXISTING_BLUEPRINT:
    add_blueprint_component(
        blueprint, quest_component_class, root_handle, "QuestTargetComponent"
    )
    add_blueprint_component(
        blueprint, map_marker_class, scene_root_handle, "MapMarkerComponent"
    )
compile_result = unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
if compile_result is False:
    fail("DoorToLevel Blueprint compilation returned false")
blueprint_status(blueprint)

generated_class = unreal.load_class(None, BP_GENERATED_CLASS)
if generated_class is None:
    fail("DoorToLevel generated class did not load after compilation")
cdo = unreal.get_default_object(generated_class)
if cdo is None:
    fail("DoorToLevel generated-class CDO is absent")
destination_world = unreal.EditorAssetLibrary.load_asset(MAP_PACKAGE)
door_mesh = loaded_visuals[MESH_PACKAGE]
if destination_world is None or destination_world.get_class().get_name() != "World":
    fail("DungeonGeneration World is absent or invalid")
if door_mesh is None or door_mesh.get_class().get_name() != "StaticMesh":
    fail("Audited door StaticMesh is absent or invalid")

blueprint.modify()
cdo.modify()
cdo.set_editor_property("interactable_name", "Interact")
cdo.set_editor_property("is_enabled", True)
cdo.set_editor_property("destination_level", destination_world)
cdo.set_editor_property("absolute_travel", True)
cdo.set_editor_property("travel_options", "")
static_mesh_components = list(cdo.get_components_by_class(unreal.StaticMeshComponent))
if len(static_mesh_components) != 1:
    fail("DoorToLevel CDO does not contain exactly one StaticMeshComponent")
current_mesh = static_mesh_components[0].get_editor_property("static_mesh")
if current_mesh is None or current_mesh.get_path_name() != door_mesh.get_path_name():
    if static_mesh_components[0].set_static_mesh(door_mesh) is False:
        fail("Setting the audited door StaticMesh returned false")

subsystem, _root_handle, _scene_root_handle, subobjects_after = gather_subobjects(
    blueprint
)
blueprint_before_save = inspect_blueprint(
    blueprint,
    door_mesh,
    require_project_components=RESUME_EXISTING_BLUEPRINT,
)
if not unreal.EditorAssetLibrary.save_asset(BP_PACKAGE, only_if_is_dirty=False):
    fail("UE 5.8 DoorToLevel Blueprint save failed")
if not os.path.isfile(blueprint_file):
    fail("DoorToLevel Blueprint package file was not created")

inventory_after_save = snapshot_package_files(content_root)
inventory_fingerprint_after_save = snapshot_fingerprint(inventory_after_save)
created = sorted(set(inventory_after_save) - set(inventory_before))
removed = sorted(set(inventory_before) - set(inventory_after_save))
modified = sorted(
    name
    for name in set(inventory_before).intersection(inventory_after_save)
    if inventory_before[name] != inventory_after_save[name]
)
expected_created = [] if RESUME_EXISTING_BLUEPRINT else [BP_RELATIVE]
expected_modified = sorted(value["relative"] for value in VISUALS.values())
if RESUME_EXISTING_BLUEPRINT:
    expected_modified.append(BP_RELATIVE)
    expected_modified.sort()
if created != expected_created or removed or modified != expected_modified:
    fail(
        "UE 5.8 DoorToLevel delta is not exact; created={!r}, removed={!r}, "
        "modified={!r}".format(created, removed, modified)
    )

target_files = [package_file(content_root, package) for package in VISUALS]
target_files.append(blueprint_file)
for path in target_files:
    for extension in (".uexp", ".ubulk", ".uptnl"):
        if os.path.exists(os.path.splitext(path)[0] + extension):
            fail("Unexpected target sidecar exists: " + path + extension)

try:
    registry.scan_modified_asset_files(target_files)
except Exception:
    registry.scan_paths_synchronous(
        [
            "/Game/Calysto/Dungeon/Demo/LowPoly",
            "/Game/Calysto/Dungeon/Mesh/DungeonMesh",
            "/Game/Procedural",
        ],
        True,
    )
registry.wait_for_completion()

visual_dependencies_after = {}
for package in sorted(VISUALS):
    dependencies = registry_dependencies(registry, package)
    game_dependencies = {value for value in dependencies if value.startswith("/Game/")}
    external_dependencies = set(dependencies) - game_dependencies
    if game_dependencies != VISUALS[package]["game_dependencies"]:
        fail("UE 5.8 post-resave /Game dependencies differ for " + package)
    if external_dependencies - ALLOWED_VISUAL_EXTERNAL_DEPENDENCIES:
        fail("Unexpected post-resave external dependencies for " + package)
    visual_dependencies_after[package] = dependencies

bp_asset_data = [
    data
    for data in registry.get_assets_by_package_name(
        BP_PACKAGE, include_only_on_disk_assets=True
    )
    if class_name(data) == "Blueprint"
]
if len(bp_asset_data) != 1:
    fail("Asset Registry does not contain exactly one DoorToLevel Blueprint")
native_parent = normalize_class_tag(bp_asset_data[0].get_tag_value("NativeParentClass"))
parent_tag = normalize_class_tag(bp_asset_data[0].get_tag_value("ParentClass"))
generated_tag = normalize_class_tag(bp_asset_data[0].get_tag_value("GeneratedClass"))
if native_parent != PARENT_CLASS_PATH or parent_tag != PARENT_CLASS_PATH:
    fail("DoorToLevel Asset Registry parent tags differ: {}, {}".format(
        native_parent, parent_tag
    ))
if generated_tag != BP_GENERATED_CLASS:
    fail("DoorToLevel generated-class tag differs: " + generated_tag)
bp_dependencies_before_reload = registry_dependencies(registry, BP_PACKAGE)
bp_game_dependencies = {
    value for value in bp_dependencies_before_reload if value.startswith("/Game/")
}
if bp_game_dependencies != {MESH_PACKAGE}:
    fail("DoorToLevel /Game dependency closure differs: " + repr(sorted(bp_game_dependencies)))
if QUEST_COMPONENT_PACKAGE not in bp_dependencies_before_reload:
    fail("DoorToLevel does not reference the current ACFU quest-target component")
for forbidden in (
    "/Game/FullSample/Integrations/ACFBaseInteractableBP",
    "/Game/FullSample/Integrations/ATSIntegrations/ACFQuestTargetComponentBP",
):
    if forbidden in bp_dependencies_before_reload:
        fail("DoorToLevel retained a forbidden legacy FullSample dependency: " + forbidden)

packages_to_reload = []
for asset in list(loaded_visuals.values()) + [blueprint]:
    try:
        package = asset.get_outermost()
    except Exception:
        package = asset
        while package is not None and package.get_outer() is not None:
            package = package.get_outer()
    if package is None:
        fail("Could not resolve a package for reload")
    if package not in packages_to_reload:
        packages_to_reload.append(package)

reload_result = unreal.EditorLoadingAndSavingUtils.reload_packages(
    packages_to_reload,
    unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE,
)
if isinstance(reload_result, tuple):
    any_reloaded = bool(reload_result[0])
    reload_error = str(reload_result[1]) if len(reload_result) > 1 else ""
else:
    any_reloaded = bool(reload_result)
    reload_error = ""
if not any_reloaded:
    fail("UE 5.8 package reload reported no reload: " + reload_error)

registry.scan_paths_synchronous(
    [
        "/Game/Calysto/Dungeon/Demo/LowPoly",
        "/Game/Calysto/Dungeon/Mesh/DungeonMesh",
        "/Game/Procedural",
    ],
    True,
)
registry.wait_for_completion()
reloaded_visuals = {}
for package in LOAD_ORDER:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None or asset.get_class().get_name() != VISUALS[package]["class"]:
        fail("Post-resave visual reload failed: " + package)
    reloaded_visuals[package] = asset
reloaded_blueprint = unreal.EditorAssetLibrary.load_asset(BP_PACKAGE)
blueprint_after_reload = inspect_blueprint(
    reloaded_blueprint, reloaded_visuals[MESH_PACKAGE]
)

inventory_after_reload = snapshot_package_files(content_root)
if inventory_after_reload != inventory_after_save:
    fail("Package reload changed target Content inventory")
dirty_after = dirty_content_packages()
target_package_names = set(VISUALS) | {BP_PACKAGE}
dirty_target_packages = sorted(target_package_names.intersection(dirty_after))
if dirty_target_packages:
    fail("Resaved DoorToLevel packages remain dirty: " + repr(dirty_target_packages))
new_dirty_packages = sorted(set(dirty_after) - set(dirty_before))
unexpected_dirty_after = sorted(set(dirty_after) - target_package_names)
if new_dirty_packages or unexpected_dirty_after:
    fail(
        "Rebuild dirtied unrelated or protected packages; new={!r}, unrelated={!r}".format(
            new_dirty_packages, unexpected_dirty_after
        )
    )

bp_dependencies_after_reload = registry_dependencies(registry, BP_PACKAGE)
if bp_dependencies_after_reload != bp_dependencies_before_reload:
    fail("DoorToLevel dependencies changed across package reload")
visual_dependencies_reloaded = {
    package: registry_dependencies(registry, package) for package in sorted(VISUALS)
}
if visual_dependencies_reloaded != visual_dependencies_after:
    fail("Visual dependencies changed across package reload")

source_rows = {
    row["package"]: row for row in receipt.get("staged_assets", [])
}
for package in sorted(VISUALS):
    source_file = os.path.realpath(source_rows[package]["source"])
    expected = VISUALS[package]
    if (
        os.path.getsize(source_file) != expected["source_length"]
        or sha256(source_file) != expected["source_sha256"]
    ):
        fail("Source visual asset changed during UE 5.8 rebuild: " + package)
if os.path.getsize(legacy_source) != 53280 or sha256(legacy_source) != (
    "7EDF9F4A24D14F03AF2AE3F6A111696CF4AAC79052C225BCE90429D06935D016"
):
    fail("Legacy DoorToLevel read-only reference changed during UE 5.8 rebuild")

save_by_package = {row["package"]: row for row in visual_save_rows}
target_rows = {}
target_packages = []
for package in sorted(VISUALS):
    path = package_file(content_root, package)
    row = {
        "package": package,
        "class": VISUALS[package]["class"],
        "file": path,
        "length": os.path.getsize(path),
        "sha256": sha256(path),
        "source_length": VISUALS[package]["source_length"],
        "source_sha256": VISUALS[package]["source_sha256"],
        "migration_length": migration_rows[package]["length"],
        "migration_sha256": migration_rows[package]["sha256"],
        "sha256_before_resave": save_by_package[package]["sha256_before_resave"],
    }
    target_packages.append(row)
    target_rows[package] = {"length": row["length"], "sha256": row["sha256"]}
target_packages.append(
    {
        "package": BP_PACKAGE,
        "class": "Blueprint",
        "file": blueprint_file,
        "length": os.path.getsize(blueprint_file),
        "sha256": sha256(blueprint_file),
        "legacy_source_length": 53280,
        "legacy_source_sha256": "7EDF9F4A24D14F03AF2AE3F6A111696CF4AAC79052C225BCE90429D06935D016",
        "rebuilt_not_migrated": True,
    }
)

payload = {
    "schema_version": 2,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_DOOR_TO_LEVEL_REBUILD_RESAVE_RELOAD_PASS",
    "run_id": RUN_ID,
    "engine_version": engine_version,
    "project": project_file,
    "run_root": run_root,
    "receipt": receipt_path,
    "receipt_sha256": receipt_sha256,
    "validation": validation_path,
    "validation_sha256": validation_sha256,
    "migration": migration_path,
    "migration_sha256": migration_sha256,
    "post_migration_gate": post_migration_gate_path,
    "post_migration_gate_sha256": post_migration_gate_sha256,
    "visual_package_count": VISUAL_PACKAGE_COUNT,
    "target_package_count": VISUAL_PACKAGE_COUNT + 1,
    "source_bytes": SOURCE_BYTES,
    "source_fingerprint": SOURCE_FINGERPRINT,
    "resume_after_failed_resave": RESUME_AFTER_FAILED_RESAVE,
    "resume_existing_blueprint": RESUME_EXISTING_BLUEPRINT,
    "visual_inputs_before_resave": visual_input_rows,
    "visual_target_fingerprint": target_fingerprint(target_rows),
    "target_packages": target_packages,
    "material_compile_operations": compile_rows,
    "visual_dependencies_before_resave": visual_dependencies_before,
    "visual_dependencies_after_resave": visual_dependencies_after,
    "visual_dependencies_after_reload": visual_dependencies_reloaded,
    "blueprint_dependencies": bp_dependencies_after_reload,
    "blueprint_game_dependencies": sorted(bp_game_dependencies),
    "blueprint_before_save": blueprint_before_save,
    "blueprint_after_reload": blueprint_after_reload,
    "subobjects_before_project_components": subobjects_before,
    "subobjects_after_project_components": subobjects_after,
    "native_parent_class": PARENT_CLASS_PATH,
    "current_quest_component_class": QUEST_COMPONENT_CLASS_PATH,
    "current_map_marker_class": MAP_MARKER_CLASS_PATH,
    "event_graph_present": False,
    "duplicate_travel_graph_present": False,
    "runtime_travel_owner": "AProjectLevelDoor::OnInteractedByPawn_Implementation",
    "legacy_blueprint_loaded": False,
    "legacy_blueprint_migrated": False,
    "legacy_fullsample_dependencies": [],
    "runtime_ubg_dependency": False,
    "package_reload_requested": True,
    "package_reload_result": "PASS",
    "package_reload_error": reload_error,
    "dirty_content_packages_before_load": dirty_before,
    "dirty_content_packages_after_reload": dirty_after,
    "dirty_content_probe_fail_closed": True,
    "allowed_dirty_packages_before_rebuild": sorted(allowed_dirty_before),
    "unexpected_dirty_packages_before_rebuild": unexpected_dirty_before,
    "new_dirty_packages_after_rebuild": new_dirty_packages,
    "unexpected_dirty_packages_after_rebuild": unexpected_dirty_after,
    "dirty_target_packages_after_reload": [],
    "target_inventory_algorithm": "lowercase relative package path|length|sha256; mtime_ns retained as auxiliary evidence",
    "target_package_file_count_before": len(inventory_before),
    "target_package_file_count_after": len(inventory_after_save),
    "target_inventory_fingerprint_before": inventory_fingerprint_before,
    "target_inventory_fingerprint_after": inventory_fingerprint_after_save,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_package_files": modified,
    "target_delta_exact": True,
    "target_sidecars_created": [],
    "source_assets_unchanged": True,
    "legacy_source_blueprint_unchanged": True,
    "post_migration_source_protected_gate": "PASS",
    "post_resave_source_protected_gate": "PENDING_EXTERNAL_GATE",
    "pie_requested": False,
    "visual_qa_requested": False,
    "cook_requested": False,
    "package_requested": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DOOR_TO_LEVEL58_REBUILD_PASS: " + evidence_path)
