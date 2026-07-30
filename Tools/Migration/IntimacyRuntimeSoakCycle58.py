"""One deterministic full Intimacy cycle with equipment isolation assertions."""

import json
import os
import runpy
from pathlib import Path

import unreal


SOURCE_VISUAL_SCRIPT = os.environ.get(
    "CODEX_INTIMACY_BASE_SCRIPT",
    r"D:\Projects UE5\LustAsDeadlySin\Tools\Intimacy\test_project_intimacy_visual_runtime.py",
)
GENDER = os.environ.get("CODEX_INTIMACY_SOAK_GENDER", "Male").strip().capitalize()
RESULT_FILE = Path(os.environ["CODEX_INTIMACY_SOAK_RESULT"])
CLASS_PATHS = {
    "Male": "/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C",
    "Female": "/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C",
}
EQUIPMENT_TOKENS = ("weapon", "itemactor", "equipment", "sword", "bowactor", "shield", "quiver", "arrowactor")
EQUIPMENT_ASSET_TOKENS = ("/weapons/", "/items/", "/equipment/")

if GENDER not in CLASS_PATHS:
    raise RuntimeError("CODEX_INTIMACY_SOAK_GENDER must be Male or Female")

namespace = runpy.run_path(SOURCE_VISUAL_SCRIPT, run_name="__codex_intimacy_runtime_soak_cycle__")
original_prime_current_target = namespace["prime_current_target"]
original_mark_visual_capture = namespace["mark_visual_capture"]
original_emit = namespace["emit"]
original_finish = namespace["finish"]

SOAK = {
    "gender": GENDER,
    "class_path": CLASS_PATHS[GENDER],
    "pre": {},
    "active": {},
    "restored": {},
    "active_equipment_suppressed": False,
    "active_suppressed_equipment_count": 0,
    "equipment_restored_exactly": False,
    "base_success": False,
    "success": False,
    "error": "",
}


def object_path(value):
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return str(value)


def component_asset_path(component):
    for method_name in ("get_skeletal_mesh_asset", "get_static_mesh", "get_asset"):
        try:
            asset = getattr(component, method_name)()
            if asset:
                return object_path(asset)
        except Exception:
            continue
    return ""


def attached_actors(actor):
    if not actor:
        return []
    output = []
    try:
        actor.get_attached_actors(output, True, True)
        return list(output)
    except Exception:
        try:
            return list(actor.get_attached_actors())
        except Exception:
            return []


def is_equipment_actor(actor):
    identity = f"{actor.get_name()} {object_path(actor.get_class())}".lower()
    if any(token in identity for token in EQUIPMENT_TOKENS):
        return True
    try:
        components = actor.get_components_by_class(unreal.ActorComponent)
    except Exception:
        components = []
    for component in components:
        asset_path = component_asset_path(component).lower()
        if any(token in asset_path for token in EQUIPMENT_ASSET_TOKENS):
            return True
    return False


def actor_hidden(actor):
    try:
        return bool(actor.is_hidden())
    except Exception:
        return None


def actor_collision(actor):
    try:
        return bool(actor.get_actor_enable_collision())
    except Exception:
        return None


def collect_equipment(player, target):
    result = {}
    for role, participant in (("player", player), ("target", target)):
        for actor in attached_actors(participant):
            if not actor or not is_equipment_actor(actor):
                continue
            path = object_path(actor)
            result[path] = {
                "role": role,
                "name": actor.get_name(),
                "hidden": actor_hidden(actor),
                "collision": actor_collision(actor),
            }
    return result


def current_participants():
    world = namespace["get_game_world"]()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    return player, namespace["STATE"].target_actor


def find_gender_target(world, player_pawn):
    target_class = unreal.load_class(None, CLASS_PATHS[GENDER])
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, target_class) if world and target_class else []
    target = actors[0] if actors else None
    if not target and world and player_pawn and target_class:
        target = unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(
            world,
            CLASS_PATHS[GENDER],
            player_pawn.get_actor_location() + unreal.Vector(250.0, 0.0, 0.0),
            player_pawn.get_actor_rotation(),
        )
    if not target:
        raise RuntimeError(f"Could not resolve or spawn {GENDER} Intimacy target")
    return target


def prime_current_target():
    world = namespace["get_game_world"]()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    target = find_gender_target(world, player)
    namespace["STATE"].target_actor = target
    emote_component = namespace["get_emote_component"]()
    if not emote_component:
        return False
    emote_component.set_debug_blueprint_scene_target_actor(target)
    namespace["STATE"].targeting_prime = {
        "player_pawn": object_path(player),
        "candidate_target": object_path(target),
        "debug_target_set": True,
    }
    if not SOAK["pre"]:
        SOAK["pre"] = collect_equipment(player, target)
    return True


def mark_visual_capture(label, state):
    if label == "IntimacyHud_0001Scene" and not SOAK["active"]:
        player, target = current_participants()
        SOAK["active"] = collect_equipment(player, target)
        emote_component = namespace["get_emote_component"]()
        try:
            SOAK["active_suppressed_equipment_count"] = int(
                emote_component.automation_get_suppressed_blueprint_scene_equipment_count()
            )
        except Exception:
            SOAK["active_suppressed_equipment_count"] = 0
        SOAK["active_equipment_suppressed"] = (
            bool(SOAK["active"])
            and SOAK["active_suppressed_equipment_count"] == len(SOAK["active"])
            and all(entry["collision"] is False for entry in SOAK["active"].values())
        )
    return original_mark_visual_capture(label, state)


def emit(message):
    if message.startswith("milky_splash_cleanup_ok=") and not SOAK["restored"]:
        player, target = current_participants()
        SOAK["restored"] = collect_equipment(player, target)
        SOAK["equipment_restored_exactly"] = SOAK["restored"] == SOAK["pre"]
    return original_emit(message)


def write_result():
    RESULT_FILE.parent.mkdir(parents=True, exist_ok=True)
    RESULT_FILE.write_text(json.dumps(SOAK, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def finish(success):
    if namespace["STATE"].finished:
        return
    SOAK["base_success"] = bool(success)
    SOAK["success"] = bool(
        success
        and SOAK["active_equipment_suppressed"]
        and SOAK["equipment_restored_exactly"]
        and len(SOAK["pre"]) >= 1
    )
    if not SOAK["success"]:
        SOAK["error"] = "runtime_cycle_or_equipment_contract_failed"
    write_result()
    return original_finish(SOAK["success"])


original_prime_current_target.__globals__["find_candidate_target"] = find_gender_target
original_prime_current_target.__globals__["prime_current_target"] = prime_current_target
original_mark_visual_capture.__globals__["mark_visual_capture"] = mark_visual_capture
original_emit.__globals__["emit"] = emit
original_finish.__globals__["finish"] = finish
