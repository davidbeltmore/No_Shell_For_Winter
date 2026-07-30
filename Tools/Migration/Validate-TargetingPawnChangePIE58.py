"""Reproduce the possession-change cleanup that previously entered ACF with a null ControlledPawn."""

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
        "CODEX_TARGETING_PAWN_CHANGE_QA_OUTPUT",
        PROJECT_DIR
        / "Saved"
        / "Migration"
        / "Phase5"
        / "Runtime"
        / "TargetingPawnChangePIE58.json",
    )
)
TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
TIMEOUT_SECONDS = 90.0
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


def game_world():
    return EDITOR_LEVEL_LIBRARY.get_game_world()


def component(actor, name_fragment):
    if not actor:
        return None
    for candidate in actor.get_components_by_class(unreal.ActorComponent):
        if name_fragment in candidate.get_class().get_name():
            return candidate
    return None


def world_subsystem(world, class_path):
    subsystem_class = unreal.load_class(None, class_path)
    if not world or not subsystem_class:
        return None
    try:
        result = unreal.SubsystemBlueprintLibrary.get_world_subsystem(world, subsystem_class)
        if result:
            return result
    except Exception:
        pass
    prefix = world.get_path_name()
    for candidate in unreal.ObjectIterator(unreal.Object):
        try:
            if candidate.get_class() == subsystem_class and candidate.get_path_name().startswith(prefix):
                return candidate
        except Exception:
            continue
    return None


def invoke(obj, name, *args):
    function = getattr(obj, name, None)
    if not callable(function):
        raise RuntimeError(f"Missing reflected method {name} on {obj}")
    return function(*args)


def actor_has_tag(actor, tag):
    try:
        return tag in [str(value) for value in actor.get_editor_property("tags")]
    except Exception:
        return False


class TestState:
    def __init__(self):
        self.phase = "load_map"
        self.phase_elapsed = 0.0
        self.started_at = time.monotonic()
        self.callback = None
        self.player = None
        self.controller = None
        self.partner = None
        self.emote = None
        self.intimacy = None
        self.targeting = None
        self.result = {}
        self.error = ""


STATE = TestState()


def write_result(success):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "success": bool(success),
        "map": TARGET_MAP,
        "contract": "T-selected Male enemy -> Minus intimacy -> UnPossess -> Possess",
        "result": STATE.result,
        "error": STATE.error,
    }
    OUTPUT_FILE.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    unreal.log("[TargetingPawnChangeQA] " + json.dumps(document, ensure_ascii=True))


def finish(success):
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_targeting_pawn_change_callback = None
    write_result(success)
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.SystemLibrary.quit_editor()


def resolve_context():
    world = game_world()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    controller = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
    enemy_class = unreal.load_class(
        None,
        "/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C",
    )
    partners = unreal.GameplayStatics.get_all_actors_of_class(world, enemy_class) if world and enemy_class else []
    emote = None
    try:
        emote = unreal.ProjectRuntimeReflectionLibrary.get_project_emote_subsystem(world)
    except Exception:
        pass
    if not emote:
        emote = world_subsystem(world, "/Script/EFProjectSystemsGameplay.ProjectEmoteSubsystem")
    intimacy = world_subsystem(world, "/Script/EFProjectSystemsGameplay.ProjectIntimacySubsystem")
    targeting = component(player, "ProjectTargetingFixComponent")
    if not all((world, player, controller, partners, emote, intimacy, targeting)):
        return False
    STATE.player = player
    STATE.controller = controller
    STATE.partner = partners[0]
    STATE.emote = emote
    STATE.intimacy = intimacy
    STATE.targeting = targeting
    return True


def tick(delta_time):
    try:
        STATE.phase_elapsed += delta_time
        if time.monotonic() - STATE.started_at > TIMEOUT_SECONDS:
            STATE.error = "timeout:" + STATE.phase
            finish(False)
            return

        if STATE.phase == "load_map":
            STATE.phase = "wait_map"
            STATE.phase_elapsed = 0.0
            LEVEL_EDITOR.load_level(TARGET_MAP)
            return

        if STATE.phase == "wait_map":
            if world_name(UNREAL_EDITOR.get_editor_world()) != TARGET_MAP_NAME or STATE.phase_elapsed < 1.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor() or STATE.phase_elapsed < 1.5 or not resolve_context():
                return
            STATE.result["target_selected"] = bool(
                invoke(STATE.targeting, "debug_set_current_target_actor", STATE.partner)
            )
            STATE.result["quick_start_returned"] = bool(
                invoke(STATE.intimacy, "request_quick_start_intimacy")
            )
            STATE.phase = "wait_active"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_active":
            active = bool(invoke(STATE.intimacy, "is_intimacy_session_active"))
            action = bool(invoke(STATE.emote, "is_runtime_action_active"))
            if active and action:
                STATE.result["session_was_active"] = True
                STATE.result["runtime_action_id"] = str(
                    invoke(STATE.emote, "get_active_runtime_action_id")
                )
                STATE.result["unpossess_called"] = bool(
                    unreal.ProjectRuntimeReflectionLibrary.invoke_no_arg_function(
                        STATE.controller, "UnPossess"
                    )
                )
                if not STATE.result["unpossess_called"]:
                    raise RuntimeError("Reflected Controller.UnPossess call failed")
                STATE.phase = "wait_unpossessed"
                STATE.phase_elapsed = 0.0
                return
            if STATE.phase_elapsed > 15.0:
                STATE.error = "intimacy_did_not_activate"
                finish(False)
            return

        if STATE.phase == "wait_unpossessed":
            if STATE.phase_elapsed < 2.0:
                return
            STATE.result["pawn_after_unpossess"] = str(
                unreal.GameplayStatics.get_player_pawn(game_world(), 0)
            )
            STATE.result["repossess_called"] = bool(
                unreal.ProjectRuntimeReflectionLibrary.invoke_object_arg_function(
                    STATE.controller, "Possess", STATE.player
                )
            )
            if not STATE.result["repossess_called"]:
                raise RuntimeError("Reflected Controller.Possess call failed")
            STATE.phase = "wait_repossessed"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_repossessed":
            if STATE.phase_elapsed < 4.0:
                return
            current_pawn = unreal.GameplayStatics.get_player_pawn(game_world(), 0)
            STATE.result.update(
                {
                    "pawn_restored": current_pawn == STATE.player,
                    "session_active_after_cycle": bool(
                        invoke(STATE.intimacy, "is_intimacy_session_active")
                    ),
                    "runtime_action_active_after_cycle": bool(
                        invoke(STATE.emote, "is_runtime_action_active")
                    ),
                    "move_input_ignored": bool(STATE.controller.is_move_input_ignored()),
                    "look_input_ignored": bool(STATE.controller.is_look_input_ignored()),
                    "player_shield_tag": actor_has_tag(STATE.player, "Project.Intimacy.CombatShield"),
                    "partner_shield_tag": actor_has_tag(STATE.partner, "Project.Intimacy.CombatShield"),
                }
            )
            success = all(
                (
                    STATE.result.get("target_selected"),
                    STATE.result.get("quick_start_returned"),
                    STATE.result.get("session_was_active"),
                    STATE.result.get("runtime_action_id") == "Actions.Together.0001Scene",
                    STATE.result.get("unpossess_called"),
                    STATE.result.get("repossess_called"),
                    STATE.result.get("pawn_restored"),
                    not STATE.result.get("session_active_after_cycle"),
                    not STATE.result.get("runtime_action_active_after_cycle"),
                    not STATE.result.get("move_input_ignored"),
                    not STATE.result.get("look_input_ignored"),
                    not STATE.result.get("player_shield_tag"),
                    not STATE.result.get("partner_shield_tag"),
                )
            )
            if not success:
                STATE.error = "post_possession_cycle_contract_failed"
            finish(success)
    except Exception as exc:
        STATE.error = f"{exc}\n{traceback.format_exc()}"
        finish(False)


old_callback = getattr(builtins, "_codex_targeting_pawn_change_callback", None)
if old_callback is not None:
    try:
        unreal.unregister_slate_post_tick_callback(old_callback)
    except Exception:
        pass
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_targeting_pawn_change_callback = STATE.callback
