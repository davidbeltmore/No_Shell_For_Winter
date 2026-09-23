"""Open the existing deposit's ProceduralChest Details without saving assets.

For the project's existing CODEX_MIGRATION_PIE_SCRIPT startup hook only; this
script opens authoring UI and never requests PIE or quits the user's editor.
"""
import time
import unreal

state = {"due": time.monotonic() + 3, "busy": False}

def show_details(delta):
    if state["busy"] or time.monotonic() < state["due"]:
        return
    state["busy"] = True
    unreal.unregister_slate_post_tick_callback(state["callback"])
    bp = unreal.load_asset("/Game/_Game/Items/Chests/BP_EquipmentDeposit")
    if not bp or not unreal.ProjectLockpickingEditorLibrary.show_procedural_chest_details(bp):
        unreal.log_warning("Could not select ProceduralChest. Open the deposit Blueprint and select the component manually.")

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
state["callback"] = unreal.register_slate_post_tick_callback(show_details)
