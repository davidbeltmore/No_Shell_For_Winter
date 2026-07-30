"""Read-only UE 5.8 baseline PIE probe used when marketplace MCP tools are unavailable."""

import builtins
import json
import os
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_FILE = Path(os.environ.get("CODEX_MIGRATION_PIE_OUTPUT", PROJECT_DIR / "Saved" / "Migration" / "BaselinePIE.txt"))
RESULT_FILE = Path(os.environ.get("CODEX_MIGRATION_PIE_RESULT", OUTPUT_FILE.with_suffix(".json")))
VIEWPORT_SCREENSHOT = Path(
    os.environ.get("CODEX_MIGRATION_PIE_VIEWPORT_SCREENSHOT", OUTPUT_FILE.parent / "PIE_Test_Viewport.png")
)
TARGET_MAP = os.environ.get("CODEX_MIGRATION_PIE_MAP", "/Game/FullSample/Test")
TARGET_MAP_NAME = TARGET_MAP.rsplit("/", 1)[-1].split(".")[0].lower()
TOTAL_TIMEOUT_SECONDS = float(os.environ.get("CODEX_MIGRATION_PIE_TIMEOUT", "240"))
CAPTURE_HOLD_SECONDS = float(os.environ.get("CODEX_MIGRATION_PIE_CAPTURE_HOLD", "10"))

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def object_path(value):
    if not value:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def class_path(value):
    if not value:
        return ""
    try:
        return value.get_class().get_path_name()
    except Exception:
        return ""


def get_property(value, name, default=None):
    if not value:
        return default
    try:
        return value.get_editor_property(name)
    except Exception:
        return default


def normalized_world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def write_text(lines):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit(message):
    line = f"[BaselinePIE] {message}"
    unreal.log(line)
    STATE.lines.append(line)
    write_text(STATE.lines)


def write_result(payload):
    RESULT_FILE.parent.mkdir(parents=True, exist_ok=True)
    RESULT_FILE.write_text(json.dumps(payload, indent=2, sort_keys=True, default=str), encoding="utf-8")


def probe_class(path):
    try:
        loaded = unreal.load_class(None, path)
        return {
            "requested": path,
            "resolved": object_path(loaded),
            "loaded": loaded is not None,
        }
    except Exception as exc:
        return {"requested": path, "resolved": "", "loaded": False, "error": str(exc)}


def probe_settings():
    class_name = "/Script/AscentGASRuntime.ACFGASDeveloperSettings"
    result = {"class": class_name, "loaded": False, "properties": {}}
    try:
        settings_class = unreal.load_class(None, class_name)
        result["loaded"] = settings_class is not None
        if not settings_class:
            return result
        settings = unreal.get_default_object(settings_class)
        result["cdo"] = object_path(settings)
        result["python_members"] = sorted(
            name for name in dir(settings) if "health" in name.lower() or "serializ" in name.lower()
        )
        for property_name in ("health_attribute", "serializable_attributes"):
            attempts = {}
            for candidate in (property_name, "".join(part.capitalize() for part in property_name.split("_"))):
                try:
                    attempts[candidate] = str(settings.get_editor_property(candidate))
                except Exception as exc:
                    attempts[candidate] = {"error": str(exc)}
            result["properties"][property_name] = attempts
    except Exception as exc:
        result["error"] = str(exc)
    return result


def probe_asset(path):
    result = {"requested": path, "loaded": False}
    try:
        asset = unreal.load_asset(path)
        result["loaded"] = asset is not None
        result["asset"] = object_path(asset)
        result["asset_class"] = class_path(asset)
        if not asset:
            return result
        generated_class = get_property(asset, "generated_class")
        if generated_class:
            result["generated_class"] = object_path(generated_class)
            try:
                result["generated_super_class"] = object_path(generated_class.get_super_class())
            except Exception:
                result["generated_super_class"] = ""
            try:
                result["cdo"] = object_path(unreal.get_default_object(generated_class))
            except Exception as exc:
                result["cdo_error"] = str(exc)
        row_struct = get_property(asset, "row_struct")
        if row_struct:
            result["row_struct"] = object_path(row_struct)
        try:
            result["data_table_rows"] = [
                str(name) for name in unreal.DataTableFunctionLibrary.get_data_table_row_names(asset)
            ]
        except Exception:
            pass
    except Exception as exc:
        result["error"] = str(exc)
    return result


def visible_widgets():
    widgets = []
    try:
        iterator = unreal.ObjectIterator(unreal.UserWidget)
    except Exception as exc:
        return {"widgets": widgets, "error": str(exc)}
    for widget in iterator:
        try:
            if widget.is_in_viewport():
                widgets.append({
                    "object": object_path(widget),
                    "class": class_path(widget),
                    "visibility": str(widget.get_visibility()),
                })
        except Exception:
            continue
    widgets.sort(key=lambda row: row["object"].lower())
    return {"widgets": widgets}


def runtime_snapshot():
    world = EDITOR_LEVEL_LIBRARY.get_game_world()
    player_controller = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    game_mode = unreal.GameplayStatics.get_game_mode(world) if world else None
    player_state = get_property(player_controller, "player_state")

    meshes = []
    if pawn:
        try:
            components = pawn.get_components_by_class(unreal.SkeletalMeshComponent)
        except Exception:
            components = []
        for component in components or []:
            try:
                mesh_asset = component.get_skeletal_mesh_asset()
            except Exception:
                mesh_asset = get_property(component, "skeletal_mesh")
            try:
                anim_instance = component.get_anim_instance()
            except Exception:
                anim_instance = None
            try:
                attach_parent = component.get_attach_parent()
            except Exception:
                attach_parent = None
            try:
                visible = bool(component.is_visible())
            except Exception:
                visible = None
            meshes.append({
                "component": component.get_name(),
                "component_class": class_path(component),
                "mesh_asset": object_path(mesh_asset),
                "visible": visible,
                "hidden_in_game": get_property(component, "hidden_in_game"),
                "leader_pose_component": object_path(get_property(component, "leader_pose_component")),
                "attach_parent": object_path(attach_parent),
                "anim_instance_class": class_path(anim_instance),
            })
    meshes.sort(key=lambda row: row["component"].lower())

    try:
        time_seconds = float(unreal.GameplayStatics.get_time_seconds(world)) if world else 0.0
    except Exception:
        time_seconds = 0.0

    return {
        "world": object_path(world),
        "world_name": normalized_world_name(world),
        "time_seconds": time_seconds,
        "game_mode_object": object_path(game_mode),
        "game_mode_class": class_path(game_mode),
        "player_controller_object": object_path(player_controller),
        "player_controller_class": class_path(player_controller),
        "pawn_object": object_path(pawn),
        "pawn_class": class_path(pawn),
        "player_state_object": object_path(player_state),
        "player_state_class": class_path(player_state),
        "skeletal_meshes": meshes,
        "visible_widgets": visible_widgets(),
    }


def snapshot_ready(snapshot):
    return bool(
        snapshot["world_name"] == TARGET_MAP_NAME
        and snapshot["time_seconds"] >= 3.0
        and snapshot["game_mode_class"]
        and snapshot["player_controller_class"]
        and snapshot["pawn_class"]
    )


def request_viewport_screenshot(world):
    VIEWPORT_SCREENSHOT.parent.mkdir(parents=True, exist_ok=True)
    result = {"requested": False, "path": str(VIEWPORT_SCREENSHOT)}
    try:
        take_screenshot = getattr(unreal.AutomationLibrary, "take_high_res_screenshot", None)
        if callable(take_screenshot):
            result["automation_result"] = bool(take_screenshot(1280, 720, str(VIEWPORT_SCREENSHOT)))
            result["requested"] = True
    except Exception as exc:
        result["automation_error"] = str(exc)
    try:
        command = f'Shot ShowUI filename="{VIEWPORT_SCREENSHOT.as_posix()}"'
        unreal.SystemLibrary.execute_console_command(world, command)
        result["console_command"] = command
        result["requested"] = True
    except Exception as exc:
        result["console_error"] = str(exc)
    return result


class RuntimeState:
    def __init__(self):
        self.phase = "load_map"
        self.elapsed = 0.0
        self.phase_elapsed = 0.0
        self.stable_samples = 0
        self.callback_handle = None
        self.lines = []
        self.last_snapshot = None
        self.result = None


STATE = RuntimeState()


def finish(success, failure=None):
    if STATE.callback_handle is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback_handle)
        STATE.callback_handle = None
    if getattr(builtins, "_codex_migration_baseline_pie", None) is not None:
        builtins._codex_migration_baseline_pie = None
    if STATE.result is None:
        STATE.result = {"status": "PASS" if success else "FAIL"}
    STATE.result["status"] = "PASS" if success else "FAIL"
    STATE.result["failure"] = failure
    STATE.result["stable_samples"] = STATE.stable_samples
    write_result(STATE.result)
    emit(f"finished success={success} failure={failure or ''}")
    unreal.SystemLibrary.quit_editor()


def tick(delta_time):
    try:
        STATE.elapsed += delta_time
        STATE.phase_elapsed += delta_time
        if STATE.elapsed > TOTAL_TIMEOUT_SECONDS:
            finish(False, f"timeout phase={STATE.phase}")
            return

        if STATE.phase == "load_map":
            emit(f"loading_map={TARGET_MAP}")
            STATE.phase = "wait_map"
            STATE.phase_elapsed = 0.0
            LEVEL_EDITOR.load_level(TARGET_MAP)
            return

        if STATE.phase == "wait_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if normalized_world_name(editor_world) != TARGET_MAP_NAME or STATE.phase_elapsed < 1.5:
                return
            emit(f"map_loaded={object_path(editor_world)}")
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            LEVEL_EDITOR.editor_request_begin_play()
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor():
                return
            snapshot = runtime_snapshot()
            STATE.last_snapshot = snapshot
            if snapshot_ready(snapshot):
                STATE.stable_samples += 1
            else:
                STATE.stable_samples = 0
            if STATE.stable_samples < 3:
                return

            # Screenshot helpers may pump Slate and re-enter this callback. Change phase first.
            STATE.phase = "capture_in_progress"
            STATE.phase_elapsed = 0.0
            class_probes = [
                probe_class("/Script/AscentGASIntegration.ACFAttributeSet"),
                probe_class("/Script/AscentGASRuntime.ACFAttributeSet"),
                probe_class("/Script/AscentGASIntegration.ACFStatisticsSet"),
                probe_class("/Script/AscentGASRuntime.ACFStatisticsSet"),
                probe_class("/Script/AscentLevelingSystem.ARSLevelingComponent"),
                probe_class("/Script/AscentGASRuntime.ARSLevelingComponent"),
            ]
            screenshot = request_viewport_screenshot(EDITOR_LEVEL_LIBRARY.get_game_world())
            STATE.result = {
                "status": "IN_PROGRESS",
                "map": TARGET_MAP,
                "runtime": snapshot,
                "acf_class_probes": class_probes,
                "acf_gas_settings": probe_settings(),
                "acf_asset_probes": [
                    probe_asset("/AscentCombatFramework/Blueprints/DamageTypes/ACFImpactDamageType"),
                    probe_asset("/AscentCombatFramework/Integrations/QuestActions/ACF_RewardAction_BP"),
                    probe_asset("/AscentCombatFramework/Integrations/QuestActions/ACF_AutoLevelUp_BP"),
                    probe_asset("/AscentCombatFramework/GASRuntime/ACF_SerializabeAttributes_DT"),
                ],
                "viewport_screenshot_request": screenshot,
            }
            write_result(STATE.result)
            emit(f"PIE_READY={json.dumps(snapshot, sort_keys=True, default=str)}")
            emit("SCREENSHOT_WINDOW_READY=True")
            STATE.phase = "hold_capture"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "capture_in_progress":
            return

        if STATE.phase == "hold_capture":
            if STATE.phase_elapsed < CAPTURE_HOLD_SECONDS:
                return
            STATE.phase = "wait_end_pie"
            STATE.phase_elapsed = 0.0
            emit("ending_pie=True")
            EDITOR_LEVEL_LIBRARY.editor_end_play()
            return

        if STATE.phase == "wait_end_pie":
            if LEVEL_EDITOR.is_in_play_in_editor():
                return
            finish(True)
            return
    except Exception as exc:
        details = f"{exc}\n{traceback.format_exc()}"
        emit(f"exception={details}")
        try:
            if LEVEL_EDITOR.is_in_play_in_editor():
                EDITOR_LEVEL_LIBRARY.editor_end_play()
        except Exception:
            pass
        finish(False, str(exc))


existing = getattr(builtins, "_codex_migration_baseline_pie", None)
if existing is not None:
    unreal.log_warning("[BaselinePIE] duplicate callback registration ignored")
else:
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text("", encoding="utf-8")
    STATE.callback_handle = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_migration_baseline_pie = STATE
    emit("registered=True")
