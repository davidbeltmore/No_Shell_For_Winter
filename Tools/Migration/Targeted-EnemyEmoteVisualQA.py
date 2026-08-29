"""Run the established Y-menu visual sequence after reproducing the T-selected enemy contract."""

import builtins
import os
import runpy
import time

import unreal


SOURCE_VISUAL_SCRIPT = os.environ.get(
    "CODEX_TARGETED_EMOTE_BASE_SCRIPT",
    r"D:\Projects UE5\LustAsDeadlySin\Tools\EmoteMenu\test_project_emote_visual_runtime.py",
)
TARGET_CHARACTER_CLASS = os.environ.get(
    "CODEX_TARGETED_EMOTE_CHARACTER_CLASS",
    "/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C",
)
TARGET_CHARACTER_LABEL = os.environ.get("CODEX_TARGETED_EMOTE_CHARACTER_LABEL", "Male companion")
CAPTURE_SESSION_HUD = os.environ.get("CODEX_TARGETED_EMOTE_CAPTURE_SESSION_HUD", "0") == "1"


# Avoid an Auto Reimport popup contaminating the OS-level captures. This is a
# process-local editor preference only; it is intentionally never saved.
try:
    loading_settings = unreal.get_default_object(unreal.EditorLoadingSavingSettings)
    loading_settings.set_editor_property("monitor_content_directories", False)
except Exception:
    pass


namespace = runpy.run_path(SOURCE_VISUAL_SCRIPT, run_name="__codex_targeted_enemy_emote_base__")
original_execute_action = namespace["execute_action"]
is_lads_menu_visible = namespace["is_lads_menu_visible"]
selection_applied = False


# The source visual harness normally captures Actions > Basic. Reuse its
# synchronization/capture protocol while driving the product route requested
# here: Y > Actions > Partner > Intimacy.
visual_scenario = [
    {
        "action": "toggle_menu",
        "marker": "Root",
        "condition": lambda state: is_lads_menu_visible(state) and state["selected_index"] == 0,
    },
    {
        "action": "confirm",
        "marker": None,
        "condition": lambda state: is_lads_menu_visible(state) and state["selected_index"] == 0,
    },
    {
        "action": "navigate_down",
        "marker": None,
        "condition": lambda state: is_lads_menu_visible(state) and state["selected_index"] == 1,
    },
    {
        "action": "navigate_down",
        "marker": None,
        "condition": lambda state: is_lads_menu_visible(state) and state["selected_index"] == 2,
    },
    {
        "action": "navigate_down",
        "marker": "ActionsCategory",
        "condition": lambda state: is_lads_menu_visible(state) and state["selected_index"] == 3,
    },
    {
        "action": "confirm",
        "marker": None if CAPTURE_SESSION_HUD else "AnimationList",
        "condition": lambda state: is_lads_menu_visible(state) and state["selected_index"] == 0,
    },
]
if CAPTURE_SESSION_HUD:
    visual_scenario.append(
        {
            "action": "confirm",
            "marker": "AnimationList",
            "condition": lambda state: not state["menu_open"] and state["emote_active"],
        }
    )
original_execute_action.__globals__["VISUAL_SCENARIO"] = visual_scenario


def defer_toggle_menu(subsystem, delay_seconds=2.0):
    started_at = time.monotonic()
    callback_handle = None

    def deferred_tick(_delta_time):
        nonlocal callback_handle
        if time.monotonic() - started_at < delay_seconds:
            return
        unreal.unregister_slate_post_tick_callback(callback_handle)
        builtins._codex_targeted_enemy_deferred_toggle = None
        original_execute_action(subsystem, "toggle_menu")

    callback_handle = unreal.register_slate_post_tick_callback(deferred_tick)
    builtins._codex_targeted_enemy_deferred_toggle = callback_handle


def select_enemy_target():
    world = unreal.EditorLevelLibrary.get_game_world()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    enemy_class = unreal.load_class(
        None,
        TARGET_CHARACTER_CLASS,
    )
    enemies = unreal.GameplayStatics.get_all_actors_of_class(world, enemy_class) if world and enemy_class else []
    enemy = enemies[0] if enemies else None
    if player and world and enemy_class and not enemy:
        enemy = unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(
            world,
            TARGET_CHARACTER_CLASS,
            player.get_actor_location() + unreal.Vector(250.0, 0.0, 0.0),
            player.get_actor_rotation(),
        )
        if enemy:
            unreal.log(
                "[CodexProjectEmoteVisual] targeted_enemy_spawned=True label="
                + TARGET_CHARACTER_LABEL
                + " enemy="
                + enemy.get_name()
            )
    if not player or not enemy:
        raise RuntimeError(
            "Targeted Y-menu QA could not resolve the player and " + TARGET_CHARACTER_LABEL + "."
        )

    doctrine = None
    for owner in (player, unreal.GameplayStatics.get_player_controller(world, 0)):
        if not owner:
            continue
        for component in owner.get_components_by_class(unreal.ActorComponent):
            if "ProjectInnerDoctrineComponent" in component.get_class().get_name():
                doctrine = component
                break
        if doctrine:
            break
    if not doctrine:
        raise RuntimeError("Targeted Y-menu QA could not resolve ProjectInnerDoctrineComponent.")
    charisma = unreal.ProjectDoctrineAttribute.CHARISMA
    charisma_before = int(doctrine.get_attribute_level(charisma))
    if charisma_before != 10 and not doctrine.apply_free_attribute_levels(charisma, 10 - charisma_before):
        raise RuntimeError("Targeted Y-menu QA could not set transient Charisma to level 10.")
    charisma_after = int(doctrine.get_attribute_level(charisma))
    if charisma_after != 10:
        raise RuntimeError(f"Targeted Y-menu QA Charisma mismatch: {charisma_after}.")

    targeting_component = None
    for component in player.get_components_by_class(unreal.ActorComponent):
        if "ProjectTargetingFixComponent" in component.get_class().get_name():
            targeting_component = component
            break
    if not targeting_component:
        raise RuntimeError("Targeted Y-menu QA could not resolve ProjectTargetingFixComponent.")
    if not targeting_component.debug_set_current_target_actor(enemy):
        raise RuntimeError("Targeted Y-menu QA could not reproduce T-selected enemy state.")

    unreal.log(
        "[CodexProjectEmoteVisual] targeted_enemy_selected=True label="
        + TARGET_CHARACTER_LABEL
        + " enemy="
        + enemy.get_name()
        + " charisma_before="
        + str(charisma_before)
        + " charisma_after="
        + str(charisma_after)
    )


def targeted_execute_action(subsystem, action_name):
    global selection_applied
    if action_name == "toggle_menu" and not selection_applied:
        select_enemy_target()
        selection_applied = True
        defer_toggle_menu(subsystem)
        return True
    return original_execute_action(subsystem, action_name)


# runpy may return a shallow namespace copy; patch the live globals retained by
# the registered tick callback rather than only the returned dictionary.
original_execute_action.__globals__["execute_action"] = targeted_execute_action
