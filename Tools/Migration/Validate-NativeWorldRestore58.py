"""Upgrade and structurally validate the project-owned Altar/Story repair in UE 5.8."""

import json
import os
import traceback

import unreal


ALTAR_BLUEPRINT = "/Game/Procedural/Blueprints/Altar"
ALTAR_MESHES = {
    "/Game/Fantastic_Dungeon_Pack/meshes/props/furniture/SM_PROP_altar_dungeon_01":
        "/Game/Fantastic_Dungeon_Pack/materials/MI_PROP_stone_deco_dungeon",
    "/Game/Fantastic_Dungeon_Pack/meshes/props/small_deco/SM_PROP_book_dungeon_07":
        "/Game/Fantastic_Dungeon_Pack/materials/MI_PROP_books_dungeon",
    "/Game/Fantastic_Dungeon_Pack/meshes/props/fabrics/SM_PROP_altar_cloth_dungeon_02":
        "/Game/Fantastic_Dungeon_Pack/materials/MI_PROP_fabric_dungeon",
}
ONECLICK_INSTANCES = (
    "/Game/_Game/Textures/Stone_Wall/Stone_Wall",
    "/Game/ShareTextures/Wall/1K/MI_Stone_Wall_21",
    "/Game/ShareTextures/Wall/1K/MI_Sand_Stone_Texture_3",
    "/Game/ShareTextures/Plaster/1K/MI_Plaster_Sand",
    "/Game/ShareTextures/Ground/1K/MI_Snow_Covered_Ground",
    "/Game/ShareTextures/Ground/1K/MI_Ground_4",
)
EXPECTED_ONECLICK_PARENT = "/OneClickMaterials/MasterMaterials/M_BasicMaster.M_BasicMaster"


def path_name(obj):
    return obj.get_path_name() if obj else ""


def fail(message):
    raise RuntimeError(message)


manifest_file = os.path.realpath(os.environ.get("CODEX_NATIVE_WORLD_MANIFEST", ""))
receipt_file = os.path.realpath(os.environ.get("CODEX_NATIVE_WORLD_VALIDATE_RECEIPT", ""))

try:
    if not os.path.isfile(manifest_file):
        fail("Restore manifest is absent: " + manifest_file)
    with open(manifest_file, "r", encoding="utf-8-sig") as handle:
        manifest = json.load(handle)
    packages = [row["package_name"] for row in manifest["packages"]]
    if len(packages) != 22:
        fail("Expected 22 restored packages; found {}".format(len(packages)))
    if any(package.startswith("/Game/Calysto/") for package in packages):
        fail("Calysto entered the UE 5.8 validation manifest")

    loaded = []
    for package in packages:
        asset = unreal.EditorAssetLibrary.load_asset(package)
        if not asset:
            fail("Restored asset failed to load: " + package)
        loaded.append(asset)

    oneclick_results = []
    for package in ONECLICK_INSTANCES:
        instance = unreal.EditorAssetLibrary.load_asset(package)
        if not instance:
            fail("OneClick material instance failed to load: " + package)
        parent = instance.get_editor_property("parent")
        parent_path = path_name(parent)
        if parent_path != EXPECTED_ONECLICK_PARENT:
            fail(
                "Material parent mismatch for {}: {}".format(package, parent_path)
            )
        oneclick_results.append({"package": package, "parent": parent_path})

    mesh_results = []
    for package, expected_material in ALTAR_MESHES.items():
        mesh = unreal.EditorAssetLibrary.load_asset(package)
        if not mesh:
            fail("Altar mesh failed to load: " + package)
        material_paths = [
            path_name(slot.get_editor_property("material_interface"))
            for slot in mesh.get_editor_property("static_materials")
        ]
        expected_object = expected_material + "." + expected_material.rsplit("/", 1)[-1]
        if expected_object not in material_paths:
            fail(
                "Altar mesh material mismatch for {}: {}".format(
                    package, material_paths
                )
            )
        mesh_results.append(
            {"package": package, "materials": material_paths}
        )

    altar_bp = unreal.EditorAssetLibrary.load_asset(ALTAR_BLUEPRINT)
    if not altar_bp:
        fail("Target Altar Blueprint failed to load")
    compile_result = unreal.BlueprintEditorLibrary.compile_blueprint(altar_bp)
    if compile_result is False:
        fail("Target Altar Blueprint compilation returned false")
    status = str(altar_bp.get_editor_property("status"))
    if "ERROR" in status.upper():
        fail("Target Altar Blueprint compile status is " + status)

    generated_class = altar_bp.generated_class()
    if not generated_class:
        fail("Target Altar Blueprint has no generated class")
    cdo = unreal.get_default_object(generated_class)
    static_components = cdo.get_components_by_class(unreal.StaticMeshComponent)
    component_meshes = {}
    component_transforms = {}
    for component in static_components:
        mesh_path = path_name(component.get_editor_property("static_mesh"))
        if mesh_path:
            component_meshes[component.get_name()] = mesh_path
            component_transforms[component.get_name()] = {
                "location": str(component.get_editor_property("relative_location")),
                "rotation": str(component.get_editor_property("relative_rotation")),
                "scale": str(component.get_editor_property("relative_scale3d")),
            }
    expected_mesh_objects = {
        package + "." + package.rsplit("/", 1)[-1] for package in ALTAR_MESHES
    }
    if not expected_mesh_objects.issubset(set(component_meshes.values())):
        fail(
            "Altar CDO does not contain all three restored meshes: "
            + repr(component_meshes)
        )
    sphere_components = cdo.get_components_by_class(unreal.SphereComponent)
    if not sphere_components:
        fail("Altar CDO has no native interaction sphere")
    sphere = sphere_components[0]
    radius = float(sphere.get_editor_property("sphere_radius"))
    if abs(radius - 32.0) > 0.01:
        fail("Altar interaction sphere radius differs: {}".format(radius))

    for asset in loaded:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            fail("Failed to UE 5.8-resave restored asset: " + path_name(asset))
    if not unreal.EditorAssetLibrary.save_loaded_asset(altar_bp, only_if_is_dirty=False):
        fail("Failed to save compiled Altar Blueprint")

    # Reload after save to catch reference stripping during UE 5.8 serialization.
    unreal.SystemLibrary.collect_garbage()
    post_save_parents = {}
    for package in ONECLICK_INSTANCES:
        instance = unreal.EditorAssetLibrary.load_asset(package)
        parent_path = path_name(instance.get_editor_property("parent")) if instance else ""
        post_save_parents[package] = parent_path
        if parent_path != EXPECTED_ONECLICK_PARENT:
            fail("UE 5.8 resave stripped parent from " + package)

    receipt = {
        "status": "PASS",
        "engine": unreal.SystemLibrary.get_engine_version(),
        "restored_package_count": len(packages),
        "calysto_packages_touched": [],
        "oneclick_instances": oneclick_results,
        "post_save_oneclick_parents": post_save_parents,
        "altar_meshes": mesh_results,
        "altar_blueprint_status": status,
        "altar_cdo_mesh_components": component_meshes,
        "altar_cdo_component_transforms": component_transforms,
        "altar_interaction_sphere_radius": radius,
    }
    os.makedirs(os.path.dirname(receipt_file), exist_ok=True)
    with open(receipt_file, "w", encoding="utf-8") as handle:
        json.dump(receipt, handle, indent=2, sort_keys=True)
    unreal.log("CODEX_NATIVE_WORLD_VALIDATE58_PASS")
except Exception as exc:
    unreal.log_error("CODEX_NATIVE_WORLD_VALIDATE58_FAIL: {}".format(exc))
    unreal.log_error(traceback.format_exc())
    if receipt_file:
        os.makedirs(os.path.dirname(receipt_file), exist_ok=True)
        with open(receipt_file, "w", encoding="utf-8") as handle:
            json.dump(
                {"status": "FAIL", "error": str(exc), "traceback": traceback.format_exc()},
                handle,
                indent=2,
                sort_keys=True,
            )
    raise
