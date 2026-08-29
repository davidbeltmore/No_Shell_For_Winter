"""Focused PIE validation for the reworked T -> Minus Intimacy contract.

The run is deliberately transient: Charisma and Curse are prepared only in the
PIE world, then the session is cancelled through the same Y route used by the
player.  The JSON artifact records the Climax/Rush/Curse state transitions.
"""

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
TARGET_CHARACTER_CLASS = os.environ.get(
    "CODEX_ENEMY_INTIMACY_QA_CHARACTER_CLASS",
    "/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C",
)
TIMEOUT_SECONDS = 90.0
CURSE_SEED_PERCENT = 0.80
CURSE_OBSERVATION_SECONDS = 3.2
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


def value(obj, name, default=None):
    try:
        return getattr(obj, name)
    except Exception:
        return default


def enum_name(enum_value):
    return str(enum_value).rsplit(".", 1)[-1].split(":", 1)[0].strip("<> ")


def snapshot_payload(snapshot):
    return {
        "active": bool(value(snapshot, "active", False)),
        "hud_visible": bool(value(snapshot, "hud_visible", False)),
        "player_climax_percent": float(value(snapshot, "player_climax", 0.0)),
        "partner_climax_percent": float(value(snapshot, "partner_climax", 0.0)),
        "player_climax_percent_per_second": float(value(snapshot, "player_climax_per_second", 0.0)),
        "partner_climax_percent_per_second": float(value(snapshot, "partner_climax_per_second", 0.0)),
        "session_state": enum_name(value(snapshot, "session_state", "")),
        "player_orgasm_rush": bool(value(snapshot, "player_orgasm_rush", False)),
        "partner_orgasm_rush": bool(value(snapshot, "partner_orgasm_rush", False)),
        "player_orgasm_rush_remaining": float(value(snapshot, "player_orgasm_rush_remaining", 0.0)),
        "partner_orgasm_rush_remaining": float(value(snapshot, "partner_orgasm_rush_remaining", 0.0)),
        "player_orgasm_count": int(value(snapshot, "player_orgasm_count", 0)),
        "partner_orgasm_count": int(value(snapshot, "partner_orgasm_count", 0)),
        "curse_reduction_percent_per_second": float(
            value(snapshot, "curse_reduction_percent_per_second", 0.0)
        ),
        "status_text": str(value(snapshot, "status_text", "")),
        "hint_text": str(value(snapshot, "hint_text", "")),
    }


def make_valid_guid():
    guid = unreal.Guid()
    # UE 5.8 exposes FGuid as a zero-argument StructBase with four int32
    # editor properties, rather than accepting four constructor arguments.
    guid.set_editor_property("a", 0x49A8D20B)
    guid.set_editor_property("b", 0x134B4F6D)
    guid.set_editor_property("c", 0x213EE327)
    guid.set_editor_property("d", 0x5BD0C982)
    return guid


def seed_transient_curse(doctrine):
    curse_max = float(call(doctrine, "get_curse_max"))
    call(doctrine, "cleanse_curse", curse_max, False)
    context = unreal.ProjectCurseApplicationContext()
    context.set_editor_property("amount", curse_max * CURSE_SEED_PERCENT)
    context.set_editor_property("source_kind", unreal.ProjectCurseSourceKind.DEBUG)
    context.set_editor_property("application_id", make_valid_guid())
    context.set_editor_property("resistible", False)
    context.set_editor_property("can_trigger_cursed", False)
    result = call(doctrine, "apply_curse", context)
    return {
        "maximum": curse_max,
        "requested": float(value(result, "requested_amount", 0.0)),
        "applied": float(value(result, "applied_amount", 0.0)),
        "resulting_curse": float(call(doctrine, "get_curse")),
        "duplicate": bool(value(result, "duplicate", False)),
        "invalid_application_id": bool(value(result, "rejected_invalid_application_id", False)),
    }


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
        self.doctrine = None
        self.pre = {}
        self.started = {}
        self.observed = {}
        self.first_orgasm = {}
        self.second_orgasm = {}
        self.cross_participant_orgasm = {}
        self.cancelled = {}
        self.error = ""
        self.last_context_log_at = 0.0
        self.curse_observation_started_at = 0.0


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
        "observed": STATE.observed,
        "first_orgasm": STATE.first_orgasm,
        "second_orgasm": STATE.second_orgasm,
        "cross_participant_orgasm": STATE.cross_participant_orgasm,
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
    enemy_class = unreal.load_class(None, TARGET_CHARACTER_CLASS)
    enemies = unreal.GameplayStatics.get_all_actors_of_class(world, enemy_class) if world and enemy_class else []
    partner = enemies[0] if enemies else None
    if world and player and enemy_class and not partner:
        partner = unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(
            world,
            TARGET_CHARACTER_CLASS,
            player.get_actor_location() + unreal.Vector(250.0, 0.0, 0.0),
            player.get_actor_rotation(),
        )
    emote = None
    try:
        emote = unreal.ProjectRuntimeReflectionLibrary.get_project_emote_subsystem(world)
    except Exception:
        pass
    if not emote:
        emote = get_world_subsystem(world, "/Script/EFProjectSystemsGameplay.ProjectEmoteSubsystem")
    intimacy = get_world_subsystem(world, "/Script/EFProjectSystemsGameplay.ProjectIntimacySubsystem")
    targeting = find_component(player, "ProjectTargetingFixComponent")
    doctrine = find_component(player, "ProjectInnerDoctrineComponent")
    if not doctrine:
        doctrine = find_component(controller, "ProjectInnerDoctrineComponent")
    if not all((world, player, controller, partner, emote, intimacy, targeting, doctrine)):
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
                        "doctrine": bool(doctrine),
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
    STATE.doctrine = doctrine
    STATE.emote_component = find_component(player, "ProjectEmoteComponent")
    return True


def inspect_started():
    snapshot = snapshot_payload(call(STATE.intimacy, "build_snapshot"))
    return {
        "quick_start_returned": STATE.started.get("quick_start_returned", False),
        "session_active": bool(call(STATE.intimacy, "is_intimacy_session_active")),
        "runtime_action_active": bool(call(STATE.emote, "is_runtime_action_active")),
        "runtime_action_id": str(call(STATE.emote, "get_active_runtime_action_id")),
        "partner_has_identity_component": find_component(
            STATE.partner,
            "ProjectIntimacyPartnerComponent",
        ) is not None,
        "interaction_target_preserved": call(STATE.targeting, "get_current_target_actor") == STATE.partner,
        "active_interaction_target_preserved": (
            STATE.emote_component is not None
            and call(STATE.emote_component, "get_current_interaction_target_actor") == STATE.partner
        ),
        "player_shield_tag": actor_has_tag(STATE.player, "Project.Intimacy.CombatShield"),
        "partner_shield_tag": actor_has_tag(STATE.partner, "Project.Intimacy.CombatShield"),
        "move_input_ignored": bool(STATE.controller.is_move_input_ignored()),
        "look_input_ignored": bool(STATE.controller.is_look_input_ignored()),
        "passive_curse_decay_suppressed": bool(
            call(STATE.doctrine, "is_passive_curse_decay_suppressed")
        ),
        "curse": float(call(STATE.doctrine, "get_curse")),
        "curse_maximum": float(call(STATE.doctrine, "get_curse_max")),
        "snapshot": snapshot,
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
        "passive_curse_decay_suppressed": bool(
            call(STATE.doctrine, "is_passive_curse_decay_suppressed")
        ),
        "curse": float(call(STATE.doctrine, "get_curse")),
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
            charisma_attribute = unreal.ProjectDoctrineAttribute.CHARISMA
            charisma_before = int(call(STATE.doctrine, "get_attribute_level", charisma_attribute))
            if charisma_before != 10:
                if not call(
                    STATE.doctrine,
                    "apply_free_attribute_levels",
                    charisma_attribute,
                    10 - charisma_before,
                ):
                    raise RuntimeError("Could not raise transient PIE Charisma to level 10.")
            charisma_after = int(call(STATE.doctrine, "get_attribute_level", charisma_attribute))
            curse_seed = seed_transient_curse(STATE.doctrine)
            preexisting_partner_component = unreal.ProjectIntimacyPartnerComponent.find_or_create_for_actor(
                STATE.partner
            )
            if not preexisting_partner_component:
                raise RuntimeError("Could not reproduce the pre-audited identity component state.")
            selected = bool(call(STATE.targeting, "debug_set_current_target_actor", STATE.partner))
            STATE.pre = {
                "player": STATE.player.get_name(),
                "partner": STATE.partner.get_name(),
                "target_selected": selected,
                "partner_has_identity_component": find_component(STATE.partner, "ProjectIntimacyPartnerComponent") is not None,
                "partner_component_precreated": True,
                "charisma_before": charisma_before,
                "charisma_after": charisma_after,
                "curse_seed": curse_seed,
            }
            STATE.phase = "wait_policy_refresh"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_policy_refresh":
            if STATE.phase_elapsed < 0.75:
                return
            STATE.started["quick_start_returned"] = bool(call(STATE.intimacy, "request_quick_start_intimacy"))
            STATE.phase = "wait_started"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_started":
            current = inspect_started()
            if current["session_active"] and current["runtime_action_active"]:
                STATE.started = current
                STATE.curse_observation_started_at = time.monotonic()
                STATE.phase = "observe_curse_and_climax"
                STATE.phase_elapsed = 0.0
                return
            if STATE.phase_elapsed > 12.0:
                STATE.started = current
                STATE.error = "quick_start_did_not_reach_active_session"
                finish(False)
            return


        if STATE.phase == "observe_curse_and_climax":
            if STATE.phase_elapsed < CURSE_OBSERVATION_SECONDS:
                return
            current = inspect_started()
            wall_clock_seconds = time.monotonic() - STATE.curse_observation_started_at
            gameplay_seconds = STATE.phase_elapsed
            curse_maximum = max(0.001, float(STATE.started.get("curse_maximum", 100.0)))
            curse_reduction = float(STATE.started.get("curse", 0.0)) - float(current.get("curse", 0.0))
            normalized_reduction = (curse_reduction / curse_maximum) * 100.0
            current.update(
                {
                    "observed_gameplay_seconds": gameplay_seconds,
                    "observed_wall_clock_seconds": wall_clock_seconds,
                    "curse_reduction": curse_reduction,
                    "normalized_curse_reduction_percent": normalized_reduction,
                    "expected_normalized_reduction_percent": gameplay_seconds,
                    "within_one_percent_per_second_tolerance": abs(
                        normalized_reduction - gameplay_seconds
                    ) <= 0.65,
                }
            )
            STATE.observed = current
            STATE.first_orgasm["force_returned"] = bool(
                call(STATE.intimacy, "force_partner_orgasm_for_automation")
            )
            STATE.phase = "wait_first_orgasm"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_first_orgasm":
            if STATE.phase_elapsed < 0.25:
                return
            current = inspect_started()
            STATE.first_orgasm.update(current)
            STATE.second_orgasm["force_returned"] = bool(
                call(STATE.intimacy, "force_partner_orgasm_for_automation")
            )
            STATE.phase = "wait_second_orgasm"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_second_orgasm":
            if STATE.phase_elapsed < 0.25:
                return
            current = inspect_started()
            STATE.second_orgasm.update(current)
            STATE.cross_participant_orgasm["force_returned"] = bool(
                call(STATE.intimacy, "force_player_orgasm_for_automation")
            )
            STATE.phase = "wait_cross_participant_orgasm"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_cross_participant_orgasm":
            if STATE.phase_elapsed < 0.25:
                return
            current = inspect_started()
            STATE.cross_participant_orgasm.update(current)
            call(STATE.emote, "request_toggle_emote_menu")
            STATE.phase = "wait_cancelled"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_cancelled":
            current = inspect_cancelled()
            if not current["session_active"] and not current["runtime_action_active"] and STATE.phase_elapsed >= 3.5:
                STATE.cancelled = current
                success = all(
                    (
                        STATE.pre.get("target_selected"),
                        STATE.pre.get("partner_component_precreated"),
                        STATE.pre.get("charisma_after") == 10,
                        STATE.pre.get("curse_seed", {}).get("applied", 0.0) > 0.0,
                        STATE.started.get("quick_start_returned"),
                        STATE.started.get("session_active"),
                        STATE.started.get("runtime_action_active"),
                        STATE.started.get("runtime_action_id") == "Actions.Together.0001Scene",
                        STATE.started.get("active_interaction_target_preserved"),
                        STATE.started.get("partner_has_identity_component"),
                        STATE.started.get("passive_curse_decay_suppressed"),
                        STATE.started.get("snapshot", {}).get("hud_visible"),
                        abs(
                            STATE.started.get("snapshot", {}).get(
                                "curse_reduction_percent_per_second", 0.0
                            )
                            - 1.0
                        )
                        <= 0.001,
                        STATE.observed.get("within_one_percent_per_second_tolerance"),
                        STATE.observed.get("snapshot", {}).get("player_climax_percent", 0.0) > 0.0,
                        STATE.observed.get("snapshot", {}).get("partner_climax_percent", 0.0) > 0.0,
                        STATE.first_orgasm.get("force_returned"),
                        STATE.first_orgasm.get("session_active"),
                        STATE.first_orgasm.get("runtime_action_active"),
                        STATE.first_orgasm.get("snapshot", {}).get("partner_orgasm_count", 0) >= 1,
                        STATE.first_orgasm.get("snapshot", {}).get("partner_orgasm_rush"),
                        STATE.first_orgasm.get("snapshot", {}).get("session_state") == "ORGASM_RUSH",
                        STATE.second_orgasm.get("force_returned"),
                        STATE.second_orgasm.get("session_active"),
                        STATE.second_orgasm.get("runtime_action_active"),
                        STATE.second_orgasm.get("snapshot", {}).get("partner_orgasm_count", 0) >= 2,
                        "Partner orgasm x2" in STATE.second_orgasm.get("snapshot", {}).get("status_text", ""),
                        STATE.cross_participant_orgasm.get("force_returned"),
                        STATE.cross_participant_orgasm.get("session_active"),
                        STATE.cross_participant_orgasm.get("runtime_action_active"),
                        STATE.cross_participant_orgasm.get("snapshot", {}).get("player_orgasm_count", 0) >= 1,
                        STATE.cross_participant_orgasm.get("snapshot", {}).get("partner_orgasm_count", 0) >= 2,
                        STATE.cross_participant_orgasm.get("snapshot", {}).get("player_orgasm_rush"),
                        not STATE.cross_participant_orgasm.get("snapshot", {}).get("partner_orgasm_rush"),
                        "Player orgasm x1" in STATE.cross_participant_orgasm.get("snapshot", {}).get("status_text", ""),
                        not current["move_input_ignored"],
                        not current["look_input_ignored"],
                        not current["player_shield_tag"],
                        not current["partner_shield_tag"],
                        current["interaction_target_preserved"],
                        current["partner_brain_paused"] is not True,
                        not current["passive_curse_decay_suppressed"],
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
