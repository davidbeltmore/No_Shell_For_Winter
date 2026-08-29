"""Visible PIE QA for 81 food pickables plus WellFed/Alcoholized runtime logic."""

from __future__ import annotations

import builtins
import json
import math
import os
import traceback
from pathlib import Path

import unreal


PROJECT = Path(unreal.Paths.project_dir())
MANIFEST_PATH = Path(os.environ.get("CODEX_FOODKIT81_MANIFEST", PROJECT / "Saved/Migration/FoodKitAlcohol/FoodKit81Manifest.json"))
OUTPUT_DIR = Path(os.environ.get("CODEX_FOODKIT81_PIE_OUTPUT", PROJECT / "Saved/Migration/FoodKitAlcohol/PIE"))
REPORT_PATH = OUTPUT_DIR / "FoodKitAlcoholPIE58.json"
MARKER_PATH = OUTPUT_DIR / "markers.txt"
ACK_PATH = OUTPUT_DIR / "capture_ack.txt"
TARGET_MAP = "/Engine/Maps/Entry"
TIMEOUT_SECONDS = 150.0


def write_text(path: Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def emit(message: str):
    unreal.log("[FoodKitAlcoholPIE58] " + message)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    with MARKER_PATH.open("a", encoding="utf-8") as handle:
        handle.write(message + "\n")


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def read_ack():
    try:
        return ACK_PATH.read_text(encoding="utf-8").strip()
    except Exception:
        return ""


def vector_add(*values):
    result = unreal.Vector(0.0, 0.0, 0.0)
    for value in values:
        result = result + value
    return result


def vector_scale(value, scalar):
    return unreal.Vector(value.x * scalar, value.y * scalar, value.z * scalar)


class State:
    phase = "load_map"
    phase_time = 0.0
    total_time = 0.0
    callback = None
    world = None
    pawn = None
    needs = None
    status = None
    locomotion = None
    spawned = []
    editor_pickups = []
    pie_pickups = []
    batch_index = 0
    batch_rows = []
    checks = {}
    samples = {}
    result = "PENDING"
    metabolism_start = 0.0
    screenshot_task = None
    screenshot_path = None


STATE = State()
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


with MANIFEST_PATH.open("r", encoding="utf-8-sig") as handle:
    MANIFEST = json.load(handle)
if MANIFEST.get("status") != "FOOD_KIT_81_MANIFEST_PASS" or MANIFEST.get("entry_count") != 81:
    raise RuntimeError("FoodKit manifest is not PASS/81")
ENTRIES = MANIFEST["entries"]
CLASS_CACHE = {}


def find_component(actor, class_hint):
    if actor is None:
        return None
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if class_hint.lower() in component.get_class().get_name().lower():
            return component
    return None


def blueprint_class(package):
    if package in CLASS_CACHE:
        return CLASS_CACHE[package]
    value = unreal.EditorAssetLibrary.load_blueprint_class(package)
    if value is None:
        raise RuntimeError("Blueprint class failed to load: " + package)
    CLASS_CACHE[package] = value
    return value


def apply_consumable(item_package):
    source = unreal.get_default_object(blueprint_class(item_package))
    result = unreal.ProjectSurvivalConsumableBlueprintLibrary.apply_survival_consumable_from_source(STATE.pawn, source)
    emit("consume package={} result={}".format(item_package, result))
    if not result:
        raise RuntimeError("Consumable application failed: " + item_package)


def status_active(name):
    return bool(STATE.status.is_status_active(name))


def snapshot(label):
    value = {
        "hunger": float(STATE.needs.get_need_current_value("Hunger")),
        "thirst": float(STATE.needs.get_need_current_value("Thirst")),
        "alcohol": float(STATE.needs.get_sensation_current_value("Alcohol")),
        "well_fed": status_active("WellFed"),
        "alcoholized": status_active("Alcoholized"),
        "status_movement_multiplier": float(STATE.locomotion.get_resolved_status_movement_multiplier()),
        "active_statuses": [str(row.status_name) for row in STATE.status.build_active_status_snapshots()],
    }
    STATE.samples[label] = value
    emit("sample {} {}".format(label, json.dumps(value, sort_keys=True)))
    return value


def destroy_batch():
    for actor in STATE.spawned:
        try:
            actor.set_actor_hidden_in_game(True)
        except Exception:
            pass
    STATE.spawned = []


def begin_viewport_screenshot(filename):
    path = OUTPUT_DIR / filename
    if path.exists():
        path.unlink()
    STATE.screenshot_path = path
    STATE.screenshot_task = unreal.AutomationLibrary.take_high_res_screenshot(
        1920,
        1080,
        str(path),
        delay=0.2,
        force_game_view=True,
    )
    emit("viewport_screenshot_requested=" + filename)


def viewport_screenshot_ready():
    return bool(
        STATE.screenshot_path
        and STATE.screenshot_path.exists()
        and STATE.screenshot_path.stat().st_size > 0
    )


def clear_viewport_screenshot():
    STATE.screenshot_task = None
    STATE.screenshot_path = None


def prepare_editor_pickups():
    if STATE.editor_pickups:
        return
    staging_location = unreal.Vector(0.0, 0.0, -100000.0)
    for index, entry in enumerate(ENTRIES):
        actor = EDITOR_ACTORS.spawn_actor_from_class(
            blueprint_class(entry["pickup_package"]),
            staging_location,
            unreal.Rotator(0.0, 0.0, 0.0),
        )
        if actor is None:
            raise RuntimeError("Editor transient pickup spawn failed: " + entry["pickup_package"])
        actor.set_actor_hidden_in_game(True)
        actor.set_actor_enable_collision(False)
        actor.set_editor_property("tags", ["FoodKitQA_{:03d}".format(index)])
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            component.set_simulate_physics(False)
            component.set_enable_gravity(False)
        STATE.editor_pickups.append(actor)
    emit("transient_editor_pickups_prepared={}".format(len(STATE.editor_pickups)))


def resolve_pie_pickups():
    if STATE.pie_pickups:
        return True
    resolved = []
    for index in range(len(ENTRIES)):
        actors = list(unreal.GameplayStatics.get_all_actors_with_tag(STATE.world, "FoodKitQA_{:03d}".format(index)))
        if len(actors) != 1:
            return False
        resolved.append(actors[0])
    STATE.pie_pickups = resolved
    emit("pie_pickups_resolved={}".format(len(resolved)))
    return True


def spawn_batch(batch_index):
    destroy_batch()
    start = batch_index * 9
    rows = ENTRIES[start : start + 9]
    camera = unreal.GameplayStatics.get_player_camera_manager(STATE.world, 0)
    if camera is None:
        raise RuntimeError("PlayerCameraManager is absent")
    origin = camera.get_camera_location()
    rotation = camera.get_camera_rotation()
    forward = unreal.MathLibrary.get_forward_vector(rotation)
    right = unreal.MathLibrary.get_right_vector(rotation)
    up = unreal.Vector(0.0, 0.0, 1.0)
    actor_rotation = unreal.Rotator(0.0, rotation.yaw + 180.0, 0.0)
    names = []
    for index, entry in enumerate(rows):
        col = index % 3
        row = index // 3
        location = vector_add(
            origin,
            vector_scale(forward, 260.0),
            vector_scale(right, (col - 1) * 75.0),
            vector_scale(up, (1 - row) * 70.0 - 5.0),
        )
        transform = unreal.Transform(location=location, rotation=actor_rotation, scale=unreal.Vector(1.0, 1.0, 1.0))
        actor = STATE.pie_pickups[start + index]
        actor.set_actor_transform(transform, False, True)
        actor.set_actor_hidden_in_game(False)
        actor.set_actor_enable_collision(False)
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            component.set_simulate_physics(False)
            component.set_enable_gravity(False)
        STATE.spawned.append(actor)
        names.append(entry["pickup_package"].rsplit("/", 1)[-1])
    STATE.batch_rows.append({"batch": batch_index + 1, "count": len(rows), "pickups": names})
    emit("visual_batch_ready={} count={} names={}".format(batch_index + 1, len(rows), ",".join(names)))


def finish(success, error=""):
    try:
        destroy_batch()
        if LEVEL.is_in_play_in_editor():
            unreal.EditorLevelLibrary.editor_end_play()
    except Exception:
        pass
    STATE.result = "PASS" if success else "FAIL"
    payload = {
        "schema_version": 1,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "status": "UE58_FOODKIT_ALCOHOL_PIE_PASS" if success else "UE58_FOODKIT_ALCOHOL_PIE_FAIL",
        "error": error,
        "manifest_fingerprint": MANIFEST["fingerprint"],
        "visual_batch_count": len(STATE.batch_rows),
        "visual_pickup_count": sum(row["count"] for row in STATE.batch_rows),
        "visual_batches": STATE.batch_rows,
        "checks": STATE.checks,
        "samples": STATE.samples,
        "map_saved": False,
    }
    write_text(REPORT_PATH, json.dumps(payload, indent=2, sort_keys=True) + "\n")
    emit("finished status={} report={}".format(STATE.result, REPORT_PATH))
    if STATE.callback is not None:
        unreal.unregister_slate_post_tick_callback(STATE.callback)
        STATE.callback = None
        builtins._codex_foodkit_alcohol_pie_callback = None
    unreal.SystemLibrary.quit_editor()


def tick(delta_time):
    try:
        STATE.total_time += delta_time
        STATE.phase_time += delta_time
        if STATE.total_time > TIMEOUT_SECONDS:
            finish(False, "timeout phase=" + STATE.phase)
            return

        if STATE.phase == "load_map":
            OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
            write_text(MARKER_PATH, "")
            write_text(ACK_PATH, "")
            emit("loading_map=" + TARGET_MAP)
            STATE.phase = "start_pie"
            STATE.phase_time = 0.0
            LEVEL.load_level(TARGET_MAP)
            return

        if STATE.phase == "start_pie":
            if STATE.phase_time < 1.0:
                return
            for entry in ENTRIES:
                blueprint_class(entry["pickup_package"])
                blueprint_class(entry["item_package"])
            emit("blueprint_classes_preloaded={}".format(len(CLASS_CACHE)))
            prepare_editor_pickups()
            LEVEL.editor_request_begin_play()
            STATE.phase = "wait_pie"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wait_pie":
            if not LEVEL.is_in_play_in_editor() or STATE.phase_time < 1.0:
                return
            STATE.world = unreal.EditorLevelLibrary.get_game_world()
            STATE.pawn = unreal.GameplayStatics.get_player_pawn(STATE.world, 0)
            if STATE.pawn is None:
                return
            STATE.needs = find_component(STATE.pawn, "ProjectSurvivalNeedsComponent")
            STATE.status = find_component(STATE.pawn, "ProjectSurvivalStatusComponent")
            STATE.locomotion = find_component(STATE.pawn, "ProjectLocomotionOverrideComponent")
            if not all((STATE.needs, STATE.status, STATE.locomotion)):
                return
            if not resolve_pie_pickups():
                return
            movement = find_component(STATE.pawn, "CharacterMovementComponent")
            if movement:
                try:
                    movement.disable_movement()
                except Exception:
                    pass
            emit("pie_components_ready=True pawn=" + object_path(STATE.pawn))
            unreal.SystemLibrary.execute_console_command(STATE.world, "viewmode unlit")
            STATE.phase = "spawn_batch"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "spawn_batch":
            if STATE.batch_index >= 9:
                destroy_batch()
                STATE.phase = "wellfed_initial_clear"
                STATE.phase_time = 0.0
                STATE.needs.set_need_decay_rate("Hunger", 0.0)
                STATE.needs.set_need_decay_rate("Thirst", 0.0)
                STATE.needs.set_need_current_value("Hunger", 75.0)
                return
            spawn_batch(STATE.batch_index)
            begin_viewport_screenshot("food_batch_{:02d}.png".format(STATE.batch_index + 1))
            STATE.phase = "wait_batch_capture"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wellfed_initial_clear":
            if STATE.phase_time < 0.5:
                return
            STATE.needs.set_need_current_value("Hunger", 89.0)
            STATE.phase = "wellfed_89"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wait_batch_capture":
            if not viewport_screenshot_ready():
                return
            clear_viewport_screenshot()
            STATE.batch_index += 1
            STATE.phase = "spawn_batch"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wellfed_89":
            if STATE.phase_time < 0.5:
                return
            sample = snapshot("wellfed_89")
            STATE.checks["wellfed_inactive_below_90"] = not sample["well_fed"]
            STATE.needs.set_need_current_value("Hunger", 90.0)
            STATE.phase = "wellfed_90"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wellfed_90":
            if STATE.phase_time < 0.5:
                return
            sample = snapshot("wellfed_90")
            STATE.checks["wellfed_active_at_90"] = sample["well_fed"]
            emit("wellfed_ui_ready=1")
            STATE.phase = "wait_wellfed_ack"
            return

        if STATE.phase == "wait_wellfed_ack":
            if read_ack() != "wellfed_ui":
                return
            begin_viewport_screenshot("wellfed_needs_status.png")
            STATE.phase = "wait_wellfed_capture"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wait_wellfed_capture":
            if not viewport_screenshot_ready():
                return
            clear_viewport_screenshot()
            STATE.needs.set_need_current_value("Hunger", 80.0)
            STATE.phase = "wellfed_80"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wellfed_80":
            if STATE.phase_time < 0.5:
                return
            sample = snapshot("wellfed_80")
            STATE.checks["wellfed_hysteresis_80"] = sample["well_fed"]
            STATE.needs.set_need_current_value("Hunger", 75.0)
            STATE.phase = "wellfed_75"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wellfed_75":
            if STATE.phase_time < 0.5:
                return
            sample = snapshot("wellfed_75")
            STATE.checks["wellfed_clears_at_75"] = not sample["well_fed"]
            STATE.needs.set_need_current_value("Thirst", 0.0)
            STATE.needs.set_sensation_current_value("Alcohol", 0.0)
            apply_consumable("/Game/_Game/FoodSystem/Food/Items/Drink/BP_Drink_AlcoholBottle07")
            STATE.phase = "alcohol_first"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "alcohol_first":
            if STATE.phase_time < 0.5:
                return
            sample = snapshot("alcohol_first")
            STATE.checks["first_alcohol_adds_30"] = 29.5 <= sample["alcohol"] <= 30.0
            STATE.checks["first_alcohol_adds_24_thirst"] = 23.5 <= sample["thirst"] <= 24.1
            STATE.checks["alcoholized_active_at_25"] = sample["alcoholized"]
            STATE.checks["alcoholized_movement_085"] = math.isclose(sample["status_movement_multiplier"], 0.85, abs_tol=0.001)
            emit("alcoholized_ui_ready=1")
            STATE.phase = "wait_alcohol_ack"
            return

        if STATE.phase == "wait_alcohol_ack":
            if read_ack() != "alcoholized_ui":
                return
            begin_viewport_screenshot("alcoholized_needs_status.png")
            STATE.phase = "wait_alcohol_capture"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "wait_alcohol_capture":
            if not viewport_screenshot_ready():
                return
            clear_viewport_screenshot()
            for number in (8, 9, 10):
                apply_consumable("/Game/_Game/FoodSystem/Food/Items/Drink/BP_Drink_AlcoholBottle{:02d}".format(number))
            STATE.phase = "alcohol_stacked"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "alcohol_stacked":
            if STATE.phase_time < 0.25:
                return
            sample = snapshot("alcohol_stacked")
            STATE.checks["four_bottles_stack_and_clamp_100"] = 99.5 <= sample["alcohol"] <= 100.0
            STATE.checks["four_bottles_keep_thirst_contract"] = 95.5 <= sample["thirst"] <= 96.1
            STATE.metabolism_start = sample["alcohol"]
            STATE.phase = "metabolism_wait"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "metabolism_wait":
            if STATE.phase_time < 4.0:
                return
            sample = snapshot("alcohol_after_4_seconds")
            metabolized = STATE.metabolism_start - sample["alcohol"]
            STATE.checks["alcohol_metabolism_025_per_second"] = 0.85 <= metabolized <= 1.2
            STATE.needs.set_sensation_current_value("Alcohol", 15.0)
            STATE.phase = "alcohol_15"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "alcohol_15":
            if STATE.phase_time < 0.5:
                return
            sample = snapshot("alcohol_15")
            STATE.checks["alcoholized_hysteresis_15"] = sample["alcoholized"]
            STATE.needs.set_sensation_current_value("Alcohol", 10.0)
            STATE.phase = "alcohol_10"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "alcohol_10":
            if STATE.phase_time < 0.5:
                return
            sample = snapshot("alcohol_10")
            STATE.checks["alcoholized_clears_at_10"] = not sample["alcoholized"]
            STATE.checks["movement_restores_after_alcoholized"] = math.isclose(sample["status_movement_multiplier"], 1.0, abs_tol=0.001)
            STATE.needs.set_sensation_current_value("Alcohol", 0.0)
            STATE.needs.set_need_current_value("Hunger", 10.0)
            apply_consumable("/Game/_Game/FoodSystem/Food/Items/Food/BP_Food_Apple01")
            STATE.phase = "food_no_alcohol"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "food_no_alcohol":
            if STATE.phase_time < 0.3:
                return
            sample = snapshot("food_no_alcohol")
            STATE.checks["food_restores_hunger"] = 21.5 <= sample["hunger"] <= 22.1
            STATE.checks["food_does_not_add_alcohol"] = sample["alcohol"] <= 0.01
            STATE.needs.set_need_current_value("Thirst", 10.0)
            apply_consumable("/Game/_Game/FoodSystem/Food/Items/Drink/BP_Drink_WaterBottle01")
            STATE.phase = "water_no_alcohol"
            STATE.phase_time = 0.0
            return

        if STATE.phase == "water_no_alcohol":
            if STATE.phase_time < 0.3:
                return
            sample = snapshot("water_no_alcohol")
            STATE.checks["water_restores_thirst"] = 47.5 <= sample["thirst"] <= 48.1
            STATE.checks["water_does_not_add_alcohol"] = sample["alcohol"] <= 0.01
            failed = sorted(name for name, passed in STATE.checks.items() if not passed)
            finish(not failed, "failed checks: " + ", ".join(failed) if failed else "")
            return
    except Exception as exc:
        finish(False, "{}\n{}".format(exc, traceback.format_exc()))


old = getattr(builtins, "_codex_foodkit_alcohol_pie_callback", None)
if old is not None:
    try:
        unreal.unregister_slate_post_tick_callback(old)
    except Exception:
        pass
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_foodkit_alcohol_pie_callback = STATE.callback
emit("callback_registered=True")
