"""Run a focused DungeonGeneration PIE smoke test and quit the spawned editor."""

import builtins
import json
import os
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_FILE = Path(
    os.environ.get(
        "CODEX_CALYSTO_PROCEDURAL_PIE_EVIDENCE",
        PROJECT_DIR
        / "Saved"
        / "Migration"
        / "CalystoProcedural"
        / "CalystoProceduralPIE58.json",
    )
)
TARGET_MAP = "/Game/Procedural/Maps/DungeonGeneration"
TARGET_MAP_NAME = "dungeongeneration"
TIMEOUT_SECONDS = 120.0
LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary

CLASS_PATHS = {
    "dungeon": "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C",
    "start_point": "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.BP_StartPoint_C",
    "door": "/Game/Procedural/DoorToLevel.DoorToLevel_C",
    "nav_bounds": "/Script/NavigationSystem.NavMeshBoundsVolume",
    "recast_navmesh": "/Script/NavigationSystem.RecastNavMesh",
}


def normalized_world_name(world):
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


class RuntimeState:
    def __init__(self):
        self.phase = "load_map"
        self.started = time.monotonic()
        self.phase_started = self.started
        self.callback = None
        self.stable_samples = 0
        self.last_sample = {}
        self.error = ""


STATE = RuntimeState()


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("[CalystoProceduralPIE] phase=" + value)


def actors_of_class(world, class_path):
    cls = unreal.load_class(None, class_path)
    if not world or not cls:
        return []
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, cls))


def runtime_sample(world):
    dungeon = actors_of_class(world, CLASS_PATHS["dungeon"])
    starts = actors_of_class(world, CLASS_PATHS["start_point"])
    doors = actors_of_class(world, CLASS_PATHS["door"])
    nav_bounds = actors_of_class(world, CLASS_PATHS["nav_bounds"])
    recast = actors_of_class(world, CLASS_PATHS["recast_navmesh"])
    all_actors = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor))
    pcg_components = []
    for actor in dungeon:
        try:
            pcg_components.extend(actor.get_components_by_class(unreal.PCGComponent))
        except Exception:
            pass
    generating = []
    for component in pcg_components:
        probe = getattr(component, "is_generating", None)
        if callable(probe):
            try:
                generating.append(bool(probe()))
            except Exception:
                pass
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    return {
        "world": object_path(world),
        "world_name": normalized_world_name(world),
        "world_time_seconds": float(
            unreal.GameplayStatics.get_time_seconds(world)
        ),
        "actor_count": len(all_actors),
        "dungeon_actor_count": len(dungeon),
        "dungeon_actor_paths": [object_path(value) for value in dungeon],
        "start_point_count": len(starts),
        "start_point_paths": [object_path(value) for value in starts],
        "nav_bounds_count": len(nav_bounds),
        "recast_navmesh_count": len(recast),
        "door_to_level_count": len(doors),
        "pcg_component_count": len(pcg_components),
        "pcg_generating_states": generating,
        "player_pawn": object_path(player),
    }


def sample_ready(sample):
    generation_idle = not sample["pcg_generating_states"] or not any(
        sample["pcg_generating_states"]
    )
    return (
        sample["world_name"] == TARGET_MAP_NAME
        and sample["world_time_seconds"] >= 5.0
        and sample["dungeon_actor_count"] == 1
        and sample["start_point_count"] >= 1
        and sample["nav_bounds_count"] >= 1
        and sample["recast_navmesh_count"] >= 1
        and sample["pcg_component_count"] >= 1
        and generation_idle
        and sample["door_to_level_count"] == 0
    )


def write_result(success):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    result = {
        "status": "UE58_CALYSTO_PROCEDURAL_PIE_PASS" if success else "UE58_CALYSTO_PROCEDURAL_PIE_FAIL",
        "success": bool(success),
        "target_map": TARGET_MAP,
        "class_paths": CLASS_PATHS,
        "stable_samples": STATE.stable_samples,
        "runtime": STATE.last_sample,
        "door_contract": "DoorToLevel must remain absent from DungeonGeneration and be level-placed in HUB",
        "error": STATE.error,
    }
    OUTPUT_FILE.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log("[CalystoProceduralPIE] result=" + json.dumps(result, sort_keys=True))


def finish(success, error=""):
    STATE.error = error
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_calysto_procedural_pie = None
    write_result(success)
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def tick(_delta_seconds):
    try:
        if time.monotonic() - STATE.started > TIMEOUT_SECONDS:
            finish(False, "timeout in phase {}".format(STATE.phase))
            return

        if STATE.phase == "load_map":
            set_phase("wait_map")
            LEVEL_EDITOR.load_level(TARGET_MAP)
            return

        if STATE.phase == "wait_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if (
                normalized_world_name(editor_world) != TARGET_MAP_NAME
                or time.monotonic() - STATE.phase_started < 1.0
            ):
                return
            set_phase("wait_pie")
            LEVEL_EDITOR.editor_request_begin_play()
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor():
                return
            set_phase("inspect_runtime")
            return

        if STATE.phase == "inspect_runtime":
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            if not world:
                return
            sample = runtime_sample(world)
            STATE.last_sample = sample
            STATE.stable_samples = STATE.stable_samples + 1 if sample_ready(sample) else 0
            if STATE.stable_samples >= 3:
                finish(True)
            return
    except Exception as exc:
        finish(False, "{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_procedural_pie", None)
if existing is not None:
    unreal.log_warning("[CalystoProceduralPIE] duplicate registration ignored")
else:
    editor_python_library = getattr(
        unreal, "EditorPythonScriptingLibrary", None
    )
    if editor_python_library is not None:
        keep_alive = getattr(
            editor_python_library, "set_keep_python_script_alive", None
        )
        if callable(keep_alive):
            keep_alive(True)
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_calysto_procedural_pie = STATE
    unreal.log("[CalystoProceduralPIE] registered")
