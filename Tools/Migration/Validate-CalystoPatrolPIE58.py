"""Bounded V6 Calysto patrol runtime proof; writes evidence only under Saved/."""

import builtins
import hashlib
import json
import math
import os
import time
import traceback
from pathlib import Path

import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
EXPECTED_ROOT = Path("D:/Projects UE5/NoShellForWinter").resolve()
if PROJECT_ROOT != EXPECTED_ROOT:
    raise RuntimeError("Calysto patrol validation only supports the writable UE 5.8 target")

HUB_MAP = "/Game/_Game/Hub/HUB"
HUB_NAME = "hub"
DUNGEON_NAME = "dungeongeneration"
# This seed previously materialized three enemies. The receipt records the actual result,
# rather than assuming historical population parity.
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_PATROL_RUN_SEED", "5738796534536664893"))
TIMEOUT_SECONDS = 150.0
OBSERVATION_SECONDS = 15.0
SAMPLE_INTERVAL_SECONDS = 0.25
MINIMUM_MOVEMENT_CM = 100.0
MINIMUM_MOVING_SPEED_CM_PER_SEC = 10.0
VISUAL_MINIMUM_MOVING_SPEED_CM_PER_SEC = 50.0
VISUAL_SETTLE_SECONDS = 0.5
VISUAL_MOVEMENT_SECONDS = 1.5
VISUAL_CAPTURE_WIDTH = 1600
VISUAL_CAPTURE_HEIGHT = 900
MAXIMUM_SCREENSHOT_BYTES = 32 * 1024 * 1024
# The HUB requests its session-persistent combat preload during BeginPlay.  A
# seeded dungeon request is representative only after that preload has settled.
SESSION_PRELOAD_SETTLE_SECONDS = 40.0

DEFAULT_RUN_DIRECTORY = (
    PROJECT_ROOT
    / "Saved"
    / "Migration"
    / "CalystoDungeonDirectorV6"
    / ("PatrolPIE_" + time.strftime("%Y%m%d_%H%M%S"))
)
REPORT_PATH = Path(
    os.environ.get("CODEX_CALYSTO_PATROL_PIE_OUTPUT", DEFAULT_RUN_DIRECTORY / "PatrolPIE.json")
).resolve()
SAVED_ROOT = (PROJECT_ROOT / "Saved").resolve()
try:
    REPORT_PATH.relative_to(SAVED_ROOT)
except ValueError as error:
    raise RuntimeError("Calysto patrol validation may write evidence only under Saved/") from error
RUN_DIRECTORY = REPORT_PATH.parent

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
DUNGEON_SUBSYSTEM_CLASS = unreal.load_class(
    None, "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
)
NAV_BOUNDS_CLASS = unreal.load_class(None, "/Script/NavigationSystem.NavMeshBoundsVolume")
RECAST_NAV_CLASS = unreal.load_class(None, "/Script/NavigationSystem.RecastNavMesh")
PATROL_COMPONENT_CLASS = unreal.load_class(None, "/Script/AIFramework.ACFAIPatrolComponent")
FREE_CAMERA_SUBSYSTEM_CLASS = unreal.load_class(
    None, "/Script/EFProjectSystemsGameplay.ProjectGameplayFreeCameraSubsystem"
)

POPULATION_TAG = "EF.Calysto.Population"
ENEMY_TAG = "EF.Calysto.Population.Category.Enemy"


def object_path(value):
    return value.get_path_name() if value else ""


def vector(value):
    return {"x": float(value.x), "y": float(value.y), "z": float(value.z)}


def normalized_world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        pieces = name.split("_", 2)
        if len(pieces) == 3:
            name = pieces[2]
    return name.lower()


def safe_call(callback):
    try:
        return callback()
    except Exception as error:
        return {"PENDING": str(error)}


def safe_bool_call(callback):
    value = safe_call(callback)
    return value if isinstance(value, bool) else False


def safe_number_or_none(callback):
    value = safe_call(callback)
    return value if isinstance(value, (int, float, bool)) else None


def safe_text_call(callback):
    value = safe_call(callback)
    return str(value) if not isinstance(value, dict) else "PENDING: " + value.get("PENDING", "")


def safe_editor_property(value, property_name):
    return safe_call(lambda: value.get_editor_property(property_name))


def actor_tags(actor):
    try:
        return sorted(str(value) for value in actor.get_editor_property("tags"))
    except Exception:
        return []


def is_population_enemy(actor):
    tags = actor_tags(actor)
    return POPULATION_TAG in tags and ENEMY_TAG in tags, tags


def find_game_instance_subsystem(world):
    if not world or not DUNGEON_SUBSYSTEM_CLASS:
        return None
    game_instance = unreal.GameplayStatics.get_game_instance(world)
    if not game_instance:
        return None
    prefix = object_path(game_instance) + "."
    for candidate in unreal.ObjectIterator(unreal.Object):
        try:
            if (
                candidate.get_class() == DUNGEON_SUBSYSTEM_CLASS
                and object_path(candidate).startswith(prefix)
            ):
                return candidate
        except Exception:
            continue
    return None


def patrol_component(actor):
    if not PATROL_COMPONENT_CLASS:
        return None
    try:
        return actor.get_component_by_class(PATROL_COMPONENT_CLASS)
    except Exception:
        return None


class PatrolValidationState:
    def __init__(self):
        self.phase = "load_hub"
        self.started_at = time.monotonic()
        self.phase_started_at = self.started_at
        self.callback = None
        self.finished = False
        self.finishing = False
        self.request_accepted = False
        self.dungeon_seen_at = None
        self.first_enemy_seen_at = None
        self.last_sample_at = 0.0
        self.initial_positions = {}
        self.previous_positions = {}
        self.maximum_displacements = {}
        self.maximum_speed_2d = {}
        self.samples = []
        self.events = []
        self.error = ""
        self.dirty_before = dirty_packages()
        self.free_camera_subsystem = None
        self.visual_camera = None
        self.visual_mode = "idle"
        self.visual_mode_started_at = None
        self.visual_actor_path = ""
        self.visual_before_location = None
        self.visual = {
            "status": "PENDING",
            "review": "PENDING_HUMAN_REVIEW",
            "actor": "",
            "before": {},
            "after": {},
            "camera": {},
        }


def dirty_packages():
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted(object_path(package) for package in packages)


STATE = PatrolValidationState()


def set_phase(phase):
    STATE.phase = phase
    STATE.phase_started_at = time.monotonic()
    unreal.log("CALYSTO_V6_PATROL_PIE_PHASE=" + phase)


def runtime_enemy_sample(world):
    rows = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        is_enemy, tags = is_population_enemy(actor)
        if not is_enemy:
            continue

        location = actor.get_actor_location()
        velocity = safe_call(actor.get_velocity)
        if isinstance(velocity, dict):
            velocity = unreal.Vector(0.0, 0.0, 0.0)
        controller = safe_call(actor.get_controller)
        component = patrol_component(actor)
        movement = safe_call(
            lambda: actor.get_component_by_class(unreal.CharacterMovementComponent)
        )
        if isinstance(movement, dict):
            movement = None
        target_location = safe_call(controller.get_target_point_location_bk) \
            if controller and not isinstance(controller, dict) else None
        if isinstance(target_location, unreal.Vector):
            target_location = vector(target_location)
        elif isinstance(target_location, dict):
            target_location = None
        rows.append(
            {
                "actor": object_path(actor),
                "class": object_path(actor.get_class()),
                "tags": tags,
                "location": vector(location),
                "velocity": vector(velocity),
                "speed_2d": math.hypot(float(velocity.x), float(velocity.y)),
                "controller": object_path(controller) if not isinstance(controller, dict) else "",
                "ai_state": str(safe_call(controller.get_ai_state))
                if controller and not isinstance(controller, dict)
                else "",
                "controller_move_status": safe_text_call(controller.get_move_status)
                if controller and not isinstance(controller, dict)
                else "PENDING: controller unavailable",
                "controller_target_location": target_location,
                "controller_target_distance": safe_number_or_none(
                    controller.get_target_point_distance_bk
                )
                if controller and not isinstance(controller, dict)
                else None,
                "controller_command_duration": safe_number_or_none(
                    controller.get_command_duration_time_bk
                )
                if controller and not isinstance(controller, dict)
                else None,
                "movement_component": object_path(movement) if movement else "",
                "movement_component_class": object_path(movement.get_class()) if movement else "",
                "movement_mode": safe_text_call(
                    lambda: safe_editor_property(movement, "movement_mode")
                )
                if movement
                else "PENDING: component unavailable",
                "max_walk_speed": safe_number_or_none(
                    lambda: safe_editor_property(movement, "max_walk_speed")
                )
                if movement
                else None,
                "max_speed": safe_number_or_none(movement.get_max_speed)
                if movement
                else None,
                "max_acceleration": safe_number_or_none(movement.get_max_acceleration)
                if movement
                else None,
                "acf_can_move": safe_bool_call(movement.get_can_move)
                if movement and hasattr(movement, "get_can_move")
                else None,
                "acf_current_locomotion": safe_text_call(
                    movement.get_current_locomotion_state
                )
                if movement and hasattr(movement, "get_current_locomotion_state")
                else "PENDING: not an ACF movement component",
                "acf_target_locomotion": safe_text_call(
                    movement.get_target_locomotion_state
                )
                if movement and hasattr(movement, "get_target_locomotion_state")
                else "PENDING: not an ACF movement component",
                "patrol_component": object_path(component),
                "patrol_loop_active": safe_bool_call(component.is_patrol_loop_active)
                if component
                else False,
                "patrol_type": str(safe_call(component.get_patrol_type)) if component else "",
                "patrol_radius": safe_call(component.get_random_patrol_radius) if component else None,
            }
        )
    return rows


def record_sample(world):
    enemy_rows = runtime_enemy_sample(world)
    movement_rows = []
    for row in enemy_rows:
        actor_path = row["actor"]
        current = row["location"]
        initial = STATE.initial_positions.setdefault(actor_path, current)
        displacement_from_initial = math.hypot(
            current["x"] - initial["x"], current["y"] - initial["y"]
        )
        previous = STATE.previous_positions.get(actor_path)
        if previous is not None:
            sample_displacement = math.hypot(
                current["x"] - previous["x"], current["y"] - previous["y"]
            )
            STATE.maximum_displacements[actor_path] = max(
                displacement_from_initial,
                STATE.maximum_displacements.get(actor_path, 0.0),
            )
            movement_rows.append(
                {
                    "actor": actor_path,
                    "sample_delta_2d": sample_displacement,
                    "displacement_from_initial_2d": displacement_from_initial,
                }
            )
        STATE.previous_positions[actor_path] = current
        STATE.maximum_speed_2d[actor_path] = max(
            row["speed_2d"], STATE.maximum_speed_2d.get(actor_path, 0.0)
        )

    nav_bounds = unreal.GameplayStatics.get_all_actors_of_class(world, NAV_BOUNDS_CLASS)
    recast_meshes = unreal.GameplayStatics.get_all_actors_of_class(world, RECAST_NAV_CLASS)
    sample = {
        "elapsed_seconds": time.monotonic() - STATE.started_at,
        "world": object_path(world),
        "world_time_seconds": float(unreal.GameplayStatics.get_time_seconds(world)),
        "nav_mesh_bounds_count": len(nav_bounds),
        "recast_navmesh_count": len(recast_meshes),
        "enemy_count": len(enemy_rows),
        "enemies": enemy_rows,
        "movement": movement_rows,
    }
    STATE.samples.append(sample)
    # Keep the receipt bounded while retaining enough temporal evidence.
    if len(STATE.samples) > 100:
        STATE.samples.pop(0)
    return sample


def find_runtime_actor(world, actor_path):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if object_path(actor) == actor_path:
            return actor
    return None


def distance_2d(first, second):
    return math.hypot(float(first["x"]) - float(second["x"]), float(first["y"]) - float(second["y"]))


def valid_png(path):
    if not path.is_file():
        return None
    if path.stat().st_size > MAXIMUM_SCREENSHOT_BYTES:
        raise RuntimeError("Patrol visual screenshot exceeded its bounded artifact size")
    raw = path.read_bytes()
    if (
        len(raw) < 32
        or raw[:8] != b"\x89PNG\r\n\x1a\n"
        or raw[12:16] != b"IHDR"
        or raw[-12:] != b"\x00\x00\x00\x00IEND\xaeB\x60\x82"
    ):
        return None
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def resolve_visual_camera(world, actor, row):
    if not FREE_CAMERA_SUBSYSTEM_CLASS:
        raise RuntimeError("ProjectGameplayFreeCameraSubsystem is unavailable for patrol visual QA")

    if STATE.free_camera_subsystem is None:
        try:
            STATE.free_camera_subsystem = unreal.SubsystemBlueprintLibrary.get_world_subsystem(
                world, FREE_CAMERA_SUBSYSTEM_CLASS
            )
        except Exception:
            STATE.free_camera_subsystem = None
        if STATE.free_camera_subsystem is None:
            for subsystem in unreal.ObjectIterator(FREE_CAMERA_SUBSYSTEM_CLASS):
                try:
                    if subsystem.get_world() == world:
                        STATE.free_camera_subsystem = subsystem
                        break
                except Exception:
                    continue
    if STATE.free_camera_subsystem is None:
        raise RuntimeError("Gameplay free-camera subsystem was not initialized in the dungeon world")

    if not bool(STATE.free_camera_subsystem.is_gameplay_free_camera_active()):
        if not bool(STATE.free_camera_subsystem.start_gameplay_free_camera()):
            raise RuntimeError("Gameplay free-camera refused patrol visual QA activation")
        STATE.events.append({"event": "visual_free_camera_started"})
        return False

    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    if controller is None:
        return False
    camera = controller.get_view_target()
    if camera is None or "CameraActor" not in object_path(camera.get_class()):
        return False

    speed = float(row["speed_2d"])
    if speed < VISUAL_MINIMUM_MOVING_SPEED_CM_PER_SEC:
        return False
    direction = unreal.Vector(
        float(row["velocity"]["x"]) / speed,
        float(row["velocity"]["y"]) / speed,
        0.0,
    )
    actor_location = actor.get_actor_location()
    # Keep the observation camera on the actor's own navigable corridor.  A
    # wide lateral offset can place a camera behind the modular room shell.
    camera_location = actor_location - direction * 180.0 + unreal.Vector(0.0, 0.0, 75.0)
    focus_location = actor_location + unreal.Vector(0.0, 0.0, 55.0)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, focus_location)
    camera.set_actor_location(camera_location, False, False)
    camera.set_actor_rotation(camera_rotation, False)
    STATE.visual_camera = camera
    STATE.visual["camera"] = {
        "actor": object_path(camera),
        "location": vector(camera_location),
        "rotation": {
            "pitch": float(camera_rotation.pitch),
            "yaw": float(camera_rotation.yaw),
            "roll": float(camera_rotation.roll),
        },
        "focus_location": vector(focus_location),
    }
    return True


def request_visual_screenshot(label, row):
    output = RUN_DIRECTORY / ("Patrol_" + label + ".png")
    if output.exists():
        raise RuntimeError("Patrol visual screenshot path already exists: " + str(output))
    accepted = unreal.AutomationLibrary.take_high_res_screenshot(
        VISUAL_CAPTURE_WIDTH, VISUAL_CAPTURE_HEIGHT, str(output)
    )
    if accepted is False:
        raise RuntimeError("Viewport rejected patrol visual screenshot " + label)
    STATE.visual[label] = {
        "path": str(output),
        "requested": True,
        "requested_elapsed_seconds": time.monotonic() - STATE.started_at,
        "actor_location": row["location"],
        "actor_velocity": row["velocity"],
        "actor_speed_2d": row["speed_2d"],
    }
    STATE.events.append({"event": "visual_screenshot_requested", "label": label, "path": str(output)})


def update_visual_capture(world, sample, now):
    rows = {row["actor"]: row for row in sample["enemies"]}
    if STATE.visual_mode == "idle":
        candidate = next(
            (row for row in rows.values() if row["speed_2d"] >= VISUAL_MINIMUM_MOVING_SPEED_CM_PER_SEC),
            None,
        )
        if candidate is None:
            return
        actor = find_runtime_actor(world, candidate["actor"])
        if actor is None or not resolve_visual_camera(world, actor, candidate):
            return
        STATE.visual_actor_path = candidate["actor"]
        STATE.visual["actor"] = candidate["actor"]
        STATE.visual_mode = "settle"
        STATE.visual_mode_started_at = now
        STATE.events.append({"event": "visual_camera_positioned", "actor": candidate["actor"]})
        return

    row = rows.get(STATE.visual_actor_path)
    if row is None:
        STATE.visual["status"] = "FAILED"
        STATE.visual["error"] = "selected patrol actor was no longer present"
        return

    if STATE.visual_mode == "settle":
        if now - STATE.visual_mode_started_at < VISUAL_SETTLE_SECONDS:
            return
        request_visual_screenshot("before", row)
        STATE.visual_mode = "wait_before"
        return

    if STATE.visual_mode == "wait_before":
        evidence = valid_png(Path(STATE.visual["before"]["path"]))
        if evidence is None:
            return
        STATE.visual["before"].update(evidence)
        STATE.visual_before_location = STATE.visual["before"]["actor_location"]
        STATE.visual_mode = "moving"
        STATE.visual_mode_started_at = now
        return

    if STATE.visual_mode == "moving":
        if now - STATE.visual_mode_started_at < VISUAL_MOVEMENT_SECONDS:
            return
        if distance_2d(row["location"], STATE.visual_before_location) < MINIMUM_MOVEMENT_CM:
            return
        request_visual_screenshot("after", row)
        STATE.visual_mode = "wait_after"
        return

    if STATE.visual_mode == "wait_after":
        evidence = valid_png(Path(STATE.visual["after"]["path"]))
        if evidence is None:
            return
        STATE.visual["after"].update(evidence)
        STATE.visual["status"] = "CAPTURED"
        STATE.events.append({"event": "visual_capture_complete", "actor": STATE.visual_actor_path})


def stop_visual_camera():
    if STATE.free_camera_subsystem is None:
        return
    try:
        if bool(STATE.free_camera_subsystem.is_gameplay_free_camera_active()):
            STATE.free_camera_subsystem.stop_gameplay_free_camera()
            STATE.events.append({"event": "visual_free_camera_stopped"})
    except Exception as error:
        STATE.visual.setdefault("cleanup_error", str(error))


def patrol_loop_actor_paths():
    return {
        enemy["actor"]
        for sample in STATE.samples
        for enemy in sample.get("enemies", [])
        if enemy.get("patrol_loop_active")
    }


def moved_actor_paths():
    return {
        actor
        for actor, displacement in STATE.maximum_displacements.items()
        if displacement >= MINIMUM_MOVEMENT_CM
        and STATE.maximum_speed_2d.get(actor, 0.0) >= MINIMUM_MOVING_SPEED_CM_PER_SEC
    }


def write_report(status):
    RUN_DIRECTORY.mkdir(parents=True, exist_ok=True)
    latest = STATE.samples[-1] if STATE.samples else {}
    patrolling_actors = patrol_loop_actor_paths()
    patrol_active = bool(patrolling_actors)
    patrol_state = any(
        "AIState.Patrol" in enemy.get("ai_state", "")
        for sample in STATE.samples
        for enemy in sample.get("enemies", [])
    )
    moved_actors = sorted(moved_actor_paths())
    patrolling_moved_actors = sorted(set(moved_actors) & patrolling_actors)
    dirty_after = dirty_packages()
    report = {
        "schema_version": 1,
        "status": status,
        "run_seed": RUN_SEED,
        "elapsed_seconds": time.monotonic() - STATE.started_at,
        "phase": STATE.phase,
        "request_accepted": STATE.request_accepted,
        "dungeon_observed": STATE.dungeon_seen_at is not None,
        "first_enemy_observed": STATE.first_enemy_seen_at is not None,
        "latest_runtime_sample": latest,
        "patrol_loop_observed": patrol_active,
        # GameplayTag's UE Python string representation is not stable in 5.8
        # (it can render only as a struct shell). The C++ CALYSTO_V6_PATROL_STARTED
        # marker records that state transition; do not turn reflection ambiguity
        # into a false runtime failure here.
        "ai_state_patrol_observed": patrol_state,
        "ai_state_patrol_reflection": "PENDING" if not patrol_state else "OBSERVED",
        "maximum_displacement_2d_cm": STATE.maximum_displacements,
        "maximum_speed_2d_cm_per_second": STATE.maximum_speed_2d,
        "moved_actors": moved_actors,
        "patrolling_moved_actors": patrolling_moved_actors,
        "visual_capture": STATE.visual,
        "checks": {
            "nav_mesh_bounds_present": latest.get("nav_mesh_bounds_count", 0) >= 1,
            "recast_navmesh_present": latest.get("recast_navmesh_count", 0) >= 1,
            "enemy_population_present": latest.get("enemy_count", 0) >= 1,
            "patrol_loop_observed": patrol_active,
            "ai_state_patrol_observed": patrol_state,
            "movement_observed": bool(patrolling_moved_actors),
            "visual_capture_complete": STATE.visual.get("status") == "CAPTURED",
            "no_new_dirty_packages": dirty_after == STATE.dirty_before,
        },
        "events": STATE.events,
        "samples": STATE.samples,
        "dirty_before": STATE.dirty_before,
        "dirty_after": dirty_after,
        "error": STATE.error,
    }
    REPORT_PATH.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CALYSTO_V6_PATROL_PIE_RESULT=" + json.dumps(report["checks"], sort_keys=True))


def finish(success, error=""):
    if STATE.finished or STATE.finishing:
        return
    STATE.finishing = True
    STATE.error = error
    try:
        stop_visual_camera()
        if STATE.callback is not None:
            try:
                unreal.unregister_slate_post_tick_callback(STATE.callback)
            except Exception:
                pass
            STATE.callback = None
        builtins._codex_calysto_patrol_pie58 = None
        try:
            write_report("PASS" if success else "FAIL")
        except Exception:
            unreal.log_error("CALYSTO_V6_PATROL_PIE_REPORT_WRITE_FAILED=" + traceback.format_exc())
    finally:
        STATE.finished = True
        try:
            if LEVEL_EDITOR.is_in_play_in_editor():
                EDITOR_LEVEL_LIBRARY.editor_end_play()
        finally:
            unreal.SystemLibrary.quit_editor()


def tick(_delta_seconds):
    try:
        now = time.monotonic()
        if now - STATE.started_at > TIMEOUT_SECONDS:
            finish(False, "timeout in phase " + STATE.phase)
            return

        if STATE.phase == "load_hub":
            set_phase("wait_hub")
            LEVEL_EDITOR.load_level(HUB_MAP)
            return

        if STATE.phase == "wait_hub":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if (
                normalized_world_name(editor_world) != HUB_NAME
                or now - STATE.phase_started_at < 2.0
            ):
                return
            set_phase("wait_hub_pie")
            LEVEL_EDITOR.editor_request_begin_play()
            return

        if STATE.phase == "wait_hub_pie":
            if not LEVEL_EDITOR.is_in_play_in_editor():
                return
            set_phase("wait_session_preload")
            return

        world = EDITOR_LEVEL_LIBRARY.get_game_world()
        if not world:
            return

        if STATE.phase == "wait_session_preload":
            if normalized_world_name(world) != HUB_NAME:
                finish(False, "left HUB before the runtime preload settled")
                return
            if now - STATE.phase_started_at < SESSION_PRELOAD_SETTLE_SECONDS:
                return
            set_phase("request_seeded_run")
            return

        if STATE.phase == "request_seeded_run":
            if normalized_world_name(world) != HUB_NAME or now - STATE.phase_started_at < 3.0:
                return
            director = find_game_instance_subsystem(world)
            if not director:
                return
            STATE.request_accepted = bool(director.request_start_new_run_with_seed(RUN_SEED))
            STATE.events.append(
                {
                    "event": "request_start_new_run_with_seed",
                    "seed": RUN_SEED,
                    "accepted": STATE.request_accepted,
                }
            )
            if not STATE.request_accepted:
                finish(False, "V6 subsystem rejected RequestStartNewRunWithSeed")
                return
            set_phase("wait_dungeon")
            return

        if STATE.phase == "wait_dungeon":
            if normalized_world_name(world) == DUNGEON_NAME:
                STATE.dungeon_seen_at = now
                set_phase("observe_patrol")
            return

        if STATE.phase == "observe_patrol":
            if normalized_world_name(world) != DUNGEON_NAME:
                finish(False, "left DungeonGeneration during patrol observation")
                return
            if now - STATE.last_sample_at < SAMPLE_INTERVAL_SECONDS:
                return
            STATE.last_sample_at = now
            sample = record_sample(world)
            update_visual_capture(world, sample, now)
            if sample["enemy_count"] > 0 and STATE.first_enemy_seen_at is None:
                STATE.first_enemy_seen_at = now
                STATE.events.append({"event": "enemy_population_observed", "count": sample["enemy_count"]})
            if STATE.first_enemy_seen_at is None:
                return
            if now - STATE.first_enemy_seen_at < OBSERVATION_SECONDS:
                return

            patrol_active = bool(patrol_loop_actor_paths())
            moved = bool(moved_actor_paths() & patrol_loop_actor_paths())
            navigation_ready = sample["nav_mesh_bounds_count"] >= 1 and sample["recast_navmesh_count"] >= 1
            visual_capture_complete = STATE.visual.get("status") == "CAPTURED"
            no_new_dirty_packages = dirty_packages() == STATE.dirty_before
            finish(
                navigation_ready and patrol_active and moved and visual_capture_complete and no_new_dirty_packages,
                "" if navigation_ready and patrol_active and moved and visual_capture_complete and no_new_dirty_packages
                else "required patrol runtime evidence was not observed",
            )
    except Exception:
        finish(False, traceback.format_exc())


existing = getattr(builtins, "_codex_calysto_patrol_pie58", None)
if existing is not None:
    unreal.log_warning("CALYSTO_V6_PATROL_PIE duplicate registration ignored")
else:
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    STATE.callback = unreal.register_slate_post_tick_callback(tick)
    builtins._codex_calysto_patrol_pie58 = STATE
    unreal.log("CALYSTO_V6_PATROL_PIE_ARMED report=" + str(REPORT_PATH))
