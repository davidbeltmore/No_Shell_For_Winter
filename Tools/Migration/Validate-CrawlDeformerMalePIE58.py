"""Focused UE 5.8 PIE validation for the Male Crawl mesh deformer.

The real HUB player is switched to the current Male mesh only inside PIE,
while Crawl is active. The script verifies the project locomotion component's
deformer state, captures evidence, restores the original Female mesh, ends PIE,
and quits the validation editor without saving runtime changes.
"""

import builtins
import json
import os
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
EVIDENCE_DIR = Path(
    os.environ.get(
        "CODEX_ANIMATION_MIGRATION_QA_DIR",
        PROJECT_DIR / "Saved" / "Migration" / "AnimationMigration" / "20260719",
    )
)
OUTPUT_FILE = EVIDENCE_DIR / "UE58_CrawlDeformerMalePIE.json"
SCREENSHOT = EVIDENCE_DIR / "UE58_Crawl_Male_Runtime.png"
TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
FEMALE_PATH = "/Game/DazToUnreal/Female/Female.Female"
MALE_PATH = "/Game/DazToUnreal/Male/Male.Male"
DEFORMER_PATH = (
    "/DeformerGraph/Deformers/"
    "DG_DualQuatSkin_Morph_Cloth.DG_DualQuatSkin_Morph_Cloth"
)
TIMEOUT_SECONDS = 90.0

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


def call(obj, method_name, *args):
    method = getattr(obj, method_name, None)
    if not callable(method):
        raise RuntimeError(f"Missing reflected method {method_name} on {obj}")
    return method(*args)


def world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def find_component(actor, class_name_fragment):
    if not actor:
        return None
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if class_name_fragment in component.get_class().get_name():
            return component
    return None


def find_visible_body_mesh(actor):
    if not actor:
        return None
    fallback = None
    for component in actor.get_components_by_class(unreal.SkeletalMeshComponent):
        mesh_asset = call(component, "get_skeletal_mesh_asset")
        if not mesh_asset:
            continue
        fallback = fallback or component
        if path(mesh_asset) in (FEMALE_PATH, MALE_PATH):
            return component
    return fallback


def request_screenshot():
    SCREENSHOT.parent.mkdir(parents=True, exist_ok=True)
    take_screenshot = getattr(unreal.AutomationLibrary, "take_high_res_screenshot", None)
    if not callable(take_screenshot):
        return {"requested": False, "reason": "take_high_res_screenshot unavailable"}
    return {
        "requested": True,
        "accepted": bool(take_screenshot(1280, 720, str(SCREENSHOT))),
        "path": str(SCREENSHOT),
    }


class State:
    def __init__(self):
        self.phase = "load_map"
        self.phase_elapsed = 0.0
        self.started_at = time.monotonic()
        self.callback = None
        self.player = None
        self.locomotion = None
        self.mesh_component = None
        self.original_mesh = None
        self.result = {
            "status": "UE58_CRAWL_DEFORMER_MALE_PIE_FAIL",
            "map": TARGET_MAP,
            "female_path": FEMALE_PATH,
            "male_path": MALE_PATH,
            "deformer_path": DEFORMER_PATH,
            "female_before": {},
            "male_crawl": {},
            "restored": {},
            "screenshot": {},
            "errors": [],
        }


STATE = State()


def restore_runtime_state():
    if STATE.mesh_component and STATE.original_mesh:
        try:
            call(STATE.mesh_component, "set_skeletal_mesh_asset", STATE.original_mesh)
        except Exception:
            pass
    if STATE.locomotion:
        for method_name, value in (
            ("set_crawl_mode_enabled", False),
            ("set_walk_mode_enabled", False),
        ):
            try:
                call(STATE.locomotion, method_name, value)
            except Exception:
                pass


def finish(success):
    restore_runtime_state()
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_crawl_deformer_male_qa_callback = None
    STATE.result["screenshot"]["exists"] = SCREENSHOT.is_file()
    STATE.result["status"] = (
        "UE58_CRAWL_DEFORMER_MALE_PIE_PASS"
        if success
        else "UE58_CRAWL_DEFORMER_MALE_PIE_FAIL"
    )
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[CrawlDeformerMalePIE58] " + json.dumps(STATE.result, ensure_ascii=True))
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def resolve_context():
    world = EDITOR_LEVEL_LIBRARY.get_game_world()
    player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    locomotion = find_component(player, "ProjectLocomotionOverrideComponent")
    mesh_component = find_visible_body_mesh(player)
    if not all((world, player, locomotion, mesh_component)):
        return False
    STATE.player = player
    STATE.locomotion = locomotion
    STATE.mesh_component = mesh_component
    STATE.original_mesh = call(mesh_component, "get_skeletal_mesh_asset")
    return True


def tick(delta_time):
    try:
        STATE.phase_elapsed += delta_time
        if time.monotonic() - STATE.started_at > TIMEOUT_SECONDS:
            STATE.result["errors"].append("timeout:" + STATE.phase)
            finish(False)
            return

        if STATE.phase == "load_map":
            LEVEL_EDITOR.load_level(TARGET_MAP)
            STATE.phase = "wait_map"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if world_name(editor_world) != TARGET_MAP_NAME or STATE.phase_elapsed < 1.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "wait_pie":
            if (
                not LEVEL_EDITOR.is_in_play_in_editor()
                or STATE.phase_elapsed < 2.0
                or not resolve_context()
            ):
                return
            STATE.result["player"] = path(STATE.player)
            STATE.result["female_before"] = {
                "mesh": path(STATE.original_mesh),
                "deformer_applied": bool(
                    call(STATE.locomotion, "is_crawl_mesh_deformer_applied")
                ),
            }
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            call(STATE.locomotion, "set_crawl_mode_enabled", True)
            STATE.phase = "female_crawl"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "female_crawl" and STATE.phase_elapsed >= 1.5:
            male_mesh = unreal.load_asset(MALE_PATH)
            if not male_mesh:
                raise RuntimeError("Unable to load current Male mesh")
            call(STATE.mesh_component, "set_skeletal_mesh_asset", male_mesh)
            STATE.phase = "male_crawl"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "male_crawl" and STATE.phase_elapsed >= 2.0:
            STATE.result["male_crawl"] = {
                "mesh": path(call(STATE.mesh_component, "get_skeletal_mesh_asset")),
                "walk_enabled": bool(call(STATE.locomotion, "is_walk_mode_enabled")),
                "crawl_active": bool(call(STATE.locomotion, "is_crawl_mode_active")),
                "deformer_applied": bool(
                    call(STATE.locomotion, "is_crawl_mesh_deformer_applied")
                ),
                "deformer": str(
                    call(STATE.locomotion, "get_active_crawl_mesh_deformer_name")
                ),
                "animation": str(
                    call(STATE.locomotion, "get_current_animation_asset_name")
                ),
            }
            STATE.result["screenshot"] = request_screenshot()
            STATE.phase = "male_screenshot"
            STATE.phase_elapsed = 0.0
            return

        if STATE.phase == "male_screenshot" and STATE.phase_elapsed >= 1.5:
            call(STATE.mesh_component, "set_skeletal_mesh_asset", STATE.original_mesh)
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
            STATE.result["restored"] = {
                "mesh": path(call(STATE.mesh_component, "get_skeletal_mesh_asset")),
                "deformer_applied": bool(
                    call(STATE.locomotion, "is_crawl_mesh_deformer_applied")
                ),
                "deformer": str(
                    call(STATE.locomotion, "get_active_crawl_mesh_deformer_name")
                ),
            }
            male = STATE.result["male_crawl"]
            restored = STATE.result["restored"]
            success = all(
                (
                    STATE.result["female_before"].get("mesh") == FEMALE_PATH,
                    not STATE.result["female_before"].get("deformer_applied"),
                    male.get("mesh") == MALE_PATH,
                    male.get("walk_enabled"),
                    male.get("crawl_active"),
                    male.get("deformer_applied"),
                    male.get("deformer") == DEFORMER_PATH,
                    restored.get("mesh") == FEMALE_PATH,
                    not restored.get("deformer_applied"),
                )
            )
            finish(success)
            return
    except Exception as exc:
        STATE.result["errors"].append(f"{exc}\n{traceback.format_exc()}")
        finish(False)


old_callback = getattr(builtins, "_codex_crawl_deformer_male_qa_callback", None)
if old_callback is not None:
    try:
        unreal.unregister_slate_post_tick_callback(old_callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_crawl_deformer_male_qa_callback = STATE.callback
unreal.log("[CrawlDeformerMalePIE58] registered")
