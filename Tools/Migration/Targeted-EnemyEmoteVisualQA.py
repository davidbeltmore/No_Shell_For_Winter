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
    "/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C",
)
TARGET_CHARACTER_LABEL = os.environ.get("CODEX_TARGETED_EMOTE_CHARACTER_LABEL", "Male enemy")


namespace = runpy.run_path(SOURCE_VISUAL_SCRIPT, run_name="__codex_targeted_enemy_emote_base__")
original_execute_action = namespace["execute_action"]
selection_applied = False


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
