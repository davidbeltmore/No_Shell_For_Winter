"""Focused UE 5.8 PIE validation for the organized animation migration.

The script exercises the project-owned locomotion and ProjectEmote runtime
components on the real HUB player. It writes evidence and screenshots under
Saved/ only, restores runtime state, ends PIE, and quits the validation editor.
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
OUTPUT_FILE = EVIDENCE_DIR / "UE58_AnimationMigrationPIE.json"
CRAWL_SCREENSHOT = EVIDENCE_DIR / "UE58_Crawl_Runtime.png"
DANCE_SCREENSHOT = EVIDENCE_DIR / "UE58_Dance_Runtime.png"
FAP_SCREENSHOT = EVIDENCE_DIR / "UE58_Fap_Runtime.png"
EXPECTED_CRAWL_DEFORMER = (
    "/DeformerGraph/Deformers/"
    "DG_DualQuatSkin_Morph_Cloth.DG_DualQuatSkin_Morph_Cloth"
)

TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
TIMEOUT_SECONDS = 120.0

EXPECTED = {
    "walk": "/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04.Anim_KA_Walk04",
    "walk_idle": "/Game/_Game/Animations/Emotes/General/Anim_KA_Idle01_breathing.Anim_KA_Idle01_breathing",
    "walk_pivot": "/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04_Pivot.Anim_KA_Walk04_Pivot",
    "crawl_entry": "/Game/_Game/Animations/Locomotion/Crawl/Anim_KA_Crawling_Baby_Entry.Anim_KA_Crawling_Baby_Entry",
    "crawl_exit": "/Game/_Game/Animations/Locomotion/Crawl/Anim_KA_Crawling_Baby_Exit.Anim_KA_Crawling_Baby_Exit",
    "crawl_idle": "/Game/_Game/Animations/Locomotion/Crawl/Anim_KA_Crawling_Baby_Idle.Anim_KA_Crawling_Baby_Idle",
    "crawl_forward": "/Game/_Game/Animations/Locomotion/Crawl/Anim_KA_Crawling_Baby_Walk_Fwd.Anim_KA_Crawling_Baby_Walk_Fwd",
    "dance": "/Game/_Game/Animations/Emotes/Dance/Anim_KA_Idle63_Dance05_Loop.Anim_KA_Idle63_Dance05_Loop",
    "private_solo": "/Game/_Game/Animations/Intimacy/Female/P_INT_Solo_Private01.P_INT_Solo_Private01",
    "looking_back": "/Game/_Game/Animations/Emotes/General/Anim_KA_Idle11_LookingBack.Anim_KA_Idle11_LookingBack",
    "sit_training": "/Game/_Game/Animations/Emotes/Sit/Anim_KA_Idle53_Seiza_Loop1.Anim_KA_Idle53_Seiza_Loop1",
}

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


def soft_path(value):
    if not value:
        return ""
    try:
        return value.to_soft_object_path().to_string()
    except Exception:
        pass
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def call(obj, snake_name, *args):
    fn = getattr(obj, snake_name, None)
    if not callable(fn):
        raise RuntimeError(f"Missing reflected method {snake_name} on {obj}")
    return fn(*args)


def find_component(actor, class_name_fragment):
    if not actor:
        return None
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if class_name_fragment in component.get_class().get_name():
            return component
    return None


def request_screenshot(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    take_screenshot = getattr(unreal.AutomationLibrary, "take_high_res_screenshot", None)
    if not callable(take_screenshot):
        return {"requested": False, "reason": "AutomationLibrary.take_high_res_screenshot unavailable"}
    return {
        "requested": True,
        "accepted": bool(take_screenshot(1280, 720, str(path))),
        "path": str(path),
    }


def interaction_catalog(component):
    result = {}
    for property_name in ("action_interactions", "object_interactions"):
        try:
            definitions = component.get_editor_property(property_name)
        except Exception:
            definitions = []
        for definition in definitions:
            interaction_id = str(definition.get_editor_property("interaction_id"))
            result[interaction_id] = {
                "animation": soft_path(definition.get_editor_property("animation")),
                "playback_mode": str(definition.get_editor_property("playback_mode")),
                "legacy_emote_type": str(definition.get_editor_property("legacy_emote_type")),
            }
    return result


def training_catalog(component):
    result = {}
    for definition in call(component, "get_available_training_definitions"):
        training_id = str(definition.get_editor_property("training_id"))
        result[training_id] = {
            "animation": soft_path(definition.get_editor_property("training_animation")),
            "display_name": str(definition.get_editor_property("display_name")),
        }
    return result


class State:
    def __init__(self):
        self.phase = "load_map"
        self.phase_elapsed = 0.0
        self.started_at = time.monotonic()
        self.callback = None
        self.player = None
        self.locomotion = None
        self.emote = None
        self.training = None
        self.result = {
            "status": "UE58_ANIMATION_MIGRATION_PIE_FAIL",
            "map": TARGET_MAP,
            "expected": EXPECTED,
            "runtime_assets": {},
            "catalog": {},
            "walk": {},
            "crawl": {},
            "dance": {},
            "fap": {},
            "training": {},
            "screenshots": {},
            "errors": [],
        }


STATE = State()


def locomotion_assets():
    properties = {
        "walk": "walk_loop_animation",
        "walk_idle": "walk_idle_animation",
        "walk_pivot": "walk_pivot_animation",
        "crawl_entry": "crawl_entry_animation",
        "crawl_exit": "crawl_exit_animation",
        "crawl_idle": "crawl_idle_animation",
        "crawl_forward": "crawl_forward_animation",
    }
    return {
        key: soft_path(STATE.locomotion.get_editor_property(property_name))
        for key, property_name in properties.items()
    }


def restore_runtime_state():
    for obj, method, args in (
        (STATE.emote, "stop_emote", (True,)),
        (STATE.training, "cancel_training", ()),
        (STATE.locomotion, "set_crawl_mode_enabled", (False,)),
        (STATE.locomotion, "set_walk_mode_enabled", (False,)),
    ):
        if obj:
            try:
                call(obj, method, *args)
            except Exception:
                pass


def finish(success):
    restore_runtime_state()
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_animation_migration_qa_callback = None
    STATE.result["screenshots"].setdefault("crawl", {})["exists"] = CRAWL_SCREENSHOT.is_file()
    STATE.result["screenshots"].setdefault("dance", {})["exists"] = DANCE_SCREENSHOT.is_file()
    STATE.result["screenshots"].setdefault("fap", {})["exists"] = FAP_SCREENSHOT.is_file()
    STATE.result["status"] = (
        "UE58_ANIMATION_MIGRATION_PIE_PASS"
        if success
        else "UE58_ANIMATION_MIGRATION_PIE_FAIL"
    )
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[AnimationMigrationPIE58] " + json.dumps(STATE.result, ensure_ascii=True))
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def resolve_context():
    world = EDITOR_LEVEL_LIBRARY.get_game_world()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    locomotion = find_component(player, "ProjectLocomotionOverrideComponent")
    emote = find_component(player, "ProjectEmoteComponent")
    training = find_component(player, "ACFTrainingComponent")
    if not all((world, player, locomotion, emote, training)):
        return False
    STATE.player = player
    STATE.locomotion = locomotion
    STATE.emote = emote
    STATE.training = training
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
            STATE.result["player"] = object_path(STATE.player)
            STATE.result["runtime_assets"] = locomotion_assets()
            STATE.result["catalog"]["interactions"] = interaction_catalog(STATE.emote)
            STATE.result["catalog"]["training"] = training_catalog(STATE.training)
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            STATE.phase = "walk"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "walk" and STATE.phase_elapsed >= 0.75:
            STATE.result["walk"] = {
                "enabled": bool(call(STATE.locomotion, "is_walk_mode_enabled")),
                "crawl_active": bool(call(STATE.locomotion, "is_crawl_mode_active")),
                "dependencies": str(call(STATE.locomotion, "describe_resolved_dependencies")),
                "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")),
                "overlay_montage": str(call(STATE.locomotion, "get_active_overlay_montage_name")),
                "deformer_applied": bool(
                    call(STATE.locomotion, "is_crawl_mesh_deformer_applied")
                ),
                "deformer": str(
                    call(STATE.locomotion, "get_active_crawl_mesh_deformer_name")
                ),
            }
            call(STATE.locomotion, "set_crawl_mode_enabled", True)
            STATE.phase = "crawl"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "crawl" and STATE.phase_elapsed >= 1.5:
            STATE.result["crawl"] = {
                "walk_enabled": bool(call(STATE.locomotion, "is_walk_mode_enabled")),
                "active": bool(call(STATE.locomotion, "is_crawl_mode_active")),
                "animation": str(call(STATE.locomotion, "get_current_animation_asset_name")),
                "overlay_montage": str(call(STATE.locomotion, "get_active_overlay_montage_name")),
                "transition_lock": bool(call(STATE.locomotion, "is_transition_movement_lock_active")),
                "deformer_applied": bool(
                    call(STATE.locomotion, "is_crawl_mesh_deformer_applied")
                ),
                "deformer": str(
                    call(STATE.locomotion, "get_active_crawl_mesh_deformer_name")
                ),
            }
            STATE.result["screenshots"]["crawl"] = request_screenshot(CRAWL_SCREENSHOT)
            STATE.phase = "crawl_screenshot"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "crawl_screenshot" and STATE.phase_elapsed >= 1.5:
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
            STATE.result["crawl"]["deformer_restored_after_disable"] = not bool(
                call(STATE.locomotion, "is_crawl_mesh_deformer_applied")
            )
            STATE.result["crawl"]["deformer_after_disable"] = str(
                call(STATE.locomotion, "get_active_crawl_mesh_deformer_name")
            )
            started = bool(call(STATE.emote, "start_interaction_by_id", "Actions.Dance"))
            STATE.result["dance"]["start_returned"] = started
            STATE.phase = "dance"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "dance" and STATE.phase_elapsed >= 1.5:
            STATE.result["dance"].update(
                {
                    "active": bool(call(STATE.emote, "is_emote_active")),
                    "playback_started": bool(call(STATE.emote, "is_emote_playback_started")),
                    "interaction_id": str(call(STATE.emote, "get_active_interaction_id")),
                }
            )
            STATE.result["screenshots"]["dance"] = request_screenshot(DANCE_SCREENSHOT)
            STATE.phase = "dance_screenshot"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "dance_screenshot" and STATE.phase_elapsed >= 1.5:
            call(STATE.emote, "stop_emote", True)
            started = bool(
                call(
                    STATE.emote,
                    "start_runtime_interaction_by_id",
                    "Actions.Masturbate.FunnyTimeRemake",
                )
            )
            STATE.result["fap"]["start_returned"] = started
            STATE.phase = "fap"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "fap" and STATE.phase_elapsed >= 1.75:
            STATE.result["fap"].update(
                {
                    "active": bool(call(STATE.emote, "is_emote_active")),
                    "playback_started": bool(call(STATE.emote, "is_emote_playback_started")),
                    "interaction_id": str(call(STATE.emote, "get_active_interaction_id")),
                    "blueprint_scene": bool(
                        call(STATE.emote, "is_active_interaction_blueprint_scene")
                    ),
                }
            )
            STATE.result["screenshots"]["fap"] = request_screenshot(FAP_SCREENSHOT)
            STATE.phase = "fap_screenshot"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "fap_screenshot" and STATE.phase_elapsed >= 1.5:
            call(STATE.emote, "stop_emote", True)
            training_started = bool(call(STATE.training, "start_training_by_id", "PushUps"))
            STATE.result["training"]["start_returned"] = training_started
            STATE.phase = "training"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "training" and STATE.phase_elapsed >= 1.0:
            STATE.result["training"].update(
                {
                    "active": bool(call(STATE.training, "is_training_active")),
                    "training_id": str(call(STATE.training, "get_active_training_id")),
                }
            )
            runtime_assets_ok = all(
                STATE.result["runtime_assets"].get(key) == EXPECTED[key]
                for key in (
                    "walk",
                    "walk_idle",
                    "walk_pivot",
                    "crawl_entry",
                    "crawl_exit",
                    "crawl_idle",
                    "crawl_forward",
                )
            )
            interactions = STATE.result["catalog"]["interactions"]
            training = STATE.result["catalog"]["training"]
            training_started = bool(STATE.result["training"].get("start_returned"))
            catalog_ok = (
                interactions.get("Actions.Dance", {}).get("animation") == EXPECTED["dance"]
                and interactions.get("Actions.Masturbate.FunnyTimeRemake", {}).get("animation")
                == EXPECTED["fap"]
                and interactions.get("Objects.LookingBack", {}).get("animation")
                == EXPECTED["looking_back"]
                and training.get("PushUps", {}).get("animation") == EXPECTED["sit_training"]
            )
            behavior_ok = all(
                (
                    STATE.result["walk"].get("enabled"),
                    STATE.result["crawl"].get("active"),
                    STATE.result["dance"].get("start_returned"),
                    STATE.result["dance"].get("active"),
                    STATE.result["dance"].get("playback_started"),
                    STATE.result["dance"].get("interaction_id") == "Actions.Dance",
                    STATE.result["fap"].get("start_returned"),
                    STATE.result["fap"].get("active"),
                    STATE.result["fap"].get("playback_started"),
                    STATE.result["fap"].get("interaction_id")
                    == "Actions.Masturbate.FunnyTimeRemake",
                )
            )
            deformer_ok = all(
                (
                    not STATE.result["walk"].get("deformer_applied"),
                    STATE.result["crawl"].get("deformer_applied"),
                    STATE.result["crawl"].get("deformer") == EXPECTED_CRAWL_DEFORMER,
                    STATE.result["crawl"].get("deformer_restored_after_disable"),
                )
            )
            STATE.result["gates"] = {
                "runtime_assets_ok": runtime_assets_ok,
                "catalog_ok": catalog_ok,
                "behavior_ok": behavior_ok,
                "deformer_ok": deformer_ok,
                "training_started": training_started,
            }
            finish(
                runtime_assets_ok
                and catalog_ok
                and behavior_ok
                and deformer_ok
                and training_started
            )
            return
    except Exception as exc:
        STATE.result["errors"].append(f"{exc}\n{traceback.format_exc()}")
        finish(False)


old_callback = getattr(builtins, "_codex_animation_migration_qa_callback", None)
if old_callback is not None:
    try:
        unreal.unregister_slate_post_tick_callback(old_callback)
    except Exception:
        pass

STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_animation_migration_qa_callback = STATE.callback
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.log("[AnimationMigrationPIE58] registered")
