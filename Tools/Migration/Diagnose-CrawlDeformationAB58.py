"""Capture an identical side-view Crawl pose with and without runtime DQS.

This is a PIE-only diagnostic. It never saves the player, skeletal mesh, map,
or animation assets.
"""

import builtins
import json
import os
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
EVIDENCE_DIR = (
    PROJECT_DIR / "Saved" / "Migration" / "AnimationMigration" / "20260719"
)
OUTPUT_FILE = EVIDENCE_DIR / "UE58_CrawlDeformation_AB.json"
DQS_SCREENSHOT = EVIDENCE_DIR / "UE58_Crawl_Side_DQS.png"
LINEAR_SCREENSHOT = EVIDENCE_DIR / "UE58_Crawl_Side_Linear.png"
TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
TIMEOUT_SECONDS = 90.0

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def call(obj, method, *args):
    function = getattr(obj, method, None)
    if not callable(function):
        raise RuntimeError(f"Missing reflected method {method} on {obj}")
    return function(*args)


def world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def find_component(actor, class_name_fragment):
    if not actor:
        return None
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if class_name_fragment in component.get_class().get_name():
            return component
    return None


def screenshot(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    accepted = unreal.AutomationLibrary.take_high_res_screenshot(
        1280, 720, str(path)
    )
    return {"accepted": bool(accepted), "path": str(path)}


class State:
    def __init__(self):
        self.phase = "load_map"
        self.phase_elapsed = 0.0
        self.started_at = time.monotonic()
        self.callback = None
        self.world = None
        self.player = None
        self.controller = None
        self.locomotion = None
        self.mesh = None
        self.camera = None
        self.editor_camera_name = ""
        self.result = {
            "status": "UE58_CRAWL_DEFORMATION_AB_FAIL",
            "map": TARGET_MAP,
            "dqs": {},
            "linear": {},
            "screenshots": {},
            "assets_saved": False,
            "errors": [],
        }


STATE = State()


def set_side_camera():
    player_location = STATE.player.get_actor_location()
    right = STATE.player.get_actor_right_vector()
    camera_location = player_location + (right * 315.0) + unreal.Vector(0, 0, 75)
    target_location = player_location + unreal.Vector(0, 0, 55)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(
        camera_location, target_location
    )
    if not STATE.camera:
        raise RuntimeError("Could not resolve diagnostic CameraActor in PIE")
    STATE.camera.set_actor_location_and_rotation(
        camera_location, camera_rotation, False, True
    )
    STATE.controller.set_view_target_with_blend(STATE.camera, 0.0)


def deformer_snapshot():
    return {
        "crawl_active": bool(call(STATE.locomotion, "is_crawl_mode_active")),
        "animation": str(
            call(STATE.locomotion, "get_current_animation_asset_name")
        ),
        "runtime_deformer_applied": bool(
            call(STATE.locomotion, "is_crawl_mesh_deformer_applied")
        ),
        "active_deformer": str(
            call(STATE.locomotion, "get_active_crawl_mesh_deformer_name")
        ),
        "mesh": STATE.mesh.get_skeletal_mesh_asset().get_path_name(),
    }


def finish(success):
    try:
        if STATE.locomotion:
            STATE.locomotion.set_editor_property(
                "enable_crawl_mesh_deformer", True
            )
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
    except Exception:
        pass

    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_crawl_deformation_ab_callback = None

    STATE.result["screenshots"].setdefault("dqs", {})["exists"] = (
        DQS_SCREENSHOT.is_file()
    )
    STATE.result["screenshots"].setdefault("linear", {})["exists"] = (
        LINEAR_SCREENSHOT.is_file()
    )
    STATE.result["status"] = (
        "UE58_CRAWL_DEFORMATION_AB_PASS"
        if success
        else "UE58_CRAWL_DEFORMATION_AB_FAIL"
    )
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[CrawlDeformationAB58] " + json.dumps(STATE.result))
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def resolve_context():
    STATE.world = EDITOR_LEVEL_LIBRARY.get_game_world()
    STATE.player = (
        unreal.GameplayStatics.get_player_pawn(STATE.world, 0)
        if STATE.world
        else None
    )
    STATE.controller = (
        unreal.GameplayStatics.get_player_controller(STATE.world, 0)
        if STATE.world
        else None
    )
    STATE.locomotion = find_component(
        STATE.player, "ProjectLocomotionOverrideComponent"
    )
    STATE.mesh = STATE.player.get_component_by_class(
        unreal.SkeletalMeshComponent
    ) if STATE.player else None
    if STATE.world and STATE.editor_camera_name:
        for camera in unreal.GameplayStatics.get_all_actors_of_class(
            STATE.world, unreal.CameraActor
        ):
            if camera.get_name().startswith(STATE.editor_camera_name):
                STATE.camera = camera
                break
    return all(
        (
            STATE.world,
            STATE.player,
            STATE.controller,
            STATE.locomotion,
            STATE.mesh,
        )
    )


def tick(delta_time):
    try:
        STATE.phase_elapsed += delta_time
        if time.monotonic() - STATE.started_at > TIMEOUT_SECONDS:
            STATE.result["errors"].append("timeout:" + STATE.phase)
            finish(False)
            return

        if STATE.phase == "load_map":
            LEVEL_EDITOR.load_level(TARGET_MAP)
            STATE.phase = "wait_map"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_map":
            if (
                world_name(UNREAL_EDITOR.get_editor_world()) != TARGET_MAP_NAME
                or STATE.phase_elapsed < 1.0
            ):
                return
            editor_camera = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.CameraActor, unreal.Vector(), unreal.Rotator()
            )
            if not editor_camera:
                raise RuntimeError(
                    "Could not create transient diagnostic CameraActor before PIE"
                )
            editor_camera.set_actor_label("CodexCrawlDiagnosticCamera")
            STATE.editor_camera_name = editor_camera.get_name()
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_pie":
            if (
                not LEVEL_EDITOR.is_in_play_in_editor()
                or STATE.phase_elapsed < 2.0
                or not resolve_context()
            ):
                return
            set_side_camera()
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            call(STATE.locomotion, "set_crawl_mode_enabled", True)
            STATE.phase = "dqs_pose"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "dqs_pose" and STATE.phase_elapsed >= 2.0:
            STATE.result["dqs"] = deformer_snapshot()
            STATE.result["screenshots"]["dqs"] = screenshot(DQS_SCREENSHOT)
            STATE.phase = "dqs_capture"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "dqs_capture" and STATE.phase_elapsed >= 1.5:
            STATE.locomotion.set_editor_property(
                "enable_crawl_mesh_deformer", False
            )
            STATE.phase = "linear_pose"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "linear_pose" and STATE.phase_elapsed >= 2.0:
            STATE.result["linear"] = deformer_snapshot()
            STATE.result["screenshots"]["linear"] = screenshot(
                LINEAR_SCREENSHOT
            )
            STATE.phase = "linear_capture"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "linear_capture" and STATE.phase_elapsed >= 1.5:
            valid = (
                STATE.result["dqs"].get("runtime_deformer_applied") is True
                and STATE.result["linear"].get("runtime_deformer_applied")
                is False
                and STATE.result["dqs"].get("animation")
                == STATE.result["linear"].get("animation")
                == "Anim_KA_Crawling_Baby_Idle"
                and DQS_SCREENSHOT.is_file()
                and LINEAR_SCREENSHOT.is_file()
            )
            finish(valid)
    except Exception as exc:
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = traceback.format_exc()
        unreal.log_error(STATE.result["traceback"])
        finish(False)


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_crawl_deformation_ab_callback = STATE.callback
unreal.log("[CrawlDeformationAB58] registered")
