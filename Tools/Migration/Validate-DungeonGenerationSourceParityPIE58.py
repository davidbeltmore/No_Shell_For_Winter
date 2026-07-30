"""Focused PIE smoke for source-faithful DungeonGeneration ownership."""

import builtins
import json
import os
import time
import traceback
from pathlib import Path

import unreal


TARGET_MAP = "/Game/Procedural/Maps/DungeonGeneration"
OUTPUT_FILE = Path(os.environ["CODEX_DUNGEON_PARITY_PIE_OUTPUT"])
TIMEOUT_SECONDS = 45.0
LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
DUNGEON_CLASS = unreal.load_class(
    None,
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C",
)
THE_DUNGEON_CLASS = unreal.load_class(
    None, "/Game/_Game/TheDungeon.TheDungeon_C"
)
DOOR_CLASS = unreal.load_class(
    None, "/Game/Procedural/DoorToLevel.DoorToLevel_C"
)
NAV_BOUNDS_CLASS = unreal.load_class(
    None, "/Script/NavigationSystem.NavMeshBoundsVolume"
)
RECAST_CLASS = unreal.load_class(
    None, "/Script/NavigationSystem.RecastNavMesh"
)


class State:
    def __init__(self):
        self.started = time.monotonic()
        self.phase = "wait_editor_world"
        self.phase_started = self.started
        self.callback = None
        self.last_sample = {}


STATE = State()


def world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def actors(world, actor_class):
    if not world or not actor_class:
        return []
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("[DungeonParityPIE] phase=" + value)


def write_result(success, status, error=""):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "status": status,
        "success": bool(success),
        "target_map": TARGET_MAP,
        "runtime": STATE.last_sample,
        "error": error,
        "full_generation_gate": "PENDING_SEPARATE_STARTPOINT_AND_PCG_COMPLETION_TEST",
    }
    OUTPUT_FILE.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    unreal.log("[DungeonParityPIE] result=" + json.dumps(payload, sort_keys=True))


def finish(success, status, error=""):
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
    builtins._codex_dungeon_parity_pie = None
    write_result(success, status, error)
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def tick(_delta_seconds):
    try:
        if time.monotonic() - STATE.started > TIMEOUT_SECONDS:
            finish(
                False,
                "UE58_DUNGEON_GENERATION_SOURCE_PARITY_PIE_FAIL",
                "timeout in phase " + STATE.phase,
            )
            return

        if STATE.phase == "wait_editor_world":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if world_name(editor_world) != "dungeongeneration":
                return
            set_phase("wait_pie")
            LEVEL_EDITOR.editor_request_begin_play()
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor():
                return
            set_phase("sample_runtime")
            return

        if STATE.phase == "sample_runtime":
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            if not world:
                return
            elapsed = time.monotonic() - STATE.phase_started
            dungeon = actors(world, DUNGEON_CLASS)
            legacy = actors(world, THE_DUNGEON_CLASS)
            doors = actors(world, DOOR_CLASS)
            nav_bounds = actors(world, NAV_BOUNDS_CLASS)
            recast = actors(world, RECAST_CLASS)
            player = unreal.GameplayStatics.get_player_pawn(world, 0)
            STATE.last_sample = {
                "world": world.get_path_name(),
                "world_name": world_name(world),
                "dungeon_actor_count": len(dungeon),
                "dungeon_actor_paths": [
                    value.get_path_name() for value in dungeon
                ],
                "the_dungeon_actor_count": len(legacy),
                "door_to_level_count": len(doors),
                "nav_bounds_count": len(nav_bounds),
                "recast_navmesh_count": len(recast),
                "player_pawn": player.get_path_name() if player else "",
                "sample_elapsed_seconds": elapsed,
            }
            if elapsed < 5.0:
                return
            success = (
                STATE.last_sample["world_name"] == "dungeongeneration"
                and STATE.last_sample["dungeon_actor_count"] == 1
                and STATE.last_sample["the_dungeon_actor_count"] == 0
                and STATE.last_sample["door_to_level_count"] == 0
                and STATE.last_sample["nav_bounds_count"] >= 1
                and STATE.last_sample["recast_navmesh_count"] >= 1
                and bool(STATE.last_sample["player_pawn"])
            )
            finish(
                success,
                (
                    "UE58_DUNGEON_GENERATION_SOURCE_PARITY_PIE_PASS"
                    if success
                    else "UE58_DUNGEON_GENERATION_SOURCE_PARITY_PIE_FAIL"
                ),
                "" if success else "runtime ownership sample did not match",
            )
    except Exception as exc:
        finish(
            False,
            "UE58_DUNGEON_GENERATION_SOURCE_PARITY_PIE_FAIL",
            "{}\n{}".format(exc, traceback.format_exc()),
        )


if not unreal.SystemLibrary.get_engine_version().startswith("5.8."):
    raise RuntimeError("Expected UE 5.8")
if DUNGEON_CLASS is None:
    raise RuntimeError("BP_MassiveDungeon class failed to load")

world = unreal.EditorLoadingAndSavingUtils.load_map(TARGET_MAP)
if world is None:
    raise RuntimeError("DungeonGeneration failed to load")

existing = getattr(builtins, "_codex_dungeon_parity_pie", None)
if existing is not None:
    unreal.log_warning("[DungeonParityPIE] duplicate registration ignored")
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
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_dungeon_parity_pie = STATE
    unreal.log("[DungeonParityPIE] registered")
