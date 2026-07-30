"""Focused PIE validation for T -> Minus intimacy quick start and Y cancellation."""

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
        "CODEX_ENEMY_INTIMACY_QA_OUTPUT",
        PROJECT_DIR / "Saved" / "Migration" / "Phase5" / "Runtime" / "EnemyIntimacyQuickStartPIE58.json",
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


def get_game_world():
    return EDITOR_LEVEL_LIBRARY.get_game_world()


def get_world_subsystem(world, class_path):
    cls = unreal.load_class(None, class_path)
    if not world or not cls:
        return None
    try:
        subsystem = unreal.SubsystemBlueprintLibrary.get_world_subsystem(world, cls)
        if subsystem:
            return subsystem
    except Exception:
        pass
    world_path = world.get_path_name()
    for obj in unreal.ObjectIterator(unreal.Object):
        try:
            if obj.get_class() == cls and obj.get_path_name().startswith(world_path):
                return obj
        except Exception:
            continue
    return None


def find_component(actor, class_name_fragment):
    if not actor:
        return None
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if class_name_fragment in component.get_class().get_name():
            return component
    return None


def call(obj, snake_name, *args):
    fn = getattr(obj, snake_name, None)
    if not callable(fn):
        raise RuntimeError(f"Missing reflected method {snake_name} on {obj}")
    return fn(*args)


def actor_has_tag(actor, tag):
    try:
        return tag in [str(value) for value in actor.get_editor_property("tags")]
    except Exception:
        return False


class State:
    def __init__(self):
        self.phase = "load_map"
        self.started_at = time.monotonic()
        self.elapsed = 0.0
        self.phase_elapsed = 0.0
        self.callback = None
        self.player = None
        self.partner = None
        self.controller = None
        self.emote = None
        self.intimacy = None
        self.targeting = None
        self.emote_component = None
        self.pre = {}
        self.started = {}
        self.cancelled = {}
        self.error = ""
        self.last_context_log_at = 0.0


STATE = State()


def write_result(success):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    result = {
        "success": bool(success),
        "map": TARGET_MAP,
        "contract": {
            "selection": "T-equivalent DebugSetCurrentTargetActor",
            "quick_start": "RequestQuickStartIntimacy (Minus binding)",
            "cancel": "RequestToggleEmoteMenu while runtime action active (Y binding)",
        },
        "pre": STATE.pre,
        "started": STATE.started,
        "cancelled": STATE.cancelled,
        "error": STATE.error,
    }
    OUTPUT_FILE.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    unreal.log("[EnemyIntimacyQuickStartQA] " + json.dumps(result, ensure_ascii=True))


def finish(success):
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_enemy_intimacy_quick_start_callback = None
    write_result(success)
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.SystemLibrary.quit_editor()


def resolve_context():
    world = get_game_world()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    controller = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
    enemy_class = unreal.load_class(
        None,
        "/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C",
    )
    enemies = unreal.GameplayStatics.get_all_actors_of_class(world, enemy_class) if world and enemy_class else []
    partner = enemies[0] if enemies else None
    emote = None
    try:
        emote = unreal.ProjectRuntimeReflectionLibrary.get_project_emote_subsystem(world)
    except Exception:
        pass
    if not emote:
        emote = get_world_subsystem(world, "/Script/EFProjectSystemsGameplay.ProjectEmoteSubsystem")
    intimacy = get_world_subsystem(world, "/Script/EFProjectSystemsGameplay.ProjectIntimacySubsystem")
    targeting = find_component(player, "ProjectTargetingFixComponent")
    if not all((world, player, controller, partner, emote, intimacy, targeting)):
        now = time.monotonic()
        if now - STATE.last_context_log_at >= 5.0:
            STATE.last_context_log_at = now
            unreal.log(
                "[EnemyIntimacyQuickStartQA] waiting_context="
                + json.dumps(
                    {
                        "world": bool(world),
                        "player": bool(player),
                        "controller": bool(controller),
                        "partner": bool(partner),
                        "emote": bool(emote),
                        "intimacy": bool(intimacy),
                        "targeting": bool(targeting),
                    },
                    ensure_ascii=True,
                )
            )
        return False
    STATE.player = player
    STATE.controller = controller
    STATE.partner = partner
    STATE.emote = emote
    STATE.intimacy = intimacy
    STATE.targeting = targeting
    STATE.emote_component = find_component(player, "ProjectEmoteComponent")
    return True


def inspect_started():
    return {
        "quick_start_returned": STATE.started.get("quick_start_returned", False),
        "session_active": bool(call(STATE.intimacy, "is_intimacy_session_active")),
        "runtime_action_active": bool(call(STATE.emote, "is_runtime_action_active")),
        "runtime_action_id": str(call(STATE.emote, "get_active_runtime_action_id")),
        "interaction_target_preserved": call(STATE.targeting, "get_current_target_actor") == STATE.partner,
        "active_interaction_target_preserved": (
            STATE.emote_component is not None
            and call(STATE.emote_component, "get_current_interaction_target_actor") == STATE.partner
        ),
        "player_shield_tag": actor_has_tag(STATE.player, "Project.Intimacy.CombatShield"),
        "partner_shield_tag": actor_has_tag(STATE.partner, "Project.Intimacy.CombatShield"),
        "move_input_ignored": bool(STATE.controller.is_move_input_ignored()),
        "look_input_ignored": bool(STATE.controller.is_look_input_ignored()),
    }


def inspect_cancelled():
    partner_controller = STATE.partner.get_controller() if hasattr(STATE.partner, "get_controller") else None
    brain = find_component(partner_controller, "BrainComponent") if partner_controller else None
    brain_paused = None
    if brain:
        try:
            brain_paused = bool(brain.is_paused())
        except Exception:
            brain_paused = None
    return {
        "session_active": bool(call(STATE.intimacy, "is_intimacy_session_active")),
        "runtime_action_active": bool(call(STATE.emote, "is_runtime_action_active")),
        "interaction_target_preserved": call(STATE.targeting, "get_current_target_actor") == STATE.partner,
        "player_shield_tag": actor_has_tag(STATE.player, "Project.Intimacy.CombatShield"),
        "partner_shield_tag": actor_has_tag(STATE.partner, "Project.Intimacy.CombatShield"),
        "move_input_ignored": bool(STATE.controller.is_move_input_ignored()),
        "look_input_ignored": bool(STATE.controller.is_look_input_ignored()),
        "partner_brain_paused": brain_paused,
    }


def tick(delta_time):
    try:
        STATE.elapsed += delta_time
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
            editor_world = UNREAL_EDITOR.get_editor_world()
            if world_name(editor_world) != TARGET_MAP_NAME or STATE.phase_elapsed < 1.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor() or STATE.phase_elapsed < 1.5 or not resolve_context():
                return
            selected = bool(call(STATE.targeting, "debug_set_current_target_actor", STATE.partner))
            STATE.pre = {
                "player": STATE.player.get_name(),
                "partner": STATE.partner.get_name(),
                "target_selected": selected,
                "partner_has_identity_component": find_component(STATE.partner, "ProjectIntimacyPartnerComponent") is not None,
            }
            STATE.started["quick_start_returned"] = bool(call(STATE.intimacy, "request_quick_start_intimacy"))
            STATE.phase = "wait_started"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_started":
            current = inspect_started()
            if current["session_active"] and current["runtime_action_active"]:
                STATE.started = current
                call(STATE.emote, "request_toggle_emote_menu")
                STATE.phase = "wait_cancelled"
                STATE.phase_elapsed = 0.0
                return
            if STATE.phase_elapsed > 12.0:
                STATE.started = current
                STATE.error = "quick_start_did_not_reach_active_session"
                finish(False)
            return

        if STATE.phase == "wait_cancelled":
            current = inspect_cancelled()
            if not current["session_active"] and not current["runtime_action_active"] and STATE.phase_elapsed >= 3.5:
                STATE.cancelled = current
                success = all(
                    (
                        STATE.pre.get("target_selected"),
                        STATE.pre.get("partner_has_identity_component"),
                        STATE.started.get("quick_start_returned"),
                        STATE.started.get("session_active"),
                        STATE.started.get("runtime_action_active"),
                        STATE.started.get("runtime_action_id") == "Actions.Together.0001Scene",
                        STATE.started.get("active_interaction_target_preserved"),
                        not current["move_input_ignored"],
                        not current["look_input_ignored"],
                        not current["player_shield_tag"],
                        not current["partner_shield_tag"],
                        current["interaction_target_preserved"],
                        current["partner_brain_paused"] is not True,
                    )
                )
                if not success:
                    STATE.error = "post_cancel_contract_failed"
                finish(success)
                return
            if STATE.phase_elapsed > 12.0:
                STATE.cancelled = current
                STATE.error = "cancel_did_not_restore_runtime_state"
                finish(False)
            return
    except Exception as exc:
        STATE.error = f"{exc}\n{traceback.format_exc()}"
        finish(False)


old_callback = getattr(builtins, "_codex_enemy_intimacy_quick_start_callback", None)
if old_callback is not None:
    try:
        unreal.unregister_slate_post_tick_callback(old_callback)
    except Exception:
        pass
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_enemy_intimacy_quick_start_callback = STATE.callback
