"""PIE validation that Walk N uses the current Male body and Male retargets.

The script runs only in PIE/runtime state, writes evidence under Saved/, and
quits the validation editor. It does not save assets.
"""

import builtins
import json
import os
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
EVIDENCE_DIR = Path(
    os.environ.get(
        "CODEX_ANIMATION_MIGRATION_QA_DIR",
        PROJECT_DIR / "Saved" / "Migration" / "AnimationMigration" / "20260719",
    )
)
OUTPUT_FILE = EVIDENCE_DIR / "UE58_MaleWalkRetargetPIE.json"
TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
MALE_PATH = "/Game/DazToUnreal/Male/Male.Male"
FEMALE_PATH = "/Game/DazToUnreal/Female/Female.Female"
TIMEOUT_SECONDS = 90.0
EXPECTED_MALE_IDLE = "Anim_KA_Idle01_breathing_Male"
EXPECTED_MALE_WALK = "Anim_KA_Walk04_Male"

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def path(value):
    if not value:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def call(obj, snake_name, *args):
    fn = getattr(obj, snake_name, None)
    if not callable(fn):
        raise RuntimeError(f"Missing reflected method {snake_name} on {obj}")
    return fn(*args)


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
        if class_name_fragment in component.get_class().get_name() or class_name_fragment in component.get_name():
            return component
    return None


def component_inventory(actor):
    rows = []
    if not actor:
        return rows
    for component in actor.get_components_by_class(unreal.ActorComponent):
        rows.append({
            "name": component.get_name(),
            "class": component.get_class().get_name(),
        })
    return rows


def mesh_component_row(component):
    mesh = call(component, "get_skeletal_mesh_asset") if component else None
    return {
        "component": component.get_name() if component else "None",
        "mesh": path(mesh),
        "visible": bool(component.is_visible()) if component else False,
        "hidden_in_game": bool(component.get_editor_property("hidden_in_game")) if component else False,
    }


class State:
    def __init__(self):
        self.phase = "load_map"
        self.phase_elapsed = 0.0
        self.started_at = time.monotonic()
        self.callback = None
        self.player = None
        self.customization = None
        self.locomotion = None
        self.result = {
            "status": "UE58_MALE_WALK_RETARGET_PIE_FAIL",
            "map": TARGET_MAP,
            "male_path": MALE_PATH,
            "components_before": [],
            "components_after_gender": [],
            "walk_n_idle": {},
            "walk_n_moving": {},
            "errors": [],
        }


STATE = State()


def gather_skeletal_components(actor):
    rows = []
    if not actor:
        return rows
    for component in actor.get_components_by_class(unreal.SkeletalMeshComponent):
        mesh = call(component, "get_skeletal_mesh_asset")
        if mesh:
            rows.append(mesh_component_row(component))
    return rows


def apply_male_body_mesh(actor):
    male_mesh = unreal.load_asset(MALE_PATH)
    if not male_mesh:
        raise RuntimeError("Unable to load current Male mesh")

    updated = []
    for component in actor.get_components_by_class(unreal.SkeletalMeshComponent):
        mesh = call(component, "get_skeletal_mesh_asset")
        mesh_path = path(mesh)
        if mesh_path not in (FEMALE_PATH, MALE_PATH):
            continue
        call(component, "set_skeletal_mesh_asset", male_mesh)
        updated.append(component.get_name())
    if not updated:
        raise RuntimeError("No Female/Male body skeletal mesh components were found")
    return updated


def restore_runtime_state():
    if STATE.locomotion:
        for method_name, value in (
            ("set_crawl_mode_enabled", False),
            ("set_walk_mode_enabled", False),
        ):
            try:
                call(STATE.locomotion, method_name, value)
            except Exception:
                pass


def finish(success):
    restore_runtime_state()
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_male_walk_mesh_qa_callback = None
    STATE.result["status"] = (
        "UE58_MALE_WALK_RETARGET_PIE_PASS"
        if success
        else "UE58_MALE_WALK_RETARGET_PIE_FAIL"
    )
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[MaleWalkMeshPIE58] " + json.dumps(STATE.result, ensure_ascii=True))
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def resolve_context():
    world = EDITOR_LEVEL_LIBRARY.get_game_world()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    customization = find_component(player, "EFCharacterCustomizationComponent")
    if not customization:
        customization = find_component(player, "CharacterCustomization")
    locomotion = find_component(player, "ProjectLocomotionOverrideComponent")
    if not locomotion:
        locomotion = find_component(player, "LocomotionOverride")
    if player:
        STATE.result["context_probe"] = {
            "player": path(player),
            "components": component_inventory(player),
            "customization": customization.get_name() if customization else "None",
            "locomotion": locomotion.get_name() if locomotion else "None",
        }
    if not all((world, player, locomotion)):
        return False
    STATE.player = player
    STATE.customization = customization
    STATE.locomotion = locomotion
    return True


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
            editor_world = UNREAL_EDITOR.get_editor_world()
            if world_name(editor_world) != TARGET_MAP_NAME or STATE.phase_elapsed < 1.0:
                return
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
            STATE.result["player"] = path(STATE.player)
            STATE.result["components_before"] = gather_skeletal_components(STATE.player)
            STATE.result["male_mesh_applied_to"] = apply_male_body_mesh(STATE.player)
            STATE.phase = "wait_male"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_male" and STATE.phase_elapsed >= 1.0:
            STATE.result["components_after_gender"] = gather_skeletal_components(STATE.player)
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            STATE.phase = "walk_n"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "walk_n" and STATE.phase_elapsed >= 1.0:
            resolved_component = str(call(STATE.locomotion, "get_resolved_skeletal_mesh_component_name"))
            resolved_mesh = str(call(STATE.locomotion, "get_resolved_skeletal_mesh_asset_path"))
            STATE.result["walk_n_idle"] = {
                "walk_enabled": bool(call(STATE.locomotion, "is_walk_mode_enabled")),
                "resolved_component": resolved_component,
                "resolved_mesh": resolved_mesh,
                "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")),
                "overlay_montage": str(call(STATE.locomotion, "get_active_overlay_montage_name")),
                "dependencies": str(call(STATE.locomotion, "describe_resolved_dependencies")),
            }
            STATE.phase = "walk_n_moving"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "walk_n_moving":
            call(
                STATE.player,
                "add_movement_input",
                STATE.player.get_actor_forward_vector(),
                1.0,
                True,
            )
            if STATE.phase_elapsed < 1.25:
                return
            STATE.result["walk_n_moving"] = {
                "walk_enabled": bool(call(STATE.locomotion, "is_walk_mode_enabled")),
                "resolved_component": str(
                    call(STATE.locomotion, "get_resolved_skeletal_mesh_component_name")
                ),
                "resolved_mesh": str(
                    call(STATE.locomotion, "get_resolved_skeletal_mesh_asset_path")
                ),
                "animation": str(
                    call(STATE.locomotion, "get_current_animation_asset_name")
                ),
                "overlay_montage": str(
                    call(STATE.locomotion, "get_active_overlay_montage_name")
                ),
            }
            idle = STATE.result["walk_n_idle"]
            moving = STATE.result["walk_n_moving"]
            finish(
                idle.get("resolved_mesh") == MALE_PATH
                and idle.get("animation") == EXPECTED_MALE_IDLE
                and moving.get("resolved_mesh") == MALE_PATH
                and moving.get("animation") == EXPECTED_MALE_WALK
            )
            return

    except Exception as exc:
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = traceback.format_exc()
        finish(False)


def main():
    builtins._codex_male_walk_mesh_qa_callback = tick
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    unreal.log("[MaleWalkMeshPIE58] registered")


main()
