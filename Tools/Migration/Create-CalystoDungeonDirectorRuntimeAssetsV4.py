"""Create/validate the V4 runtime Blueprint assets before policy authoring.

This script is create-only. Existing packages are validated read-only and are
never saved. A new package is saved once, and the complete Content byte delta
plus protected package hashes are written under Saved/Migration.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import traceback
from pathlib import Path

import unreal


EXPECTED_PROJECT_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
ITEM_FOLDER = "/Game/_Game/Items/Companions"
ITEM_NAME = "BP_Item_WintersRecall"
ITEM_PATH = f"{ITEM_FOLDER}/{ITEM_NAME}"
ITEM_CLASS_PATH = f"{ITEM_PATH}.{ITEM_NAME}_C"
ITEM_PARENT_PATH = "/Script/EFProjectSystemsGameplay.ProjectCompanionRevivalConsumable"
CHEST_FOLDER = "/Game/_Game/Items/Chests"
CHEST_PARENT_PATH = "/Script/EFProjectSystemsGameplay.ProjectCalystoChestV4"
CLOTHING_FOLDER = "/Game/_Game/Items/Clothing"
CLOTHING_PARENT_PATH = "/Script/InventorySystem.ACFWorldItem"
CLOTHING_ITEM_CLASS_PATH = (
    "/Game/FullSample/Blueprints/Items/Armor/ACFArmorBP.ACFArmorBP_C"
)
CLOTHING_WORLD_MESH_PATH = (
    "/Game/FullSample/Assets/Infinity_Blade_Assets/Meshes/Props/"
    "SM_TreasureBags02.SM_TreasureBags02"
)
BLUEPRINT_SPECS = (
    {
        "folder": ITEM_FOLDER,
        "name": ITEM_NAME,
        "path": ITEM_PATH,
        "class_path": ITEM_CLASS_PATH,
        "parent_path": ITEM_PARENT_PATH,
    },
    {
        "folder": CHEST_FOLDER,
        "name": "BP_CalystoLockedChestV4",
        "path": f"{CHEST_FOLDER}/BP_CalystoLockedChestV4",
        "class_path": (
            f"{CHEST_FOLDER}/BP_CalystoLockedChestV4."
            "BP_CalystoLockedChestV4_C"
        ),
        "parent_path": CHEST_PARENT_PATH,
    },
    {
        "folder": CHEST_FOLDER,
        "name": "BP_CalystoLockPickChestV4",
        "path": f"{CHEST_FOLDER}/BP_CalystoLockPickChestV4",
        "class_path": (
            f"{CHEST_FOLDER}/BP_CalystoLockPickChestV4."
            "BP_CalystoLockPickChestV4_C"
        ),
        "parent_path": CHEST_PARENT_PATH,
    },
    {
        "folder": CLOTHING_FOLDER,
        "name": "BP_CalystoArmorPickupV4",
        "path": f"{CLOTHING_FOLDER}/BP_CalystoArmorPickupV4",
        "class_path": (
            f"{CLOTHING_FOLDER}/BP_CalystoArmorPickupV4."
            "BP_CalystoArmorPickupV4_C"
        ),
        "parent_path": CLOTHING_PARENT_PATH,
        "pickup_item_class_path": CLOTHING_ITEM_CLASS_PATH,
        "pickup_item_count": 1,
        "pickup_mesh_path": CLOTHING_WORLD_MESH_PATH,
        "pickup_mesh_scale": 1.0,
    },
)

CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
RESULT_PATH = (
    Path(unreal.Paths.project_saved_dir())
    / "Migration"
    / "CalystoDungeonDirectorV4"
    / "CreateRuntimeAssetsV4.json"
)

PROTECTED_PACKAGES = {
    "BP_MassiveDungeon": CONTENT_ROOT / "Calysto" / "Dungeon" / "Blueprint" / "BP_MassiveDungeon.uasset",
    "BP_EndPoint": CONTENT_ROOT / "Calysto" / "Dungeon" / "Blueprint" / "Utility" / "BP_EndPoint.uasset",
    "BP_WallTorch": CONTENT_ROOT / "Calysto" / "Dungeon" / "Blueprint" / "Lightning" / "BP_WallTorch.uasset",
    "DA_DungeonMesh": CONTENT_ROOT / "Calysto" / "Dungeon" / "Data" / "DataAsset" / "Dungeon" / "DA_DungeonMesh.uasset",
    "DA_RoomTheme": CONTENT_ROOT / "Calysto" / "Dungeon" / "Data" / "DataAsset" / "Dungeon" / "DA_RoomTheme.uasset",
    "DA_DemoSpawner": CONTENT_ROOT / "Calysto" / "Dungeon" / "Data" / "DataAsset" / "Spawner" / "DA_DemoSpawner.uasset",
    "DA_RoomForge": CONTENT_ROOT / "Calysto" / "Dungeon" / "Data" / "DataAsset" / "Props" / "DA_RoomForge.uasset",
    "DA_RoomShrine": CONTENT_ROOT / "Calysto" / "Dungeon" / "Data" / "DataAsset" / "Props" / "DA_RoomShrine.uasset",
    "PCG_MassiveDungeonMaster": CONTENT_ROOT / "Calysto" / "Dungeon" / "PCG" / "PCG_MassiveDungeonMaster.uasset",
    "PCG_MassiveDungeonShape": CONTENT_ROOT / "Calysto" / "Dungeon" / "PCG" / "PCG_MassiveDungeonShape.uasset",
    "PCG_SpawnStartAndEnd": CONTENT_ROOT / "Calysto" / "Dungeon" / "PCG" / "Function" / "PCG_SpawnStartAndEnd.uasset",
    "PCG_SetRoomTheme": CONTENT_ROOT / "Calysto" / "Dungeon" / "PCG" / "Function" / "PCG_SetRoomTheme.uasset",
    "PCG_DungeonSpawner": CONTENT_ROOT / "Calysto" / "Dungeon" / "PCG" / "Function" / "PCG_DungeonSpawner.uasset",
    "PCG_SetDungeonMesh": CONTENT_ROOT / "Calysto" / "Dungeon" / "PCG" / "Function" / "PCG_SetDungeonMesh.uasset",
    "DungeonGeneration": CONTENT_ROOT / "Procedural" / "Maps" / "DungeonGeneration.umap",
    "Player": CONTENT_ROOT / "FullSample" / "Player.uasset",
    "Female": CONTENT_ROOT / "DazToUnreal" / "Female" / "Female.uasset",
    "Male": CONTENT_ROOT / "DazToUnreal" / "Male" / "Male.uasset",
    "Multiple": CONTENT_ROOT / "DazToUnreal" / "Multiple" / "Multiple.uasset",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def validate_execution_context() -> None:
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Dungeon Director V4 runtime assets require Unreal Engine 5.8")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail(f"Wrong Unreal project: {PROJECT_FILE}")
    if str(PROJECT_ROOT).casefold() != str(EXPECTED_PROJECT_ROOT).casefold():
        fail(
            "Refusing to run outside the writable NoShellForWinter target: "
            f"actual={PROJECT_ROOT} expected={EXPECTED_PROJECT_ROOT}"
        )
    expected_content_root = (EXPECTED_PROJECT_ROOT / "Content").resolve()
    if str(CONTENT_ROOT).casefold() != str(expected_content_root).casefold():
        fail(
            "Project Content root does not match the protected V4 target: "
            f"actual={CONTENT_ROOT} expected={expected_content_root}"
        )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def protected_snapshot() -> dict[str, dict]:
    result: dict[str, dict] = {}
    for name, path in PROTECTED_PACKAGES.items():
        result[name] = {
            "path": str(path),
            "exists": path.is_file(),
            "sha256": sha256(path) if path.is_file() else "",
        }
    # The migration baseline has never resolved an authoritative Frederick
    # package in this target. Preserve that fact explicitly instead of silently
    # omitting the named invariant or inventing a path/hash.
    result["Frederick"] = {
        "path": "",
        "exists": False,
        "sha256": "",
        "status": "PENDING_RUNTIME_IDENTITY",
    }
    return result


def content_snapshot() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    for pattern in ("*.uasset", "*.umap"):
        for path in CONTENT_ROOT.rglob(pattern):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size),
                int(stat.st_mtime_ns),
                sha256(path),
            )
    return result


def mutations(before, after) -> list[str]:
    return sorted(
        "/Game/" + relative.rsplit(".", 1)[0]
        for relative in set(before) | set(after)
        if before.get(relative) != after.get(relative)
    )


def package_is_dirty(asset) -> bool:
    package_name = asset.get_outermost().get_path_name()
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    dirty += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return any(package.get_path_name() == package_name for package in dirty)


def object_path(value) -> str:
    if value is None:
        return ""
    get_path_name = getattr(value, "get_path_name", None)
    return str(get_path_name()) if callable(get_path_name) else str(value)


def load_exact_class(class_path: str):
    loaded_class = unreal.load_class(None, class_path)
    if loaded_class is None:
        fail(f"Missing required class: {class_path}")
    if loaded_class.get_path_name() != class_path:
        fail(
            f"{class_path} resolved through a redirect to "
            f"{loaded_class.get_path_name()}"
        )
    return loaded_class


def validate_world_item_pickup(generated_class, spec: dict) -> dict:
    item_class_path = spec.get("pickup_item_class_path")
    if not item_class_path:
        return {}

    item_class = load_exact_class(item_class_path)
    cdo = unreal.get_default_object(generated_class)
    mesh_components = list(cdo.get_components_by_class(unreal.StaticMeshComponent))
    storage_components = [
        component
        for component in cdo.get_components_by_class(unreal.ActorComponent)
        if component.get_class().get_name() == "ACFStorageComponent"
    ]
    if len(mesh_components) != 1 or len(storage_components) != 1:
        fail(
            f"{spec['path']} must expose exactly one StaticMeshComponent and "
            "one ACFStorageComponent"
        )

    mesh_component = mesh_components[0]
    assigned_mesh_path = object_path(
        mesh_component.get_editor_property("static_mesh")
    )
    expected_mesh_path = spec["pickup_mesh_path"]
    if assigned_mesh_path != expected_mesh_path:
        fail(
            f"{spec['path']} world mesh is {assigned_mesh_path!r}; "
            f"expected {expected_mesh_path!r}"
        )

    expected_scale = float(spec["pickup_mesh_scale"])
    scale = mesh_component.get_editor_property("relative_scale3d")
    if any(
        abs(float(component) - expected_scale) > 0.0001
        for component in (scale.x, scale.y, scale.z)
    ):
        fail(
            f"{spec['path']} mesh scale is ({scale.x}, {scale.y}, {scale.z}); "
            f"expected uniform {expected_scale}"
        )

    storage_items = list(storage_components[0].get_editor_property("items"))
    if len(storage_items) != 1:
        fail(f"{spec['path']} must store exactly one clothing item entry")
    stored_class = storage_items[0].get_editor_property("item_class")
    stored_count = int(storage_items[0].get_editor_property("count"))
    expected_count = int(spec["pickup_item_count"])
    if object_path(stored_class) != object_path(item_class) or stored_count != expected_count:
        fail(
            f"{spec['path']} storage differs: class={object_path(stored_class)} "
            f"count={stored_count}; expected class={item_class_path} "
            f"count={expected_count}"
        )

    return {
        "pickup_item_class": item_class_path,
        "pickup_item_count": expected_count,
        "pickup_world_mesh": expected_mesh_path,
        "pickup_mesh_scale": expected_scale,
    }


def validate_blueprint(
    asset, spec: dict, parent_class, *, require_clean_package: bool = True
) -> dict:
    if asset is None:
        fail(f"Missing runtime V4 Blueprint: {spec['path']}")
    if asset.get_class().get_name() != "Blueprint":
        fail(f"{spec['path']} is not a Blueprint asset")
    if require_clean_package and package_is_dirty(asset):
        fail(
            f"{spec['path']} is dirty; refusing to validate or save existing data"
        )

    authored_parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(asset)
    if (
        authored_parent is None
        or authored_parent.get_path_name() != spec["parent_path"]
    ):
        actual_parent = (
            authored_parent.get_path_name() if authored_parent is not None else "None"
        )
        fail(
            f"{spec['path']} has parent {actual_parent}; expected exact parent "
            f"{spec['parent_path']}"
        )

    generated_class = unreal.load_class(None, spec["class_path"])
    if generated_class is None:
        fail(f"Missing generated class: {spec['class_path']}")
    if generated_class.get_path_name() != spec["class_path"]:
        fail(
            f"{spec['class_path']} resolved through a redirect to "
            f"{generated_class.get_path_name()}"
        )
    if not unreal.MathLibrary.class_is_child_of(generated_class, parent_class):
        fail(
            f"{spec['class_path']} is not derived from "
            f"{spec['parent_path']}"
        )
    validation = {
        "asset": spec["path"],
        "generated_class": generated_class.get_path_name(),
        "parent_class": parent_class.get_path_name(),
        "package_dirty": False,
    }
    validation.update(validate_world_item_pickup(generated_class, spec))
    return validation


validate_execution_context()
before_content = content_snapshot()
before_protected = protected_snapshot()
created_paths: list[str] = []
saved_paths: list[str] = []
result = {
    "schema_version": 4,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "mode": "validate_existing_read_only",
    "assets": [spec["path"] for spec in BLUEPRINT_SPECS],
    "asset_mutations": [],
    "asset_saves": [],
    "protected_before": before_protected,
    "status": "FAIL",
}

try:
    parents = {}
    for spec in BLUEPRINT_SPECS:
        parent_class = unreal.load_class(None, spec["parent_path"])
        if parent_class is None:
            fail(f"Missing compiled native parent: {spec['parent_path']}")
        if parent_class.get_path_name() != spec["parent_path"]:
            fail(
                f"{spec['parent_path']} resolved through a redirect to "
                f"{parent_class.get_path_name()}"
            )
        parents[spec["parent_path"]] = parent_class

    # Audit every pre-existing target before creating anything. This prevents a
    # later dirty/invalid package from leaving a partial create-only migration.
    loaded_assets = {}
    for spec in BLUEPRINT_SPECS:
        parent_class = parents[spec["parent_path"]]
        asset = unreal.load_asset(spec["path"])
        loaded_assets[spec["path"]] = asset
        if asset is not None:
            validate_blueprint(asset, spec, parent_class)

    validations_by_path = {}
    for spec in BLUEPRINT_SPECS:
        parent_class = parents[spec["parent_path"]]
        asset = loaded_assets[spec["path"]]
        if asset is None:
            result["mode"] = "create_new"
            unreal.EditorAssetLibrary.make_directory(spec["folder"])
            factory = unreal.BlueprintFactory()
            factory.set_editor_property("parent_class", parent_class)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                spec["name"],
                spec["folder"],
                unreal.Blueprint,
                factory,
            )
            if asset is None:
                fail(f"AssetTools could not create {spec['path']}")
            created_paths.append(spec["path"])

            # A new Blueprint must prove its exact native parent and generated
            # class before the first package write. Compilation is editor-only
            # and does not save the package.
            compile_result = unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            if compile_result is False:
                fail(f"Blueprint compilation failed for {spec['path']}")
            if spec.get("pickup_item_class_path"):
                item_class = load_exact_class(spec["pickup_item_class_path"])
                world_mesh = unreal.load_asset(spec["pickup_mesh_path"])
                if world_mesh is None or object_path(world_mesh) != spec["pickup_mesh_path"]:
                    fail(
                        "Missing exact pickup world mesh for "
                        f"{spec['path']}: {spec['pickup_mesh_path']}"
                    )
                if not unreal.ProjectSurvivalConsumableEditorLibrary.configure_blueprint_as_world_item_pickup(
                    asset,
                    item_class,
                    world_mesh,
                    int(spec["pickup_item_count"]),
                    float(spec["pickup_mesh_scale"]),
                ):
                    fail(
                        "Project-owned pickup configuration failed for "
                        f"{spec['path']}"
                    )
            validate_blueprint(
                asset,
                spec,
                parent_class,
                require_clean_package=False,
            )
            if not unreal.EditorAssetLibrary.save_loaded_asset(
                asset, only_if_is_dirty=False
            ):
                fail(f"Could not save newly created {spec['path']}")
            saved_paths.append(spec["path"])
            asset = unreal.load_asset(spec["path"])
        validations_by_path[spec["path"]] = validate_blueprint(
            asset, spec, parent_class
        )

    result["validation"] = [
        validations_by_path[spec["path"]] for spec in BLUEPRINT_SPECS
    ]

    after_content = content_snapshot()
    actual_mutations = mutations(before_content, after_content)
    expected_mutations = sorted(created_paths)
    if actual_mutations != expected_mutations:
        fail(
            "Create-only runtime asset delta differs: "
            f"actual={actual_mutations} expected={expected_mutations}"
        )

    after_protected = protected_snapshot()
    result["protected_after"] = after_protected
    mismatches = [
        name
        for name in before_protected
        if before_protected[name] != after_protected[name]
    ]
    result["protected_mismatches"] = mismatches
    if mismatches:
        fail(f"Protected package hashes changed: {mismatches}")

    result["asset_mutations"] = actual_mutations
    result["asset_saves"] = sorted(
        path for path in saved_paths if path in actual_mutations
    )
    result["status"] = "PASS"
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
    after_content = content_snapshot()
    result["asset_mutations"] = mutations(before_content, after_content)
    result["protected_after"] = protected_snapshot()
    result["protected_mismatches"] = [
        name
        for name in before_protected
        if before_protected[name] != result["protected_after"][name]
    ]
finally:
    RESULT_PATH.parent.mkdir(parents=True, exist_ok=True)
    RESULT_PATH.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    unreal.log("CALYSTO_V4_RUNTIME_ASSETS_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Unknown V4 runtime asset failure"))
