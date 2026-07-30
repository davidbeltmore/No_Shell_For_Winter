"""Capture original and three corrected Crawl-idle candidates in one PIE run."""

import builtins
import json
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
EVIDENCE_DIR = (
    PROJECT_DIR / "Saved" / "Migration" / "AnimationMigration" / "20260719"
)
OUTPUT_FILE = EVIDENCE_DIR / "UE58_CrawlSpineCorrectionVisual.json"
TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
TIMEOUT_SECONDS = 100.0
CASES = (
    (
        "original",
        "/Game/_Game/Animations/Locomotion/Crawl/"
        "Anim_KA_Crawling_Baby_Idle.Anim_KA_Crawling_Baby_Idle",
    ),
    (
        "corr25",
        "/Game/_Game/Animations/Locomotion/Crawl/Diagnostics/"
        "Anim_KA_Crawling_Baby_Idle_Corr25."
        "Anim_KA_Crawling_Baby_Idle_Corr25",
    ),
    (
        "corr45",
        "/Game/_Game/Animations/Locomotion/Crawl/Diagnostics/"
        "Anim_KA_Crawling_Baby_Idle_Corr45."
        "Anim_KA_Crawling_Baby_Idle_Corr45",
    ),
    (
        "corr65",
        "/Game/_Game/Animations/Locomotion/Crawl/Diagnostics/"
        "Anim_KA_Crawling_Baby_Idle_Corr65."
        "Anim_KA_Crawling_Baby_Idle_Corr65",
    ),
)

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
        self.case_index = 0
        self.loaded_cases = []
        self.original_idle = None
        self.initial_location = None
        self.initial_rotation = None
        self.movement = None
        self.result = {
            "status": "UE58_CRAWL_SPINE_CORRECTION_VISUAL_FAIL",
            "map": TARGET_MAP,
            "cases": [],
            "assets_saved": False,
            "errors": [],
        }


STATE = State()


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
            STATE.camera,
        )
    )


def set_side_camera():
    player_location = STATE.player.get_actor_location()
    right = STATE.player.get_actor_right_vector()
    camera_location = player_location + (right * 245.0) + unreal.Vector(0, 0, 70)
    target_location = player_location + unreal.Vector(0, 0, 58)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(
        camera_location, target_location
    )
    STATE.camera.set_actor_location_and_rotation(
        camera_location, camera_rotation, False, True
    )
    STATE.controller.set_view_target_with_blend(STATE.camera, 0.0)


def freeze_player():
    if STATE.initial_location is not None:
        STATE.player.set_actor_location(STATE.initial_location, False, False)
    if STATE.initial_rotation is not None:
        STATE.player.set_actor_rotation(STATE.initial_rotation, False)
    if STATE.movement:
        STATE.movement.stop_movement_immediately()
    STATE.controller.set_ignore_move_input(True)
    STATE.controller.set_ignore_look_input(True)
    flush_keys = getattr(STATE.controller, "flush_pressed_keys", None)
    if callable(flush_keys):
        flush_keys()


def case_screenshot(label):
    path = EVIDENCE_DIR / f"UE58_Crawl_Spine_{label}.png"
    accepted = unreal.AutomationLibrary.take_high_res_screenshot(
        1280, 720, str(path)
    )
    return path, bool(accepted)


def apply_case(index):
    label, asset_path = CASES[index]
    asset = STATE.loaded_cases[index]
    freeze_player()
    set_side_camera()
    STATE.locomotion.set_editor_property("crawl_idle_animation", asset)
    STATE.phase = "wait_case"
    STATE.phase_elapsed = 0.0
    unreal.log(
        "[CrawlSpineVisual58] applying {} -> {}".format(label, asset_path)
    )


def finish(success):
    try:
        if STATE.locomotion:
            if STATE.original_idle:
                STATE.locomotion.set_editor_property(
                    "crawl_idle_animation", STATE.original_idle
                )
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
    except Exception:
        pass
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_crawl_spine_visual_callback = None
    STATE.result["status"] = (
        "UE58_CRAWL_SPINE_CORRECTION_VISUAL_PASS"
        if success
        else "UE58_CRAWL_SPINE_CORRECTION_VISUAL_FAIL"
    )
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[CrawlSpineVisual58] " + json.dumps(STATE.result))
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def tick(delta_time):
    try:
        STATE.phase_elapsed += delta_time
        if time.monotonic() - STATE.started_at > TIMEOUT_SECONDS:
            STATE.result["errors"].append("timeout:" + STATE.phase)
            finish(False)
            return

        if STATE.phase == "load_map":
            EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
            STATE.loaded_cases = [
                unreal.EditorAssetLibrary.load_asset(asset_path)
                for _, asset_path in CASES
            ]
            if not all(STATE.loaded_cases):
                raise RuntimeError("One or more correction candidates failed to load")
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
                raise RuntimeError("Could not create transient diagnostic camera")
            editor_camera.set_actor_label("CodexCrawlSpineCorrectionCamera")
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
            STATE.initial_location = STATE.player.get_actor_location()
            STATE.initial_rotation = STATE.player.get_actor_rotation()
            STATE.movement = STATE.player.get_component_by_class(
                unreal.CharacterMovementComponent
            )
            freeze_player()
            set_side_camera()
            STATE.original_idle = STATE.locomotion.get_editor_property(
                "crawl_idle_animation"
            )
            STATE.locomotion.set_editor_property(
                "enable_crawl_mesh_deformer", True
            )
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            call(STATE.locomotion, "set_crawl_mode_enabled", True)
            STATE.phase = "wait_initial_idle"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_initial_idle" and STATE.phase_elapsed >= 4.2:
            apply_case(0)
            return

        if STATE.phase == "wait_case" and STATE.phase_elapsed >= 2.0:
            label, expected_asset = CASES[STATE.case_index]
            freeze_player()
            set_side_camera()
            STATE.phase = "capturing"
            STATE.phase_elapsed = 0.0
            path, accepted = case_screenshot(label)
            STATE.result["cases"].append(
                {
                    "label": label,
                    "expected_asset": expected_asset,
                    "active_animation": str(
                        call(
                            STATE.locomotion,
                            "get_current_animation_asset_name",
                        )
                    ),
                    "deformer": str(
                        call(
                            STATE.locomotion,
                            "get_active_crawl_mesh_deformer_name",
                        )
                    ),
                    "screenshot": str(path),
                    "screenshot_accepted": accepted,
                }
            )
            STATE.phase = "wait_capture"
            return

        if STATE.phase == "wait_capture" and STATE.phase_elapsed >= 1.5:
            STATE.case_index += 1
            if STATE.case_index >= len(CASES):
                valid = all(
                    row["screenshot_accepted"]
                    and Path(row["screenshot"]).is_file()
                    and row["active_animation"]
                    == CASES[index][1].rsplit(".", 1)[-1]
                    for index, row in enumerate(STATE.result["cases"])
                )
                finish(valid)
                return
            apply_case(STATE.case_index)
    except Exception as exc:
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = traceback.format_exc()
        unreal.log_error(STATE.result["traceback"])
        finish(False)


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_crawl_spine_visual_callback = STATE.callback
unreal.log("[CrawlSpineVisual58] registered")
