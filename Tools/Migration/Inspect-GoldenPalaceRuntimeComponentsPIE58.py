"""Read-only PIE inventory of all skeletal components that may own Golden Palace."""

import builtins
import json
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_FILE = (
    PROJECT_DIR
    / "Saved"
    / "Migration"
    / "AnimationMigration"
    / "20260719"
    / "UE58_GoldenPalaceRuntimeComponents.json"
)
TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def object_path(value):
    return value.get_path_name() if value else ""


def component_row(component):
    mesh = component.get_skeletal_mesh_asset()
    materials = []
    golden = []
    for index in range(component.get_num_materials()):
        material = component.get_material(index)
        row = {"index": index, "material": object_path(material)}
        materials.append(row)
        if "goldenpalace" in row["material"].lower():
            golden.append(row)
    return {
        "owner": object_path(component.get_owner()),
        "component": object_path(component),
        "mesh": object_path(mesh),
        "visible": bool(component.is_visible()),
        "active": bool(component.is_active()),
        "material_count": len(materials),
        "golden_palace_materials": golden,
        "materials": materials,
    }


def mesh_asset_row(path):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    rows = []
    golden = []
    if mesh:
        for index, skeletal_material in enumerate(
            mesh.get_editor_property("materials")
        ):
            material = skeletal_material.get_editor_property(
                "material_interface"
            )
            row = {
                "index": index,
                "slot": str(
                    skeletal_material.get_editor_property(
                        "material_slot_name"
                    )
                ),
                "material": object_path(material),
            }
            rows.append(row)
            if "goldenpalace" in (row["slot"] + row["material"]).lower():
                golden.append(row)
    return {
        "mesh": path,
        "loaded": bool(mesh),
        "material_count": len(rows),
        "golden_palace_materials": golden,
    }


class State:
    def __init__(self):
        self.phase = "load_map"
        self.elapsed = 0.0
        self.started = time.monotonic()
        self.callback = None
        self.result = {
            "status": "UE58_GOLDEN_PALACE_RUNTIME_COMPONENTS_FAIL",
            "assets_saved": False,
            "errors": [],
        }


STATE = State()


def finish(success):
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_golden_palace_inventory_callback = None
    STATE.result["status"] = (
        "UE58_GOLDEN_PALACE_RUNTIME_COMPONENTS_PASS"
        if success
        else "UE58_GOLDEN_PALACE_RUNTIME_COMPONENTS_FAIL"
    )
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[GoldenPalaceInventory58] " + json.dumps(STATE.result))
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def tick(delta_time):
    try:
        STATE.elapsed += delta_time
        if time.monotonic() - STATE.started > 70.0:
            STATE.result["errors"].append("timeout:" + STATE.phase)
            finish(False)
            return
        if STATE.phase == "load_map":
            LEVEL_EDITOR.load_level(TARGET_MAP)
            STATE.phase = "wait_map"
            STATE.elapsed = 0.0
            return
        if STATE.phase == "wait_map":
            if (
                world_name(UNREAL_EDITOR.get_editor_world()) != TARGET_MAP_NAME
                or STATE.elapsed < 1.0
            ):
                return
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.elapsed = 0.0
            return
        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor() or STATE.elapsed < 3.0:
                return
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            player = unreal.GameplayStatics.get_player_pawn(world, 0)
            if not world or not player:
                return
            rows = []
            player_rows = []
            for actor in unreal.GameplayStatics.get_all_actors_of_class(
                world, unreal.Actor
            ):
                for component in actor.get_components_by_class(
                    unreal.SkeletalMeshComponent
                ):
                    row = component_row(component)
                    rows.append(row)
                    if actor == player:
                        player_rows.append(row)
            STATE.result["player"] = object_path(player)
            STATE.result["player_components"] = player_rows
            STATE.result["all_skeletal_components"] = rows
            STATE.result["golden_palace_components"] = [
                row for row in rows if row["golden_palace_materials"]
            ]
            STATE.result["mesh_assets"] = [
                mesh_asset_row(
                    "/Game/DazToUnreal/Female/Female.Female"
                ),
                mesh_asset_row(
                    "/Game/DazToUnreal/Multiple/Multiple.Multiple"
                ),
                mesh_asset_row("/Game/DazToUnreal/Male/Male.Male"),
            ]
            finish(True)
    except Exception as exc:
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = traceback.format_exc()
        unreal.log_error(STATE.result["traceback"])
        finish(False)


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_golden_palace_inventory_callback = STATE.callback
unreal.log("[GoldenPalaceInventory58] registered")
