"""Log the UE 5.8 Python screenshot surface used by migration QA."""

import unreal


for name in dir(unreal.AutomationLibrary):
    if "screenshot" not in name.lower():
        continue
    value = getattr(unreal.AutomationLibrary, name)
    unreal.log("CODEX_SCREENSHOT_API name={} doc={}".format(name, getattr(value, "__doc__", "")))

unreal.SystemLibrary.quit_editor()
