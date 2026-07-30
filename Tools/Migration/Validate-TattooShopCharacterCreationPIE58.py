"""Focused PIE smoke test for TattooShop inside EF Character Creation's Tattoo tab."""

import builtins
import json
import os
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_FILE = Path(
    os.environ.get(
        "CODEX_TATTOOSHOP_CC_PIE_EVIDENCE",
        PROJECT_DIR / "Saved" / "Migration" / "TattooShopCharacterCreationPIE58.json",
    )
)
SCREENSHOT_FILE = Path(
    os.environ.get(
        "CODEX_TATTOOSHOP_CC_SCREENSHOT",
        OUTPUT_FILE.with_name(OUTPUT_FILE.stem + "_Applied.png"),
    )
)
TARGET_MAP = os.environ.get("CODEX_TATTOOSHOP_CC_MAP", "/Game/FullSample/Test")
TIMEOUT_SECONDS = float(os.environ.get("CODEX_TATTOOSHOP_CC_TIMEOUT", "120"))

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def path(value):
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


def normalize_world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def find_object_by_class_path(class_asset_path):
    target_class = unreal.load_class(None, class_asset_path)
    if not target_class:
        return None
    try:
        iterator = unreal.ObjectIterator(target_class)
    except Exception:
        iterator = unreal.ObjectIterator(unreal.Object)
    for obj in iterator:
        try:
            if obj.get_class() == target_class or obj.get_class().is_child_of(target_class):
                return obj
        except Exception:
            continue
    return None


def find_world_subsystem(world, class_asset_path):
    target_class = unreal.load_class(None, class_asset_path)
    if not world or not target_class:
        return None
    for candidate_name in ("get_subsystem", "get_world_subsystem"):
        candidate = getattr(world, candidate_name, None)
        if callable(candidate):
            try:
                subsystem = candidate(target_class)
                if subsystem:
                    return subsystem
            except Exception:
                pass
    for obj in unreal.ObjectIterator(unreal.Object):
        try:
            if obj.get_class() == target_class and path(obj).startswith(path(world)):
                return obj
        except Exception:
            continue
    return None


def find_game_instance_subsystem(world, class_asset_path):
    target_class = unreal.load_class(None, class_asset_path)
    game_instance = unreal.GameplayStatics.get_game_instance(world) if world else None
    if not game_instance or not target_class:
        return None
    for candidate_name in ("get_subsystem", "get_game_instance_subsystem"):
        candidate = getattr(game_instance, candidate_name, None)
        if callable(candidate):
            try:
                subsystem = candidate(target_class)
                if subsystem:
                    return subsystem
            except Exception:
                pass
    for obj in unreal.ObjectIterator(unreal.Object):
        try:
            if obj.get_class() == target_class and path(obj).startswith(path(game_instance)):
                return obj
        except Exception:
            continue
    return None


def call_bool(obj, snake_name, camel_name):
    for name in (snake_name, camel_name):
        fn = getattr(obj, name, None)
        if callable(fn):
            return bool(fn())
    raise RuntimeError(f"{path(obj)} has neither {snake_name} nor {camel_name}")


def visible_tattooshop_widgets():
    rows = []
    for widget in unreal.ObjectIterator(unreal.UserWidget):
        cpath = class_path(widget)
        if "WBP_TattooShop" not in cpath:
            continue
        parent = None
        try:
            parent = widget.get_parent()
        except Exception:
            pass
        try:
            in_viewport = bool(widget.is_in_viewport())
        except Exception:
            in_viewport = False
        try:
            visibility = str(widget.get_visibility())
        except Exception:
            visibility = ""
        rows.append(
            {
                "object": path(widget),
                "class": cpath,
                "visibility": visibility,
                "is_in_viewport": in_viewport,
                "parent": path(parent),
                "parent_class": class_path(parent),
            }
        )
    return rows


def write_result(payload):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(json.dumps(payload, indent=2, sort_keys=True, default=str), encoding="utf-8")


class State:
    def __init__(self):
        self.phase = "load_map"
        self.elapsed = 0.0
        self.phase_elapsed = 0.0
        self.stable = 0
        self.handle = None
        self.result = {}
        self.tattoo_subsystem = None


STATE = State()


def finish(status, failure=""):
    if STATE.handle is not None:
        unreal.unregister_slate_post_tick_callback(STATE.handle)
        STATE.handle = None
    builtins._codex_tattooshop_cc_pie = None
    STATE.result["status"] = status
    STATE.result["failure"] = failure
    STATE.result["target_map"] = TARGET_MAP
    STATE.result["screenshot"] = str(SCREENSHOT_FILE)
    STATE.result["screenshot_exists"] = SCREENSHOT_FILE.is_file()
    write_result(STATE.result)
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    except Exception:
        pass
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def tick(delta_seconds):
    try:
        STATE.elapsed += delta_seconds
        STATE.phase_elapsed += delta_seconds
        if STATE.elapsed > TIMEOUT_SECONDS:
            finish("FAIL", f"timeout in phase {STATE.phase}")
            return

        if STATE.phase == "load_map":
            LEVEL_EDITOR.load_level(TARGET_MAP)
            STATE.phase = "wait_editor_map"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_editor_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if normalize_world_name(editor_world) != TARGET_MAP.rsplit("/", 1)[-1].lower() or STATE.phase_elapsed < 1.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor():
                return
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            pc = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            try:
                time_seconds = float(unreal.GameplayStatics.get_time_seconds(world)) if world else 0.0
            except Exception:
                time_seconds = 0.0
            if world and pc and pawn and time_seconds >= 2.0:
                STATE.stable += 1
            else:
                STATE.stable = 0
            if STATE.stable < 3:
                return
            cc_subsystem = find_game_instance_subsystem(
                world, "/Script/EFCharacterCreationRuntime.EFCharacterCreationSubsystem"
            )
            if not cc_subsystem:
                finish("FAIL", "EFCharacterCreationSubsystem not found")
                return
            opened_cc = call_bool(
                cc_subsystem,
                "open_character_creation_for_automation",
                "OpenCharacterCreationForAutomation",
            )
            STATE.result["character_creation_opened"] = opened_cc
            STATE.result["character_creation_subsystem"] = path(cc_subsystem)
            STATE.result["pawn"] = path(pawn)
            STATE.result["pawn_class"] = class_path(pawn)
            STATE.phase = "wait_root"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_root":
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            cc_subsystem = find_game_instance_subsystem(
                world, "/Script/EFCharacterCreationRuntime.EFCharacterCreationSubsystem"
            )
            root = None
            if cc_subsystem:
                for name in ("get_active_root_widget_for_automation", "GetActiveRootWidgetForAutomation"):
                    fn = getattr(cc_subsystem, name, None)
                    if callable(fn):
                        try:
                            root = fn()
                        except Exception:
                            root = None
                        if root:
                            break
            if not root:
                return
            opened_tattoo = call_bool(root, "open_tattoo_tab_for_automation", "OpenTattooTabForAutomation")
            STATE.result["root_widget"] = path(root)
            STATE.result["root_widget_class"] = class_path(root)
            STATE.result["tattoo_tab_opened"] = opened_tattoo
            STATE.phase = "wait_tattooshop"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_tattooshop":
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            tattoo_subsystem = find_world_subsystem(
                world, "/Script/EFProjectSystemsGameplay.ProjectTattooShopInputSubsystem"
            )
            widgets = visible_tattooshop_widgets()
            is_open = False
            if tattoo_subsystem:
                for name in ("is_tattoo_shop_open", "IsTattooShopOpen"):
                    fn = getattr(tattoo_subsystem, name, None)
                    if callable(fn):
                        try:
                            is_open = bool(fn())
                        except Exception:
                            is_open = False
                        break
            STATE.result["tattoo_subsystem"] = path(tattoo_subsystem)
            STATE.result["tattoo_shop_open"] = is_open
            STATE.result["tattoo_shop_widgets"] = widgets
            STATE.result["tattoo_shop_widget_count"] = len(widgets)
            if is_open and widgets:
                apply_result = False
                for name in (
                    "apply_default_heart_tattoo_for_automation",
                    "ApplyDefaultHeartTattooForAutomation",
                ):
                    fn = getattr(tattoo_subsystem, name, None)
                    if callable(fn):
                        apply_result = bool(fn())
                        break

                overlay_report = {}
                for name in (
                    "get_tattoo_overlay_report_for_automation",
                    "GetTattooOverlayReportForAutomation",
                ):
                    fn = getattr(tattoo_subsystem, name, None)
                    if callable(fn):
                        raw_report = str(fn())
                        try:
                            overlay_report = json.loads(raw_report)
                        except Exception:
                            overlay_report = {"raw": raw_report}
                        break

                STATE.tattoo_subsystem = tattoo_subsystem
                STATE.result["heart_tattoo_applied"] = apply_result
                STATE.result["overlay_report"] = overlay_report
                STATE.phase = "wait_applied"
                STATE.phase_elapsed = 0.0
            elif STATE.phase_elapsed > 5.0:
                finish("FAIL", "TattooShop did not become open/visible in Character Creation Tattoo tab")
            return

        if STATE.phase == "wait_applied":
            if STATE.phase_elapsed < 3.0:
                return
            world = EDITOR_LEVEL_LIBRARY.get_game_world()
            SCREENSHOT_FILE.parent.mkdir(parents=True, exist_ok=True)
            screenshot_accepted = False
            try:
                screenshot_accepted = bool(
                    unreal.AutomationLibrary.take_high_res_screenshot(
                        1280,
                        720,
                        str(SCREENSHOT_FILE),
                    )
                )
            except Exception as exc:
                STATE.result["screenshot_error"] = str(exc)
            STATE.result["screenshot_accepted"] = screenshot_accepted
            STATE.phase = "wait_capture"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_capture":
            if STATE.phase_elapsed < 8.0:
                return
            overlay = STATE.result.get("overlay_report", {})
            target_skin = str(overlay.get("target_skin", ""))
            checks = {
                "tattoo_shop_open": STATE.result.get("tattoo_shop_open") is True,
                "heart_tattoo_applied": STATE.result.get("heart_tattoo_applied") is True,
                "target_skin_resolved": bool(target_skin),
                "screenshot_created": SCREENSHOT_FILE.is_file(),
            }
            STATE.result["checks"] = checks
            finish(
                "PASS" if all(checks.values()) else "FAIL",
                "" if all(checks.values()) else "TattooShop tattoo application checks failed",
            )
            return
    except Exception as exc:
        STATE.result["traceback"] = traceback.format_exc()
        finish("FAIL", str(exc))


if getattr(builtins, "_codex_tattooshop_cc_pie", None) is None:
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    STATE.handle = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_tattooshop_cc_pie = STATE
else:
    unreal.log_warning("TattooShop Character Creation PIE validation already registered")
