"""Validate source DQ collection wiring and capture Golden Palace during Crawl."""

import builtins
import json
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
EVIDENCE_DIR = (
    PROJECT_DIR / "Saved" / "Migration" / "AnimationMigration" / "20260719"
)
OUTPUT_FILE = EVIDENCE_DIR / "UE58_GoldenPalaceCrawl.json"
SCREENSHOT = EVIDENCE_DIR / "UE58_GoldenPalace_Crawl_Side.png"
TARGET_MAP = "/Game/_Game/Hub/HUB"
TARGET_MAP_NAME = "hub"
EXPECTED_DEFORMER = (
    "/DeformerGraph/Deformers/"
    "DG_DualQuatSkin_Morph_Cloth.DG_DualQuatSkin_Morph_Cloth"
)
EXPECTED_COLLECTION = (
    "/Game/DazToUnreal/Deformers/"
    "MDC_DazDualQuatMorph.MDC_DazDualQuatMorph"
)

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def call(obj, method, *args):
    function = getattr(obj, method, None)
    if not callable(function):
        raise RuntimeError(f"Missing reflected method {method} on {obj}")
    return function(*args)


def object_path(value):
    return value.get_path_name() if value else ""


def world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def find_component(actor, fragment):
    if not actor:
        return None
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if fragment in component.get_class().get_name():
            return component
    return None


class State:
    def __init__(self):
        self.phase = "load_map"
        self.elapsed = 0.0
        self.started = time.monotonic()
        self.callback = None
        self.player = None
        self.controller = None
        self.locomotion = None
        self.mesh = None
        self.camera = None
        self.camera_name = ""
        self.result = {
            "status": "UE58_GOLDEN_PALACE_CRAWL_FAIL",
            "assets_saved": False,
            "errors": [],
        }


STATE = State()


def finish(success):
    try:
        if STATE.locomotion:
            call(STATE.locomotion, "set_crawl_mode_enabled", False)
            call(STATE.locomotion, "set_walk_mode_enabled", False)
    except Exception:
        pass
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_golden_palace_crawl_callback = None
    STATE.result["screenshot_exists"] = SCREENSHOT.is_file()
    STATE.result["status"] = (
        "UE58_GOLDEN_PALACE_CRAWL_PASS"
        if success
        else "UE58_GOLDEN_PALACE_CRAWL_FAIL"
    )
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(STATE.result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    unreal.log("[GoldenPalaceCrawl58] " + json.dumps(STATE.result))
    if LEVEL_EDITOR.is_in_play_in_editor():
        EDITOR_LEVEL_LIBRARY.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


def resolve_context():
    world = EDITOR_LEVEL_LIBRARY.get_game_world()
    STATE.player = (
        unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
    )
    STATE.controller = (
        unreal.GameplayStatics.get_player_controller(world, 0)
        if world
        else None
    )
    STATE.locomotion = find_component(
        STATE.player, "ProjectLocomotionOverrideComponent"
    )
    STATE.mesh = (
        STATE.player.get_component_by_class(unreal.SkeletalMeshComponent)
        if STATE.player
        else None
    )
    if world and STATE.camera_name:
        for camera in unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.CameraActor
        ):
            if camera.get_name().startswith(STATE.camera_name):
                STATE.camera = camera
                break
    return all(
        (
            world,
            STATE.player,
            STATE.controller,
            STATE.locomotion,
            STATE.mesh,
            STATE.camera,
        )
    )


def position_camera():
    location = STATE.player.get_actor_location()
    right = STATE.player.get_actor_right_vector()
    camera_location = location + right * 225.0 + unreal.Vector(0, 0, 62)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(
        camera_location, location + unreal.Vector(0, 0, 50)
    )
    STATE.camera.set_actor_location_and_rotation(
        camera_location, camera_rotation, False, True
    )
    STATE.controller.set_view_target_with_blend(STATE.camera, 0.0)


def mesh_deformer_metadata(mesh_asset):
    default = (
        mesh_asset.get_default_mesh_deformer()
        if hasattr(mesh_asset, "get_default_mesh_deformer")
        else mesh_asset.get_editor_property("default_mesh_deformer")
    )
    collection = (
        mesh_asset.get_target_mesh_deformers()
        if hasattr(mesh_asset, "get_target_mesh_deformers")
        else mesh_asset.get_editor_property("target_mesh_deformers")
    )
    material_rows = []
    golden_palace_rows = []
    for index, skeletal_material in enumerate(
        mesh_asset.get_editor_property("materials")
    ):
        material = skeletal_material.get_editor_property(
            "material_interface"
        )
        row = {
            "index": index,
            "slot": str(
                skeletal_material.get_editor_property("material_slot_name")
            ),
            "material": object_path(material),
        }
        material_rows.append(row)
        if "goldenpalace" in (row["slot"] + row["material"]).lower():
            golden_palace_rows.append(row)
    return {
        "mesh": object_path(mesh_asset),
        "default_mesh_deformer": object_path(default),
        "target_mesh_deformer_collection": object_path(collection),
        "material_count": len(material_rows),
        "golden_palace_materials": golden_palace_rows,
    }


def tick(delta_time):
    try:
        STATE.elapsed += delta_time
        if time.monotonic() - STATE.started > 90.0:
            STATE.result["errors"].append("timeout:" + STATE.phase)
            finish(False)
            return

        if STATE.phase == "load_map":
            EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
            LEVEL_EDITOR.load_level(TARGET_MAP)
            STATE.phase = "wait_map"
            STATE.elapsed = 0.0
            return

        if STATE.phase == "wait_map":
            if (
                world_name(UNREAL_EDITOR.get_editor_world()) != TARGET_MAP_NAME
                or STATE.elapsed < 1.0
            ):
                return
            camera = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.CameraActor, unreal.Vector(), unreal.Rotator()
            )
            if not camera:
                raise RuntimeError("Could not create transient camera")
            camera.set_actor_label("CodexGoldenPalaceCrawlCamera")
            STATE.camera_name = camera.get_name()
            LEVEL_EDITOR.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.elapsed = 0.0
            return

        if STATE.phase == "wait_pie":
            if (
                not LEVEL_EDITOR.is_in_play_in_editor()
                or STATE.elapsed < 2.0
                or not resolve_context()
            ):
                return
            STATE.controller.set_ignore_move_input(True)
            STATE.controller.set_ignore_look_input(True)
            movement = STATE.player.get_component_by_class(
                unreal.CharacterMovementComponent
            )
            if movement:
                movement.stop_movement_immediately()
            position_camera()
            call(STATE.locomotion, "set_walk_mode_enabled", True)
            call(STATE.locomotion, "set_crawl_mode_enabled", True)
            STATE.phase = "wait_crawl_idle"
            STATE.elapsed = 0.0
            return

        if STATE.phase == "wait_crawl_idle" and STATE.elapsed >= 4.5:
            mesh_asset = STATE.mesh.get_skeletal_mesh_asset()
            STATE.result["mesh"] = mesh_deformer_metadata(mesh_asset)
            STATE.result["crawl"] = {
                "active": bool(
                    call(STATE.locomotion, "is_crawl_mode_active")
                ),
                "animation": str(
                    call(
                        STATE.locomotion,
                        "get_current_animation_asset_name",
                    )
                ),
                "active_deformer": str(
                    call(
                        STATE.locomotion,
                        "get_active_crawl_mesh_deformer_name",
                    )
                ),
            }
            position_camera()
            STATE.phase = "capture"
            STATE.elapsed = 0.0
            STATE.result["screenshot_accepted"] = bool(
                unreal.AutomationLibrary.take_high_res_screenshot(
                    1280, 720, str(SCREENSHOT)
                )
            )
            return

        if STATE.phase == "capture" and STATE.elapsed >= 1.5:
            mesh = STATE.result["mesh"]
            crawl = STATE.result["crawl"]
            success = (
                mesh["default_mesh_deformer"] == EXPECTED_DEFORMER
                and mesh["target_mesh_deformer_collection"]
                == EXPECTED_COLLECTION
                and bool(mesh["golden_palace_materials"])
                and crawl["active"]
                and crawl["animation"] == "Anim_KA_Crawling_Baby_Idle"
                and crawl["active_deformer"] == EXPECTED_DEFORMER
                and STATE.result["screenshot_accepted"]
                and SCREENSHOT.is_file()
            )
            finish(success)
    except Exception as exc:
        STATE.result["errors"].append(str(exc))
        STATE.result["traceback"] = traceback.format_exc()
        unreal.log_error(STATE.result["traceback"])
        finish(False)


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_golden_palace_crawl_callback = STATE.callback
unreal.log("[GoldenPalaceCrawl58] registered")
