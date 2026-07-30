"""Validate DoorToLevel through the real ACF interaction path in UE 5.8 PIE.

This script is intended for a dedicated UnrealEditor process launched with
``-ExecutePythonScript``. It writes evidence under ``Saved/`` only; it never
saves or modifies an Unreal asset.
"""

import builtins
import datetime
import json
import os
import re
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
OUTPUT_FILE = Path(
    os.environ.get(
        "CODEX_DOOR_TO_LEVEL_INTERACTION_PIE_OUTPUT",
        PROJECT_DIR
        / "Saved"
        / "Migration"
        / "CalystoProcedural"
        / "DoorToLevelInteractionPIE58.json",
    )
)

HUB_MAP = "/Game/_Game/Hub/HUB"
HUB_MAP_NAME = "hub"
HUB_WORLD_PATH = "/Game/_Game/Hub/HUB.HUB"
DESTINATION_MAP = "/Game/Procedural/Maps/DungeonGeneration"
DESTINATION_MAP_NAME = "dungeongeneration"
DESTINATION_WORLD_PATH = (
    "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
)
DOOR_CLASS_PATH = "/Game/Procedural/DoorToLevel.DoorToLevel_C"
INTERACTION_COMPONENT_CLASS_PATH = (
    "/Script/AscentCombatFramework.ACFInteractionComponent"
)

TIMEOUT_SECONDS = 120.0
CONTEXT_TIMEOUT_SECONDS = 25.0
REGISTRATION_TIMEOUT_SECONDS = 15.0
TRAVEL_TIMEOUT_SECONDS = 40.0
REQUIRED_SELECTION_SAMPLES = 3
REQUIRED_DESTINATION_SAMPLES = 3
DETECTION_REFRESH_INTERVAL_SECONDS = 0.25

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def object_path(value):
    """Return a stable string without retaining a UObject in JSON evidence."""
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return "<invalid UObject>"


def vector(value):
    return {
        "x": float(value.x),
        "y": float(value.y),
        "z": float(value.z),
    }


def normalized_world_name(world):
    if not world:
        return ""
    name = world.get_name()
    if name.lower().startswith("uedpie_"):
        parts = name.split("_", 2)
        if len(parts) == 3:
            name = parts[2]
    return name.lower()


def canonical_world_path(world):
    """Strip the PIE package prefix while preserving the actual map path."""
    return re.sub(
        r"uedpie_\d+_",
        "",
        object_path(world),
        flags=re.IGNORECASE,
    )


def world_time_seconds(world):
    """Use the reflected Blueprint API; UWorld.GetTimeSeconds is not a UFUNCTION."""
    return float(unreal.GameplayStatics.get_time_seconds(world)) if world else 0.0


def game_world():
    return EDITOR_LEVEL_LIBRARY.get_game_world()


def actors_of_class(world, actor_class):
    if not world or not actor_class:
        return []
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def components_of_class(actor, component_class):
    if not actor or not component_class:
        return []
    return list(actor.get_components_by_class(component_class))


def reflected_method(owner, method_name):
    method = getattr(owner, method_name, None)
    if not callable(method):
        raise RuntimeError(
            "Missing reflected method {} on {}".format(method_name, object_path(owner))
        )
    return method


def overlapping_actor_paths(component):
    """Best-effort detector diagnostics; failure is not itself a test failure."""
    try:
        return sorted(object_path(actor) for actor in component.get_overlapping_actors())
    except Exception:
        return []


class RuntimeState:
    def __init__(self):
        self.phase = "load_hub"
        self.started_at = time.monotonic()
        self.phase_started_at = self.started_at
        self.callback = None
        self.finished = False
        self.error = ""
        self.door_class = None
        self.interaction_component_class = None
        self.player = None
        self.door = None
        self.interaction_component = None
        self.selection_samples = 0
        self.destination_samples = 0
        self.refresh_count = 0
        self.last_refresh_at = 0.0
        self.last_best_path = ""
        self.phase_history = []
        self.checks = {
            "hub_loaded": False,
            "pie_started": False,
            "exactly_one_runtime_door": False,
            "player_pawn_found": False,
            "acf_interaction_component_found": False,
            "player_moved_near_door": False,
            "detection_enabled_and_refreshed": False,
            "door_overlaps_acf_detector": False,
            "door_selected_as_best_interactable": False,
            "interaction_invoked": False,
            "destination_loaded": False,
            "door_absent_from_destination": False,
        }
        self.hub_runtime = {}
        self.registration = {}
        self.travel = {}


STATE = RuntimeState()


def phase_age():
    return time.monotonic() - STATE.phase_started_at


def set_phase(value):
    STATE.phase = value
    STATE.phase_started_at = time.monotonic()
    STATE.phase_history.append(
        {
            "phase": value,
            "elapsed_seconds": round(
                STATE.phase_started_at - STATE.started_at, 3
            ),
        }
    )
    unreal.log("[DoorToLevelInteractionPIE58] phase=" + value)


def evidence_document(success):
    return {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(
            datetime.timezone.utc
        ).isoformat(),
        "status": (
            "UE58_DOOR_TO_LEVEL_INTERACTION_PIE_PASS"
            if success
            else "UE58_DOOR_TO_LEVEL_INTERACTION_PIE_FAIL"
        ),
        "success": bool(success),
        "contract": (
            "The unique map-owned DoorToLevel in HUB must be discovered by the "
            "player's UACFInteractionComponent and interacting with that selected "
            "actor must travel PIE to DungeonGeneration."
        ),
        "maps": {
            "source": HUB_MAP,
            "destination": DESTINATION_MAP,
        },
        "class_paths": {
            "door": DOOR_CLASS_PATH,
            "interaction_component": INTERACTION_COMPONENT_CLASS_PATH,
        },
        "reflected_calls": [
            "UACFInteractionComponent.EnableDetection(true)",
            "UACFInteractionComponent.RefreshInteractions()",
            "UACFInteractionComponent.GetCurrentBestInteractableActor()",
            "UACFInteractionComponent.Interact(\"\")",
        ],
        "checks": STATE.checks,
        "phase": STATE.phase,
        "phase_history": STATE.phase_history,
        "selection_stable_samples": STATE.selection_samples,
        "destination_stable_samples": STATE.destination_samples,
        "refresh_count": STATE.refresh_count,
        "hub_runtime": STATE.hub_runtime,
        "registration": STATE.registration,
        "travel": STATE.travel,
        "error": STATE.error,
        "asset_mutations": [],
        "asset_saves": [],
        "automation_reference": (
            "UBG PlayTestTools-style post-tick PIE state machine; no UBG runtime "
            "dependency is loaded or required."
        ),
    }


def write_result(success):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    document = evidence_document(success)
    OUTPUT_FILE.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    unreal.log(
        "[DoorToLevelInteractionPIE58] result="
        + json.dumps(document, sort_keys=True)
    )


def finish(success, error=""):
    if STATE.finished:
        return
    STATE.finished = True
    STATE.error = error
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_door_to_level_interaction_pie58 = None

    try:
        write_result(success)
    finally:
        try:
            if LEVEL_EDITOR.is_in_play_in_editor():
                EDITOR_LEVEL_LIBRARY.editor_end_play()
        finally:
            unreal.SystemLibrary.quit_editor()


def fail(message):
    finish(False, message)


def resolve_hub_runtime(world):
    doors = actors_of_class(world, STATE.door_class)
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    interaction_components = components_of_class(
        player, STATE.interaction_component_class
    )

    STATE.hub_runtime.update(
        {
            "world": object_path(world),
            "world_name": normalized_world_name(world),
            "canonical_world_path": canonical_world_path(world),
            "world_time_seconds": world_time_seconds(world),
            "door_count": len(doors),
            "door_paths": [object_path(door) for door in doors],
            "player_pawn": object_path(player),
            "player_class": object_path(player.get_class()) if player else "",
            "interaction_component_count": len(interaction_components),
            "interaction_component_paths": [
                object_path(component) for component in interaction_components
            ],
        }
    )

    if len(doors) != 1 or not player or len(interaction_components) != 1:
        return False

    STATE.door = doors[0]
    STATE.player = player
    STATE.interaction_component = interaction_components[0]
    STATE.checks["exactly_one_runtime_door"] = True
    STATE.checks["player_pawn_found"] = True
    STATE.checks["acf_interaction_component_found"] = True
    return True


def place_player_and_enable_detection():
    door_location = STATE.door.get_actor_location()
    original_location = STATE.player.get_actor_location()
    # Stay well inside the combined 100 cm Door sphere and 180 cm ACF detector,
    # while avoiding the visible mesh centered at relative Y=60 as much as possible.
    requested_location = unreal.Vector(
        door_location.x - 80.0,
        door_location.y - 80.0,
        door_location.z,
    )
    move_returned = bool(
        STATE.player.set_actor_location(requested_location, False, True)
    )
    actual_location = STATE.player.get_actor_location()
    distance_to_door = float(STATE.player.get_distance_to(STATE.door))

    STATE.hub_runtime.update(
        {
            "door_path": object_path(STATE.door),
            "door_class": object_path(STATE.door.get_class()),
            "door_location": vector(door_location),
            "player_original_location": vector(original_location),
            "player_requested_location": vector(requested_location),
            "player_actual_location": vector(actual_location),
            "set_actor_location_returned": move_returned,
            "distance_to_door_cm": distance_to_door,
            "interaction_component": object_path(STATE.interaction_component),
            "interaction_component_class": object_path(
                STATE.interaction_component.get_class()
            ),
        }
    )

    # The actor move return can be false when the engine reports no translation;
    # the measured runtime distance is the authoritative gate.
    if distance_to_door > 200.0:
        raise RuntimeError(
            "Player did not reach DoorToLevel interaction range; distance={:.3f} cm".format(
                distance_to_door
            )
        )
    STATE.checks["player_moved_near_door"] = True

    # Clear any stale nearby actor first, then force ACF to rebuild overlap state.
    reflected_method(STATE.interaction_component, "enable_detection")(False)
    reflected_method(STATE.interaction_component, "enable_detection")(True)
    reflected_method(STATE.interaction_component, "refresh_interactions")()
    STATE.refresh_count += 1
    STATE.last_refresh_at = time.monotonic()
    STATE.checks["detection_enabled_and_refreshed"] = True

    STATE.registration.update(
        {
            "detector_collision_enabled": str(
                STATE.interaction_component.get_collision_enabled()
            ),
            "detector_radius_cm": float(
                STATE.interaction_component.get_unscaled_sphere_radius()
            ),
            "initial_overlapping_actor_paths": overlapping_actor_paths(
                STATE.interaction_component
            ),
        }
    )


def refresh_and_sample_selection():
    now = time.monotonic()
    if now - STATE.last_refresh_at >= DETECTION_REFRESH_INTERVAL_SECONDS:
        reflected_method(STATE.interaction_component, "refresh_interactions")()
        STATE.refresh_count += 1
        STATE.last_refresh_at = now

    best = reflected_method(
        STATE.interaction_component, "get_current_best_interactable_actor"
    )()
    best_path = object_path(best)
    door_path = object_path(STATE.door)
    overlap_paths = overlapping_actor_paths(STATE.interaction_component)
    STATE.last_best_path = best_path
    STATE.registration.update(
        {
            "last_best_interactable_actor": best_path,
            "expected_door_actor": door_path,
            "last_overlapping_actor_paths": overlap_paths,
        }
    )

    door_overlaps_detector = bool(door_path and door_path in overlap_paths)
    STATE.checks["door_overlaps_acf_detector"] = door_overlaps_detector
    if best_path and best_path == door_path and door_overlaps_detector:
        STATE.selection_samples += 1
    else:
        STATE.selection_samples = 0


def invoke_interaction():
    best = reflected_method(
        STATE.interaction_component, "get_current_best_interactable_actor"
    )()
    best_path = object_path(best)
    door_path = object_path(STATE.door)
    if not best_path or best_path != door_path:
        raise RuntimeError(
            "DoorToLevel lost ACF selection before Interact: best={!r}, door={!r}".format(
                best_path, door_path
            )
        )

    STATE.checks["door_selected_as_best_interactable"] = True
    STATE.registration.update(
        {
            "selected_actor_before_interact": best_path,
            "selected_actor_matches_door": True,
        }
    )
    component_path = object_path(STATE.interaction_component)
    world_before_interact = normalized_world_name(game_world())
    STATE.travel.update(
        {
            "interaction_type": "",
            "invoked_from_component": component_path,
            "selected_actor": best_path,
            "world_before_interact": world_before_interact,
        }
    )
    # OpenLevel may invalidate HUB runtime objects before this reflected call
    # returns, so no HUB UObject is dereferenced after it.
    reflected_method(STATE.interaction_component, "interact")('')
    STATE.checks["interaction_invoked"] = True
    # Do not keep actors/components from the old PIE world alive across travel.
    STATE.interaction_component = None
    STATE.player = None
    STATE.door = None


def sample_destination(world):
    world_name = normalized_world_name(world)
    world_path = canonical_world_path(world)
    current_world_time = world_time_seconds(world)
    destination_doors = actors_of_class(world, STATE.door_class)
    STATE.travel.update(
        {
            "last_observed_world": object_path(world),
            "last_observed_world_name": world_name,
            "last_observed_canonical_world_path": world_path,
            "last_observed_world_time_seconds": current_world_time,
            "destination_door_count": len(destination_doors),
            "destination_door_paths": [
                object_path(door) for door in destination_doors
            ],
            "destination_player_pawn": object_path(
                unreal.GameplayStatics.get_player_pawn(world, 0)
            ),
        }
    )

    if (
        world_name == DESTINATION_MAP_NAME
        and world_path.lower() == DESTINATION_WORLD_PATH.lower()
        and current_world_time >= 1.0
        and len(destination_doors) == 0
    ):
        STATE.destination_samples += 1
    else:
        STATE.destination_samples = 0


def tick(_delta_seconds):
    try:
        if STATE.finished:
            return
        if time.monotonic() - STATE.started_at > TIMEOUT_SECONDS:
            fail("Global timeout in phase {}".format(STATE.phase))
            return

        if STATE.phase == "load_hub":
            STATE.door_class = unreal.load_class(None, DOOR_CLASS_PATH)
            STATE.interaction_component_class = unreal.load_class(
                None, INTERACTION_COMPONENT_CLASS_PATH
            )
            if not STATE.door_class or not STATE.interaction_component_class:
                fail("DoorToLevel or ACFInteractionComponent class failed to load")
                return
            # Level loading can pump editor work; guard against callback reentry.
            set_phase("loading_hub_in_progress")
            loaded = LEVEL_EDITOR.load_level(HUB_MAP)
            if loaded is False:
                fail("LevelEditorSubsystem failed to load HUB")
                return
            set_phase("wait_editor_hub")
            return

        if STATE.phase == "wait_editor_hub":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if (
                normalized_world_name(editor_world) != HUB_MAP_NAME
                or canonical_world_path(editor_world).lower()
                != HUB_WORLD_PATH.lower()
            ):
                if phase_age() > CONTEXT_TIMEOUT_SECONDS:
                    fail("HUB editor world did not load")
                return
            if phase_age() < 1.0:
                return
            STATE.checks["hub_loaded"] = True
            set_phase("begin_play_in_progress")
            LEVEL_EDITOR.editor_request_begin_play()
            set_phase("wait_hub_pie")
            return

        if STATE.phase == "wait_hub_pie":
            world = game_world()
            if (
                not LEVEL_EDITOR.is_in_play_in_editor()
                or not world
                or normalized_world_name(world) != HUB_MAP_NAME
                or canonical_world_path(world).lower() != HUB_WORLD_PATH.lower()
            ):
                if phase_age() > CONTEXT_TIMEOUT_SECONDS:
                    fail("HUB PIE world did not start")
                return
            STATE.checks["pie_started"] = True
            if world_time_seconds(world) < 1.5:
                return
            set_phase("resolve_hub_runtime")
            return

        if STATE.phase == "resolve_hub_runtime":
            world = game_world()
            if (
                LEVEL_EDITOR.is_in_play_in_editor()
                and world
                and normalized_world_name(world) == HUB_MAP_NAME
                and canonical_world_path(world).lower() == HUB_WORLD_PATH.lower()
                and resolve_hub_runtime(world)
            ):
                set_phase("setup_interaction_in_progress")
                place_player_and_enable_detection()
                set_phase("wait_acf_selection")
                return
            if phase_age() > CONTEXT_TIMEOUT_SECONDS:
                fail(
                    "Could not resolve exactly one DoorToLevel, one player pawn, "
                    "and one UACFInteractionComponent in HUB PIE"
                )
            return

        if STATE.phase == "wait_acf_selection":
            world = game_world()
            if (
                not LEVEL_EDITOR.is_in_play_in_editor()
                or not world
                or normalized_world_name(world) != HUB_MAP_NAME
                or canonical_world_path(world).lower() != HUB_WORLD_PATH.lower()
            ):
                fail("HUB PIE world changed or stopped before ACF interaction")
                return
            refresh_and_sample_selection()
            if STATE.selection_samples >= REQUIRED_SELECTION_SAMPLES:
                # Interact can synchronously request OpenLevel and pump editor work.
                set_phase("interaction_in_progress")
                invoke_interaction()
                set_phase("wait_destination")
                return
            if phase_age() > REGISTRATION_TIMEOUT_SECONDS:
                fail(
                    "ACF did not select DoorToLevel; last best actor={!r}".format(
                        STATE.last_best_path
                    )
                )
            return

        if STATE.phase == "wait_destination":
            world = game_world()
            if world:
                sample_destination(world)
                if STATE.destination_samples >= REQUIRED_DESTINATION_SAMPLES:
                    STATE.checks["destination_loaded"] = True
                    STATE.checks["door_absent_from_destination"] = True
                    STATE.travel["destination_verified"] = DESTINATION_MAP
                    set_phase("complete")
                    finish(True)
                    return
            if phase_age() > TRAVEL_TIMEOUT_SECONDS:
                fail(
                    "Interact was invoked but PIE did not reach DungeonGeneration"
                )
            return
    except Exception as exc:
        finish(False, "{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_door_to_level_interaction_pie58", None)
if existing is not None:
    try:
        old_callback = getattr(existing, "callback", None)
        if old_callback is not None:
            unreal.unregister_slate_post_tick_callback(old_callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.phase_history.append({"phase": STATE.phase, "elapsed_seconds": 0.0})
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_door_to_level_interaction_pie58 = STATE
unreal.log("[DoorToLevelInteractionPIE58] registered")
