"""Environment-gated project startup hooks for deterministic migration automation."""

import os
import runpy


if os.environ.get("CODEX_RUN_MIGRATION_BASELINE_PIE") == "1":
    script_path = os.environ.get("CODEX_MIGRATION_PIE_SCRIPT", "")
    if not script_path:
        raise RuntimeError("CODEX_MIGRATION_PIE_SCRIPT is required when baseline PIE automation is enabled")
    runpy.run_path(script_path, run_name="__codex_migration_baseline_pie__")


if os.environ.get("CODEX_RUN_PROJECT_EMOTE_VISUAL_TEST") == "1":
    os.environ["CODEX_RUN_PROJECT_EMOTE_VISUAL_TEST"] = "0"
    script_path = os.environ.get("CODEX_PROJECT_EMOTE_VISUAL_SCRIPT", "")
    if not script_path:
        script_path = os.path.join(
            os.path.dirname(os.path.dirname(os.path.dirname(__file__))),
            "Tools",
            "EmoteMenu",
            "test_project_emote_visual_runtime.py",
        )
    if not os.path.isfile(script_path):
        raise RuntimeError("Project emote visual script is missing: " + script_path)
    runpy.run_path(script_path, run_name="__codex_project_emote_visual__")


if os.environ.get("CODEX_RUN_FOODKIT_ALCOHOL_PIE") == "1":
    os.environ["CODEX_RUN_FOODKIT_ALCOHOL_PIE"] = "0"
    script_path = os.environ.get("CODEX_FOODKIT_ALCOHOL_PIE_SCRIPT", "")
    if not script_path or not os.path.isfile(script_path):
        raise RuntimeError("FoodKit Alcohol PIE script is missing: " + script_path)
    runpy.run_path(script_path, run_name="__codex_foodkit_alcohol_pie__")

