import json
import os

import unreal


GAME_MODE_BP_PATH = "/Game/TattooShop/Blueprints/BP_TSGameMode"
TARGET_GAME_MODE_CLASS_PATH = (
    "/Game/FullSample/Integrations/Ultimate/Blueprint/Game/"
    "ACFUltimateGameModeBP.ACFUltimateGameModeBP_C"
)
TATTOO_CHARACTER_CLASS_PATH = (
    "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar.BP_TSChar_C"
)
HUB_MAP_PATH = "/Game/_Game/Hub/HUB"
DOOR_CLASS_PATH = "/Game/Procedural/DoorToLevel.DoorToLevel_C"


def path_name(value):
    return value.get_path_name() if value else ""


report = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "game_mode_blueprint": GAME_MODE_BP_PATH,
    "target_parent": TARGET_GAME_MODE_CLASS_PATH,
    "default_pawn": TATTOO_CHARACTER_CLASS_PATH,
    "hub_map": HUB_MAP_PATH,
    "status": "FAIL",
}

game_mode_bp = unreal.load_asset(GAME_MODE_BP_PATH)
target_game_mode_class = unreal.load_object(None, TARGET_GAME_MODE_CLASS_PATH)
tattoo_character_class = unreal.load_object(None, TATTOO_CHARACTER_CLASS_PATH)
if not game_mode_bp or not target_game_mode_class or not tattoo_character_class:
    raise RuntimeError(
        "TattooShop runtime classes failed to load: "
        f"game_mode_bp={game_mode_bp} target_parent={target_game_mode_class} "
        f"tattoo_character={tattoo_character_class}"
    )

unreal.BlueprintEditorLibrary.reparent_blueprint(game_mode_bp, target_game_mode_class)
compile_ok = unreal.BlueprintEditorLibrary.compile_blueprint(game_mode_bp)
report["game_mode_compile_ok"] = bool(compile_ok)

game_mode_class = game_mode_bp.generated_class()
if not game_mode_class:
    raise RuntimeError("BP_TSGameMode has no generated class after reparent")

game_mode_cdo = unreal.get_default_object(game_mode_class)
target_game_mode_cdo = unreal.get_default_object(target_game_mode_class)
copied_game_mode_contract = {}
for property_name in (
    "player_controller_class",
    "spectator_class",
    "game_state_class",
    "player_state_class",
    "hud_class",
    "game_session_class",
    "replay_spectator_player_controller_class",
    "server_stat_replicator_class",
):
    try:
        inherited_value = target_game_mode_cdo.get_editor_property(property_name)
        game_mode_cdo.set_editor_property(property_name, inherited_value)
        copied_game_mode_contract[property_name] = path_name(inherited_value)
    except Exception as exc:
        copied_game_mode_contract[property_name] = {"error": str(exc)}

game_mode_cdo.set_editor_property("default_pawn_class", tattoo_character_class)
report["copied_game_mode_contract"] = copied_game_mode_contract
report["generated_game_mode_class"] = path_name(game_mode_class)
report["configured_default_pawn"] = path_name(
    game_mode_cdo.get_editor_property("default_pawn_class")
)
report["configured_player_controller"] = path_name(
    game_mode_cdo.get_editor_property("player_controller_class")
)
if not report["configured_player_controller"]:
    raise RuntimeError("BP_TSGameMode PlayerControllerClass is still null")
if not unreal.EditorAssetLibrary.save_loaded_asset(game_mode_bp, only_if_is_dirty=False):
    raise RuntimeError("Failed to save BP_TSGameMode")

world = unreal.EditorLoadingAndSavingUtils.load_map(HUB_MAP_PATH)
if not world:
    raise RuntimeError(f"Failed to load {HUB_MAP_PATH}")

world_settings = world.get_world_settings()
world_settings.set_editor_property("default_game_mode", game_mode_class)
report["hub_game_mode"] = path_name(
    world_settings.get_editor_property("default_game_mode")
)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
door_candidates = []
removed_doors = []
for actor in actor_subsystem.get_all_level_actors():
    actor_class_path = path_name(actor.get_class())
    if actor_class_path == DOOR_CLASS_PATH:
        actor_info = {
            "label": actor.get_actor_label(),
            "actor_path": path_name(actor),
            "class_path": actor_class_path,
            "location": str(actor.get_actor_location()),
        }
        door_candidates.append(actor_info)
        if actor_subsystem.destroy_actor(actor):
            removed_doors.append(actor_info)

report["door_candidates"] = door_candidates
report["removed_doors"] = removed_doors
report["door_already_absent"] = len(door_candidates) == 0
if len(door_candidates) > 1 or (len(door_candidates) == 1 and len(removed_doors) != 1):
    raise RuntimeError(
        "Expected zero or one DoorToLevel actor in HUB: "
        f"candidates={len(door_candidates)} removed={len(removed_doors)}"
    )

if not unreal.EditorLevelLibrary.save_current_level():
    raise RuntimeError("Failed to save HUB after GameMode/door update")

report["status"] = "PASS"
output_path = os.environ.get(
    "CODEX_TATTOOSHOP_RUNTIME_REPORT",
    os.path.join(unreal.Paths.project_saved_dir(), "TattooShopQA", "TattooShopRuntime58.json"),
)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, ensure_ascii=False)

unreal.log(f"TATTOOSHOP_RUNTIME_CONFIG={output_path} STATUS={report['status']}")
