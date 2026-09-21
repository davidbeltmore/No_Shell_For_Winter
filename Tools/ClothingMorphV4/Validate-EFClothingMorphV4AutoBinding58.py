"""Non-destructive editor integration probe for V4 automatic binding refresh.

Run with ``-ExecutePythonScript`` (never as a commandlet) so the editor module's
real property-change delegate and ticker execute. The probe changes one copied
Clothing Name and restores it in the same frame, waits for the debounced check,
verifies that the registry is unchanged, writes evidence under Saved, and exits
without saving the Director.
"""

import json
import os
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal


DIRECTOR_PATH = "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector"
REGISTRY_PATH = "/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry"
RECEIPT_PATH = Path(
    os.environ.get(
        "EF_CLOTHING_V4_AUTO_BINDING_RECEIPT",
        str(
            Path(unreal.Paths.project_saved_dir())
            / "ClothingMorphV4QA"
            / "auto_binding_probe.json"
        ),
    )
)


class ProbeState:
    callback = None
    elapsed = 0.0
    phase = "WAIT_FOR_EDITOR"
    director = None
    registry = None
    clothes = []
    binding_paths_before = []
    error = ""


STATE = ProbeState()


def prop(obj, name):
    return obj.get_editor_property(name)


def binding_paths(registry):
    return sorted(
        binding.get_path_name()
        for binding in list(prop(registry, "native_source_bindings"))
        if binding
    )


def finish(success, details):
    payload = {
        "status": (
            "UE58_EF_CLOTHING_MORPH_V4_AUTO_BINDING_PASS"
            if success
            else "UE58_EF_CLOTHING_MORPH_V4_AUTO_BINDING_FAIL"
        ),
        "success": bool(success),
        "timestamp_utc": datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"),
        "director": DIRECTOR_PATH,
        "registry": REGISTRY_PATH,
        "enabled_clothing_count": sum(
            1 for row in STATE.clothes if bool(prop(row, "enabled"))
        ),
        "binding_paths_before": STATE.binding_paths_before,
        "binding_paths_after": (
            binding_paths(STATE.registry) if STATE.registry else []
        ),
        "property_change_dispatched": STATE.phase != "WAIT_FOR_EDITOR",
        "details": details,
        "error": STATE.error,
    }
    RECEIPT_PATH.parent.mkdir(parents=True, exist_ok=True)
    RECEIPT_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    unreal.log("EF_CLOTHING_V4_AUTO_BINDING_PROBE=" + json.dumps(payload))
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def tick(delta_seconds):
    try:
        STATE.elapsed += float(delta_seconds)
        if STATE.phase == "WAIT_FOR_EDITOR" and STATE.elapsed >= 0.5:
            STATE.director = unreal.load_asset(DIRECTOR_PATH)
            STATE.registry = unreal.load_asset(REGISTRY_PATH)
            if not STATE.director or not STATE.registry:
                raise RuntimeError("Director or V4 registry could not be loaded.")
            STATE.clothes = list(prop(STATE.director, "garments"))
            STATE.binding_paths_before = binding_paths(STATE.registry)
            if not STATE.clothes or not STATE.binding_paths_before:
                raise RuntimeError("The configured V4 catalog has no clothes or bindings.")

            # Force the same reflected nested-array event used by Details edits,
            # then restore the original identity before the 0.75 s debounce can
            # inspect or compile anything. Nothing is saved by this probe.
            probe_row = STATE.clothes[0]
            original_name = str(prop(probe_row, "garment_id"))
            probe_row.set_editor_property(
                "garment_id", original_name + "__AutoBindingProbe"
            )
            STATE.director.set_editor_property("garments", STATE.clothes)
            probe_row.set_editor_property("garment_id", original_name)
            STATE.director.set_editor_property("garments", STATE.clothes)
            STATE.phase = "WAIT_FOR_AUTO_REFRESH"
            STATE.elapsed = 0.0
            unreal.log("EF_CLOTHING_V4_AUTO_BINDING_PROPERTY_CHANGE_DISPATCHED")
            return

        if STATE.phase == "WAIT_FOR_AUTO_REFRESH" and STATE.elapsed >= 4.0:
            after = binding_paths(STATE.registry)
            enabled_count = sum(
                1 for row in STATE.clothes if bool(prop(row, "enabled"))
            )
            success = (
                after == STATE.binding_paths_before
                and len(after) == enabled_count
                and enabled_count > 0
            )
            finish(
                success,
                "The real editor property event completed its debounced automatic "
                "binding check without replacing any valid clothing binding.",
            )
    except Exception:
        STATE.error = traceback.format_exc()
        unreal.log_error(STATE.error)
        finish(False, "Automatic binding editor probe raised an exception.")


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
