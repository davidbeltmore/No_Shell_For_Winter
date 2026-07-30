"""Run the established Intimacy visual sequence against a spawned Male enemy."""

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
    "CODEX_MALE_INTIMACY_BASE_SCRIPT",
    r"D:\Projects UE5\LustAsDeadlySin\Tools\Intimacy\test_project_intimacy_visual_runtime.py",
)

runpy.run_path(SOURCE_VISUAL_SCRIPT, run_name="__codex_male_intimacy_base__")
