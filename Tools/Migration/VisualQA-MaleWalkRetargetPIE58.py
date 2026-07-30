"""Rendered PIE QA for Male + custom Walk N retargets in the real HUB map."""

import builtins
import json
import os
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_DIR = Path(
    os.environ.get(
        "CODEX_ANIMATION_MIGRATION_QA_DIR",
        PROJECT_DIR / "Saved" / "Migration" / "AnimationMigration" / "20260719",
    )
)
OUTPUT_FILE = OUTPUT_DIR / "UE58_MaleWalkRetargetVisualQA.json"
SCREENSHOT = OUTPUT_DIR / "UE58_MaleWalkN_Runtime.png"
TARGET_MAP = "/Game/_Game/Hub/HUB"
MALE_PATH = "/Game/DazToUnreal/Male/Male.Male"
EXPECTED_WALK = "Anim_KA_Walk04_Male"
TIMEOUT_SECONDS = 120.0

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def object_path(value):
    if not value:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def call(obj, method_name, *args):
    method = getattr(obj, method_name, None)
    if not callable(method):
        raise RuntimeError(f"Missing reflected method {method_name} on {obj}")
    return method(*args)


def find_component(actor, name_fragment):
    if not actor:
        return None
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if (
            name_fragment in component.get_name()
            or name_fragment in component.get_class().get_name()
        ):
            return component
    return None


def normalized_blueprint_status(blueprint):
    raw = str(blueprint.get_editor_property("status"))
    return raw, "".join(character for character in raw.upper() if character.isalnum())


def compile_blueprint(package):
    blueprint = unreal.load_asset(package)
    if blueprint is None:
        raise RuntimeError("Unable to load Blueprint: " + package)
    before, _ = normalized_blueprint_status(blueprint)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    after, normalized = normalized_blueprint_status(blueprint)
    return {
        "package": package,
        "status_before": before,
        "status_after": after,
        "up_to_date": "UPTODATE" in normalized,
        "saved": False,
    }


def apply_male_body(actor):
    male = unreal.load_asset(MALE_PATH)
    if male is None:
        raise RuntimeError("Unable to load Male mesh")
    changed = []
    for component in actor.get_components_by_class(unreal.SkeletalMeshComponent):
        current = call(component, "get_skeletal_mesh_asset")
        if object_path(current) in (
            "/Game/DazToUnreal/Female/Female.Female",
            MALE_PATH,
        ):
            call(component, "set_skeletal_mesh_asset", male)
            changed.append(component.get_name())
    if not changed:
        raise RuntimeError("No player body components accepted the Male mesh")
    return changed


class State:
    def __init__(self):
        self.phase = "compile"
        self.phase_elapsed = 0.0
        self.started = time.monotonic()
        self.callback = None
        self.player = None
        self.locomotion = None
        self.result = {
            "status": "UE58_MALE_WALK_RETARGET_VISUAL_QA_FAIL",
            "map": TARGET_MAP,
            "male_mesh": MALE_PATH,
            "blueprints": [],
            "runtime": {},
            "screenshot": {},
            "errors": [],
        }


STATE = State()


def finish(success):
    try:
        if STATE.locomotion:
            call(STATE.locomotion, "set_walk_mode_enabled", False)
    except Exception:
        pass
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_male_walk_visual_callback = None
    STATE.result["status"] = (
        "UE58_MALE_WALK_RETARGET_VISUAL_QA_PASS"
        if success
        else "UE58_MALE_WALK_RETARGET_VISUAL_QA_FAIL"
    )
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[MaleWalkRetargetVisualQA58] " + STATE.result["status"])
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def tick(delta_time):
    try:
        STATE.phase_elapsed += delta_time
        if time.monotonic() - STATE.started > TIMEOUT_SECONDS:
            STATE.result["errors"].append("timeout:" + STATE.phase)
            finish(False)
            return

        if STATE.phase == "compile":
            STATE.result["blueprints"] = [
                compile_blueprint("/Game/FullSample/GASP/ACF_GenericRetarget_ABP"),
                compile_blueprint("/Game/_Game/Animations/Kawaii/Female/ABP_Manny"),
            ]
            if not all(row["up_to_date"] for row in STATE.result["blueprints"]):
                raise RuntimeError("A relevant AnimBlueprint did not compile UP_TO_DATE")
            LEVEL_EDITOR.load_level(TARGET_MAP)
            STATE.phase = "wait_map"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_map":
            world = UNREAL_EDITOR.get_editor_world()
            if not world or "hub" not in world.get_name().lower() or STATE.phase_elapsed < 1.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_pie":
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            locomotion = find_component(player, "ProjectLocomotionOverride")
            if (
                not LEVEL_EDITOR.is_in_play_in_editor()
                or STATE.phase_elapsed < 2.0
                or not player
                or not locomotion
            ):
                return
            STATE.player = player
            STATE.locomotion = locomotion
            STATE.result["runtime"]["male_applied_to"] = apply_male_body(player)
            call(locomotion, "set_walk_mode_enabled", True)
            STATE.phase = "move"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "move":
            call(
                STATE.player,
                "add_movement_input",
                STATE.player.get_actor_forward_vector(),
                1.0,
                True,
            )
            if STATE.phase_elapsed < 1.5:
                return
            animation = str(
                call(STATE.locomotion, "get_current_animation_asset_name")
            )
            STATE.result["runtime"].update(
                {
                    "resolved_component": str(
                        call(
                            STATE.locomotion,
                            "get_resolved_skeletal_mesh_component_name",
                        )
                    ),
                    "resolved_mesh": str(
                        call(
                            STATE.locomotion,
                            "get_resolved_skeletal_mesh_asset_path",
                        )
                    ),
                    "animation": animation,
                    "overlay_montage": str(
                        call(STATE.locomotion, "get_active_overlay_montage_name")
                    ),
                }
            )
            SCREENSHOT.parent.mkdir(parents=True, exist_ok=True)
            accepted = unreal.AutomationLibrary.take_high_res_screenshot(
                1280, 720, str(SCREENSHOT)
            )
            STATE.result["screenshot"] = {
                "path": str(SCREENSHOT),
                "accepted": bool(accepted),
            }
            STATE.phase = "wait_screenshot"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_screenshot" and STATE.phase_elapsed >= 2.5:
            STATE.result["screenshot"]["exists"] = SCREENSHOT.is_file()
            finish(
                STATE.result["runtime"].get("resolved_mesh") == MALE_PATH
                and STATE.result["runtime"].get("animation") == EXPECTED_WALK
                and STATE.result["screenshot"].get("accepted")
                and STATE.result["screenshot"].get("exists")
            )
            return
    except Exception as exc:
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = traceback.format_exc()
        finish(False)


STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_male_walk_visual_callback = STATE.callback
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.log("[MaleWalkRetargetVisualQA58] registered")
