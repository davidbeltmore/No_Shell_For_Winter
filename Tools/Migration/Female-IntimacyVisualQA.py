"""Run the established Intimacy visual sequence against a spawned Female enemy."""

import os
import runpy

import unreal


def disable_auto_reimport_for_capture():
    settings_class = getattr(unreal, "EditorLoadingSavingSettings", None)
    if settings_class:
        settings = settings_class.get_default_object()
        settings.set_editor_property("monitor_content_directories", False)
        unreal.log("[CodexIntimacyVisual] auto_reimport_monitoring=false")


disable_auto_reimport_for_capture()


SOURCE_VISUAL_SCRIPT = os.environ.get(
    "CODEX_FEMALE_INTIMACY_BASE_SCRIPT",
    r"D:\Projects UE5\LustAsDeadlySin\Tools\Intimacy\test_project_intimacy_visual_runtime.py",
)
FEMALE_CLASS_PATH = (
    "/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale."
    "ACFRangedEnemyBPFemale_C"
)


namespace = runpy.run_path(SOURCE_VISUAL_SCRIPT, run_name="__codex_female_intimacy_base__")
original_find_candidate_target = namespace["find_candidate_target"]


def find_female_candidate_target(world, player_pawn):
    female_class = unreal.load_class(None, FEMALE_CLASS_PATH)
    actors = (
        unreal.GameplayStatics.get_all_actors_of_class(world, female_class)
        if world and female_class
        else []
    )
    target = actors[0] if actors else None
    if not target and world and player_pawn and female_class:
        target = unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(
            world,
            FEMALE_CLASS_PATH,
            player_pawn.get_actor_location() + unreal.Vector(250.0, 0.0, 0.0),
            player_pawn.get_actor_rotation(),
        )
    if target:
        unreal.log("[CodexIntimacyVisual] female_target_ready=" + target.get_name())
        return target
    raise RuntimeError("Female Intimacy QA could not spawn or resolve ACFRangedEnemyBPFemale.")


# The registered source callback retains this globals dictionary.
original_find_candidate_target.__globals__["find_candidate_target"] = find_female_candidate_target
