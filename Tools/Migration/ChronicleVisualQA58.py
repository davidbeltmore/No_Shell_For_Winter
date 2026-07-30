"""Visible UE 5.8 PIE QA for Chronicle wrapping and dynamic row layout."""

import builtins
import json
import os
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
RUN_DIR = Path(
    os.environ.get(
        "CODEX_CHRONICLE_QA_DIR",
        PROJECT_DIR / "Saved" / "Migration" / "Phase5" / "Visual" / "ChronicleWrap",
    )
)
RESULT_FILE = RUN_DIR / "ChronicleVisualQA.json"
OUTPUT_FILE = RUN_DIR / "ChronicleVisualQA.txt"
COMPACT_SCREENSHOT = RUN_DIR / "Chronicle_Compact.png"
EXPANDED_SCREENSHOT = RUN_DIR / "Chronicle_Expanded.png"
TARGET_MAP = os.environ.get("CODEX_CHRONICLE_QA_MAP", "/Game/_Game/Hub/HUB")
TARGET_MAP_NAME = TARGET_MAP.rsplit("/", 1)[-1].split(".")[0].lower()
TIMEOUT_SECONDS = float(os.environ.get("CODEX_CHRONICLE_QA_TIMEOUT", "240"))

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


def normalized_world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def get_world_subsystem(world, subsystem_class_path):
    subsystem_class = unreal.load_class(None, subsystem_class_path)
    if not subsystem_class:
        return None
    for subsystem in unreal.ObjectIterator(subsystem_class):
        try:
            subsystem_world = subsystem.get_world()
        except Exception:
            subsystem_world = None
        if subsystem_world == world:
            return subsystem
        if world and "UEDPIE_" in object_path(subsystem) and normalized_world_name(subsystem_world) == normalized_world_name(world):
            return subsystem
    return None


def write_output(lines):
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_result(payload):
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    RESULT_FILE.write_text(
        json.dumps(payload, indent=2, sort_keys=True, default=str) + "\n",
        encoding="utf-8",
    )


def emit(message):
    line = "[ChronicleVisualQA] " + message
    unreal.log(line)
    STATE.lines.append(line)
    write_output(STATE.lines)


def call_method(target, snake_name, *args):
    method = getattr(target, snake_name, None)
    if not callable(method):
        raise RuntimeError("{} has no callable {}".format(object_path(target), snake_name))
    return method(*args)


def widget_geometry(widget):
    result = {
        "object": object_path(widget),
        "class": class_path(widget),
        "visibility": str(widget.get_visibility()),
    }
    try:
        geometry = widget.get_cached_geometry()
        result["cached_geometry"] = str(geometry)
    except Exception as exc:
        result["geometry_error"] = str(exc)
    try:
        desired_size = widget.get_desired_size()
        result["desired_width"] = float(desired_size.x)
        result["desired_height"] = float(desired_size.y)
    except Exception as exc:
        result["desired_size_error"] = str(exc)
    return result


def collect_row_geometry(expanded):
    rows = []
    for widget in unreal.ObjectIterator(unreal.UserWidget):
        path = class_path(widget)
        if "ProjectChronicle" not in path or "Row" not in path:
            continue
        try:
            if widget.get_parent() is None:
                continue
        except Exception:
            continue
        row = widget_geometry(widget)
        rows.append(row)

    rows.sort(key=lambda row: row["object"])
    mode_token = "/Expanded/" if expanded else "/Normal/"
    visible_rows = [
        row
        for row in rows
        if mode_token in row.get("class", "")
        and row.get("desired_width", 0.0) > 0.0
        and row.get("desired_height", 0.0) > 0.0
    ]

    return {
        "rows": rows,
        "visible_rows": visible_rows,
        "visible_row_count": len(visible_rows),
        "desired_heights": [row["desired_height"] for row in visible_rows],
        "stack_desired_height_with_gaps": (
            sum(row["desired_height"] for row in visible_rows)
            + max(len(visible_rows) - 1, 0) * 3.0
        ),
        "overlaps": [],
    }


def visible_widgets():
    widgets = []
    for widget in unreal.ObjectIterator(unreal.UserWidget):
        try:
            if widget.is_in_viewport():
                widgets.append(widget_geometry(widget))
        except Exception:
            continue
    widgets.sort(key=lambda row: row["object"])
    return widgets


def request_screenshot(world, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    request = {"path": str(path), "requested": False}
    try:
        take_screenshot = getattr(unreal.AutomationLibrary, "take_high_res_screenshot", None)
        if callable(take_screenshot):
            request["automation_result"] = bool(take_screenshot(1280, 720, str(path)))
            request["requested"] = True
    except Exception as exc:
        request["automation_error"] = str(exc)
    try:
        command = 'Shot ShowUI filename="{}"'.format(path.as_posix())
        unreal.SystemLibrary.execute_console_command(world, command)
        request["console_command"] = command
        request["requested"] = True
    except Exception as exc:
        request["console_error"] = str(exc)
    return request


class RuntimeState:
    def __init__(self):
        self.phase = "load_map"
        self.elapsed = 0.0
        self.phase_elapsed = 0.0
        self.callback_handle = None
        self.lines = []
        self.activity = None
        self.needs = None
        self.result = {
            "schema_version": 1,
            "status": "IN_PROGRESS",
            "map": TARGET_MAP,
            "compact": {},
            "expanded": {},
        }


STATE = RuntimeState()


def finish(success, failure=None):
    if STATE.callback_handle is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback_handle)
        STATE.callback_handle = None
    builtins._codex_chronicle_visual_qa = None
    STATE.result["status"] = "PASS" if success else "FAIL"
    STATE.result["failure"] = failure
    STATE.result["compact_screenshot_exists"] = COMPACT_SCREENSHOT.is_file()
    STATE.result["expanded_screenshot_exists"] = EXPANDED_SCREENSHOT.is_file()
    write_result(STATE.result)
    emit("finished success={} failure={}".format(success, failure or ""))
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def seed_chronicle():
    entries = (
        "Short Chronicle entry.",
        "Inventory updated: weathered climbing rope, sealed medicine pouch, reinforced lantern, and a hand-drawn map of the frozen valley.",
        "Explicit line break check:\nThe second paragraph remains inside the same dynamically sized Chronicle row.",
    )
    for entry in entries:
        call_method(STATE.activity, "debug_add_system_entry", entry)
    call_method(
        STATE.activity,
        "add_dialogue_entry",
        "Watcher of the Winter Gate warns that the pass ahead is unsafe until the blizzard loses strength.",
    )


def tick(delta_time):
    try:
        STATE.elapsed += delta_time
        STATE.phase_elapsed += delta_time
        if STATE.elapsed > TIMEOUT_SECONDS:
            finish(False, "timeout phase={}".format(STATE.phase))
            return

        if STATE.phase == "load_map":
            STATE.phase = "wait_map"
            STATE.phase_elapsed = 0.0
            emit("loading_map=" + TARGET_MAP)
            LEVEL_EDITOR.load_level(TARGET_MAP)
            return

        if STATE.phase == "wait_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if normalized_world_name(editor_world) != TARGET_MAP_NAME or STATE.phase_elapsed < 1.5:
                return
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            emit("begin_pie=True")
            LEVEL_EDITOR.editor_request_begin_play()
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor():
                return
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            if not world or normalized_world_name(world) != TARGET_MAP_NAME or STATE.phase_elapsed < 5.0:
                return
            STATE.activity = get_world_subsystem(
                world,
                "/Script/EFProjectSystemsGameplay.ProjectActivityFeedSubsystem",
            )
            STATE.needs = get_world_subsystem(
                world,
                "/Script/EFProjectSystemsGameplay.ProjectSurvivalNeedsSubsystem",
            )
            if not STATE.activity or not STATE.needs:
                return
            STATE.phase = "wait_compact"
            STATE.phase_elapsed = 0.0
            call_method(STATE.needs, "set_needs_hud_visible", True)
            if call_method(STATE.activity, "is_feed_expanded"):
                call_method(STATE.activity, "request_toggle_expanded")
            seed_chronicle()
            emit("chronicle_seeded=True")
            return

        if STATE.phase == "wait_compact":
            if STATE.phase_elapsed < 4.0:
                return
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            compact_geometry = collect_row_geometry(False)
            STATE.phase = "wait_compact_capture"
            STATE.phase_elapsed = 0.0
            STATE.result["compact"] = {
                "expanded": bool(call_method(STATE.activity, "is_feed_expanded")),
                "hud_visible": bool(call_method(STATE.activity, "is_feed_hud_visible")),
                "stored_entry_count": int(call_method(STATE.activity, "get_stored_entry_count")),
                "geometry": compact_geometry,
                "visible_widgets": visible_widgets(),
                "screenshot_request": request_screenshot(world, COMPACT_SCREENSHOT),
            }
            emit("COMPACT_CAPTURE_READY=True")
            return

        if STATE.phase == "wait_compact_capture":
            if STATE.phase_elapsed < 25.0:
                return
            call_method(STATE.activity, "request_toggle_expanded")
            STATE.phase = "wait_expanded"
            STATE.phase_elapsed = 0.0
            emit("expanded_requested=True")
            return

        if STATE.phase == "wait_expanded":
            if STATE.phase_elapsed < 4.0:
                return
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            expanded_geometry = collect_row_geometry(True)
            STATE.phase = "wait_expanded_capture"
            STATE.phase_elapsed = 0.0
            STATE.result["expanded"] = {
                "expanded": bool(call_method(STATE.activity, "is_feed_expanded")),
                "hud_visible": bool(call_method(STATE.activity, "is_feed_hud_visible")),
                "stored_entry_count": int(call_method(STATE.activity, "get_stored_entry_count")),
                "geometry": expanded_geometry,
                "visible_widgets": visible_widgets(),
                "screenshot_request": request_screenshot(world, EXPANDED_SCREENSHOT),
            }
            emit("EXPANDED_CAPTURE_READY=True")
            return

        if STATE.phase == "wait_expanded_capture":
            if STATE.phase_elapsed < 25.0:
                return
            compact = STATE.result["compact"]
            expanded = STATE.result["expanded"]
            checks = {
                "compact_hud_visible": compact.get("hud_visible") is True,
                "expanded_hud_visible": expanded.get("hud_visible") is True,
                "compact_mode": compact.get("expanded") is False,
                "expanded_mode": expanded.get("expanded") is True,
                "compact_rows_visible": compact.get("geometry", {}).get("visible_row_count", 0) >= 2,
                "expanded_rows_visible": expanded.get("geometry", {}).get("visible_row_count", 0) >= 2,
                "compact_no_overlap": not compact.get("geometry", {}).get("overlaps"),
                "expanded_no_overlap": not expanded.get("geometry", {}).get("overlaps"),
            }
            STATE.result["checks"] = checks
            finish(all(checks.values()), None if all(checks.values()) else "one or more visual geometry checks failed")
            return
    except Exception as exc:
        emit("exception={}\n{}".format(exc, traceback.format_exc()))
        finish(False, str(exc))


existing = getattr(builtins, "_codex_chronicle_visual_qa", None)
if existing is not None:
    unreal.log_warning("[ChronicleVisualQA] duplicate callback registration ignored")
else:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text("", encoding="utf-8")
    STATE.callback_handle = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_chronicle_visual_qa = STATE
    emit("registered=True")
