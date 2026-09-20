"""Real PIE coverage for centered metadata and automatic Chronicle row heights."""
import os
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
output = root / "Saved/Migration/ChronicleAdaptive"
output.mkdir(parents=True, exist_ok=True)
os.environ["CODEX_CHRONICLE_WBP_REPORT"] = str(output / "BlueprintCompile.json")
os.environ["CODEX_CHRONICLE_QA_DIR"] = str(output)
os.environ["CODEX_CHRONICLE_QA_CAPTURE_HOLD"] = "3"
runpy.run_path(str(root / "Tools/Migration/Validate-ChronicleWidgets58.py"))
fixture = runpy.run_path(str(root / "Tools/Migration/ChronicleVisualQA58.py"))
fixture_globals = fixture["tick"].__globals__


def seed_adaptive_rows():
    activity = fixture_globals["STATE"].activity
    world = unreal.EditorLevelLibrary.get_game_world()
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
    speaker = next((actor for actor in actors if "ACFDummyEnemy" in actor.get_class().get_name()), None)
    if speaker is None:
        raise RuntimeError("HUB enemy speaker was not found")
    activity.debug_add_system_entry("Quest updated: Reach the winter gate.")
    activity.debug_add_system_entry(
        "Achievement unlocked: Winter Explorer. You have discovered every shelter "
        "in the valley and can now return to the watchtower for your reward."
    )
    activity.add_dialogue_entry("The winter gate has opened.")
    entry = activity.get_latest_entry()
    if str(entry.get_editor_property("primary_text")):
        raise RuntimeError("Speakerless dialogue incorrectly fills the name column")
    activity.add_partner_dialogue_entry(
        speaker,
        "The road beyond the gate is buried in snow. Follow the lanterns along the "
        "ridge, avoid the frozen river, and wait for the rest of the group before "
        "you approach the abandoned watchtower."
    )
    activity.add_partner_dialogue_entry(speaker, "Stay together.")
    activity.debug_add_enemy_bark_entry(speaker, False)
    fixture_globals["emit"]("adaptive_rows_seeded speaker=" + speaker.get_path_name())


fixture_globals["seed_chronicle"] = seed_adaptive_rows

# These native row wrappers do not expose valid cached geometry to Python.
# Keep the legacy fixture's mode/visibility checks, but do not present its empty
# overlap lists as a measured overlap PASS. Layout is tested in native automation
# and visually inspected using editor captures.
original_finish = fixture_globals["finish"]


def finish_adaptive(success, failure=None):
    result = fixture_globals["STATE"].result
    checks = result.get("checks", {})
    checks.pop("compact_no_overlap", None)
    checks.pop("expanded_no_overlap", None)
    result["runtime_geometry_measurement"] = "PENDING: cached geometry unavailable in Python"
    original_finish(success, failure)


fixture_globals["finish"] = finish_adaptive
