"""Arm one bounded, real-door PIE traversal of floors 1, 2 and 3 in one run.

Run in the target editor console with PIE stopped and HUB already open:
    import builtins
    builtins.CALYSTO_DIRECTOR_TRAVERSAL_OPTIONS = {
        "output_dir": r"D:/Projects UE5/NoShellForWinter/Saved/Migration/CalystoDungeonDirectorV7/Traversal_unique",
        "mode": "native_parity",
    }
    exec(open(r"D:/Projects UE5/NoShellForWinter/Tools/Migration/Validate-CalystoDirectorTraversal58.py", encoding="utf-8").read())
Then start PIE once through the discovered native MCP tool. This script does not
start PIE, load/save maps, close the editor, seed/restart a run, or publish readiness.
It teleports the pawn into each real door's interaction range, confirms ACF's
selected overlapping actor, and invokes ACF Interact exactly once per transition.
Before each floor-door approach it walks the protected route using normal
character input and one independent QA navigation query per accepted floor.
The short entry check has five seconds; each complete walk has 35 seconds.

Native parity requires the editor's explicit -CalystoDirectorNativeParity flag.
Adding -CalystoDirectorNativeParityArchitecture requires nonempty reserved and
verified native architecture on every floor; an empty accepted floor cannot pass that gate.
Full mode uses the same flow and additionally requires GameplayVerified. Both
require the read-only Director get_diagnostics_json contract documented below.
The final explicit Return to HUB is a cleanup/travel check, not a floor-cap test.
Process exit, complete log audit, screenshot visual review, protected hashes,
probability tests, full gameplay and release acceptance remain separate gates.
"""

from __future__ import annotations

import builtins
import datetime
import hashlib
import json
import math
import re
import time
import traceback
from pathlib import Path
from typing import Any

import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
EXPECTED_ROOT = Path(r"D:/Projects UE5/NoShellForWinter").resolve()
EVIDENCE_ROOT = EXPECTED_ROOT / "Saved/Migration/CalystoDungeonDirectorV7"
HUB_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDirectorSubsystem"
ENTRANCE_CLASS = "/Game/Procedural/DoorToLevel.DoorToLevel_C"
FLOOR_DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
INTERACTION_CLASS = "/Script/AscentCombatFramework.ACFInteractionComponent"
TOTAL_DEADLINE_SECONDS = 150.0
PIE_START_SECONDS = 30.0
INTERACTION_SECONDS = 8.0
REQUEST_OBSERVATION_SECONDS = 35.0
OWNER_REQUEST_SECONDS = 30.0
SCREENSHOT_SECONDS = 5.0
RETURN_HUB_SECONDS = 10.0
PIE_STOP_SECONDS = 5.0
POLL_SECONDS = 0.1
READY_HOLD_SECONDS = 0.5
ENTRY_WALK_SECONDS = 5.0
ROUTE_WALK_SECONDS = 35.0
SELECTION_SAMPLES = 3
ENTRY_TOLERANCE_CM = 35.0
MAXIMUM_ATTEMPTS = 4
MAXIMUM_SCREENSHOT_BYTES = 16 * 1024 * 1024
EXPECTED_FLOORS = [1, 2, 3]
GENERATOR_CLASS = "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
GEOMETRY_CAPTURE_SECONDS = 0.5
GEOMETRY_MAX_COMPONENTS = 512
GEOMETRY_MAX_INSTANCES = 10000
GEOMETRY_MAX_DOOR_SWEEPS = 12
WALL_DOOR_MESH = "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_WallDoor.SM_WallDoor"
OPTIONS = dict(getattr(builtins, "CALYSTO_DIRECTOR_TRAVERSAL_OPTIONS", {}))
LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def path_of(value: Any) -> str:
    return str(value.get_path_name()) if value is not None else ""


def canonical_world(world: Any) -> str:
    return re.sub(r"UEDPIE_\d+_", "", path_of(world), flags=re.IGNORECASE)


def prop(owner: Any, name: str) -> Any:
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)).lower()
    candidates = [name, snake]
    if name.startswith("b") and len(name) > 1 and name[1].isupper():
        candidates.append(snake.removeprefix("b_"))
    for candidate in candidates:
        try:
            return owner.get_editor_property(candidate)
        except Exception:
            try:
                return getattr(owner, candidate)
            except Exception:
                pass
    raise RuntimeError(f"Required reflected field {name} is unavailable")


def method(owner: Any, name: str, *args: Any) -> Any:
    target = getattr(owner, name, None)
    if not callable(target):
        raise RuntimeError(f"Required reflected method {name} is unavailable")
    return target(*args)


def enum_name(value: Any) -> str:
    return re.sub(r"[^a-z]", "", str(getattr(value, "name", value)).split(".")[-1].lower())


def dirty_packages() -> list[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({path_of(p) for p in values})


def snapshot_dict(snapshot: Any) -> dict[str, Any]:
    result = {key: int(prop(snapshot, field)) for key, field in (
        ("run_seed", "RunSeed"), ("run_epoch", "RunEpoch"), ("floor_number", "FloorNumber"),
        ("last_committed_floor_number", "LastCommittedFloorNumber"),
        ("last_committed_run_epoch", "LastCommittedRunEpoch"), ("reroll_index", "RerollIndex"),
        ("attempt_count", "AttemptCount"), ("topology_seed", "TopologySeed"))}
    result.update(state=enum_name(prop(snapshot, "State")),
                  style_id=str(method(prop(snapshot, "StyleId"), "to_string")).replace("-", "").lower(),
                  failure_message=str(prop(snapshot, "FailureMessage")),
                  request_elapsed_seconds=float(prop(snapshot, "RequestElapsedSeconds")),
                  native_floor_verified=bool(prop(snapshot, "bNativeFloorVerified")),
                  gameplay_verified=bool(prop(snapshot, "bGameplayVerified")))
    return result


def validate_floor_evidence(snapshot: dict[str, Any], diagnostic: dict[str, Any], expected_floor: int,
                            mode: str, player_location: list[float]) -> None:
    """Pure fail-closed receipt checks; no Unreal operation and no invented default evidence."""
    if snapshot["state"] != "ready" or snapshot["floor_number"] != expected_floor:
        raise ValueError("The requested floor has not reached Ready")
    if snapshot["last_committed_floor_number"] != expected_floor or snapshot["last_committed_run_epoch"] != snapshot["run_epoch"]:
        raise ValueError("Requested and committed floor/run identities differ")
    if snapshot["reroll_index"] != 0 or not snapshot["native_floor_verified"]:
        raise ValueError("A replay/reroll or unverified native floor cannot count toward the trilogy")
    elapsed = snapshot["request_elapsed_seconds"]
    if not math.isfinite(elapsed) or not 0 <= elapsed <= OWNER_REQUEST_SECONDS:
        raise ValueError("Accepted floor exceeded the shared 30-second request deadline")
    if mode == "full" and not snapshot["gameplay_verified"]:
        raise ValueError("Full gameplay verification is missing")
    if mode == "native_parity" and snapshot["gameplay_verified"]:
        raise ValueError("The native fixture incorrectly reports full gameplay verification")
    if type(diagnostic.get("schema_version")) is not int or diagnostic["schema_version"] != 1:
        raise ValueError("Required read-only diagnostic schema 1 is unavailable")
    for key in ("run_seed", "run_epoch", "floor_number"):
        if type(diagnostic.get(key)) is not int or diagnostic[key] != snapshot[key]:
            raise ValueError(f"Diagnostic {key} does not belong to this accepted floor")
    guid_pattern = r"(?:[0-9A-Fa-f]{32}|[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})"
    for key in ("request_id", "attempt_id"):
        if not isinstance(diagnostic.get(key), str) or not re.fullmatch(guid_pattern, diagnostic[key]) or not any(c in "123456789abcdefABCDEF" for c in diagnostic[key]):
            raise ValueError(f"Diagnostic {key} is not a nonzero routing identity")
    for key in ("unique_owned_start_and_end", "blocking_floor_and_capsule_clearance",
                "complete_relevant_navigation_route", "room_theme_contract_verified", "materials_verified"):
        if diagnostic.get(key) is not True:
            raise ValueError(f"Required native postcondition {key} was not verified")
    attempts = diagnostic.get("attempts")
    if not isinstance(attempts, list) or not 1 <= len(attempts) <= MAXIMUM_ATTEMPTS or len(attempts) != snapshot["attempt_count"]:
        raise ValueError("Attempt inventory is missing or differs from the accepted snapshot")
    seen = set()
    for index, attempt in enumerate(attempts):
        identity = attempt.get("attempt_id")
        if not isinstance(identity, str) or identity in seen or not re.fullmatch(guid_pattern, identity) or not any(c in "123456789abcdefABCDEF" for c in identity):
            raise ValueError("Attempt identities must be present and unique")
        seen.add(identity)
        if type(attempt.get("root_generation_requests")) is not int or attempt["root_generation_requests"] != 1:
            raise ValueError("Every generation attempt must issue exactly one root request")
        if type(attempt.get("actual_root_generation_requests")) is not int or attempt["actual_root_generation_requests"] != 1:
            raise ValueError("Every generation attempt must prove exactly one actual native GenerateLocal call")
        accepted = index == len(attempts) - 1
        if attempt.get("accepted") is not accepted or (not accepted and attempt.get("cleanup_verified") is not True):
            raise ValueError("Rejected attempt cleanup or the sole accepted attempt is unverified")
    if attempts[-1]["attempt_id"] != diagnostic["attempt_id"] or attempts[-1].get("topology_seed") != snapshot["topology_seed"]:
        raise ValueError("The accepted attempt/seed does not match the active routing token")
    entry = diagnostic.get("entry_location")
    if not isinstance(entry, list) or len(entry) != 3 or not all(type(v) in (int, float) and math.isfinite(v) for v in entry):
        raise ValueError("A finite published entry location is required")
    if len(player_location) != 3 or not all(math.isfinite(v) for v in player_location):
        raise ValueError("Player location is invalid")
    if math.dist(entry, player_location) > ENTRY_TOLERANCE_CM:
        raise ValueError("The released player is not at the Director's published entry")


def validate_entry_walk_route(points: list[list[float]], start: list[float], radius: float) -> None:
    """Validate a complete QA route's finite points without projecting or searching entries."""
    if not math.isfinite(radius) or radius <= 0 or not 2 <= len(points) <= 4096:
        raise ValueError("Entry walk requires a positive capsule radius and a bounded route")
    if any(len(p) != 3 or not all(math.isfinite(v) for v in p) for p in [start, *points]):
        raise ValueError("Entry walk route contains invalid locations")
    if math.dist(points[0][:2], start[:2]) > ENTRY_TOLERANCE_CM:
        raise ValueError("The QA route does not begin at the sole validated entry")
    if sum(math.dist(a[:2], b[:2]) for a, b in zip(points, points[1:])) < 2 * radius:
        raise ValueError("The complete QA route is shorter than one capsule diameter")


def entry_walk_satisfied(start: list[float], end: list[float], radius: float,
                         grounded_seconds: float, input_calls: int) -> bool:
    """A short entry walk is measurable progress, never proof of walking the full route."""
    values = [*start, *end, radius, grounded_seconds]
    if len(start) != 3 or len(end) != 3 or not all(math.isfinite(v) for v in values) or radius <= 0:
        raise ValueError("Entry walk observations are not finite")
    return input_calls > 0 and grounded_seconds >= 0.25 and math.dist(start[:2], end[:2]) >= 1.5 * radius


class Traversal:
    def __init__(self) -> None:
        if PROJECT_ROOT != EXPECTED_ROOT:
            raise RuntimeError("Traversal must run exclusively in the writable target project")
        self.mode = str(OPTIONS.get("mode", ""))
        if self.mode not in {"native_parity", "full"}:
            raise RuntimeError("Explicit mode native_parity or full is required")
        self.output_dir = Path(str(OPTIONS.get("output_dir", ""))).resolve()
        if not self.output_dir.is_relative_to(EVIDENCE_ROOT) or self.output_dir == EVIDENCE_ROOT:
            raise RuntimeError("Output must be a unique child directory of the target V7 evidence root")
        if (self.output_dir / "traversal.json").exists():
            raise RuntimeError("A prior traversal receipt already occupies this output directory")
        if LEVEL_EDITOR.is_in_play_in_editor():
            raise RuntimeError("Arm before PIE starts; this harness will not take over existing PIE work")
        if canonical_world(UNREAL_EDITOR.get_editor_world()).lower() != HUB_WORLD.lower():
            raise RuntimeError("HUB must already be open; this harness never switches an editor map")
        parity_flag = bool(re.search(r"(?:^|\s)-CalystoDirectorNativeParity(?:\s|$)", method(unreal.SystemLibrary, "get_command_line"), re.I))
        self.require_architecture = bool(re.search(r"(?:^|\s)-CalystoDirectorNativeParityArchitecture(?:\s|$)",
                                                   method(unreal.SystemLibrary, "get_command_line"), re.I))
        if parity_flag != (self.mode == "native_parity"):
            raise RuntimeError("Explicit native parity command-line mode and harness mode disagree")
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.started = time.monotonic()
        self.phase_started = self.started
        self.next_poll = self.started
        self.phase = "waiting_for_external_pie"
        self.callback = None
        self.finished = False
        self.pending_success = False
        self.error = ""
        self.classes = {name: unreal.load_class(None, path) for name, path in (
            ("director", DIRECTOR_CLASS), ("entrance", ENTRANCE_CLASS),
            ("floor_door", FLOOR_DOOR_CLASS), ("interaction", INTERACTION_CLASS))}
        if not all(self.classes.values()):
            raise RuntimeError("Required new Director or native interaction class is unavailable")
        self.director = None
        self.subsystem_iterator = None
        self.iterator_count = 0
        self.initial_dirty = dirty_packages()
        self.operations = []
        self.samples = []
        self.states = []
        self.expected_floor = 1
        self.request_started = None
        self.run_seed = None
        self.run_epoch = None
        self.positioned = False
        self.selection_samples = 0
        self.saw_dungeon = False
        self.pie_observed = False
        self.explicit_hub_return_verified = False
        self.screenshot_requested = False
        self.ready_sample = None
        self.geometry_capture = None
        self.write_receipt("ARMED")

    def phase_to(self, name: str) -> None:
        self.phase = name
        self.phase_started = time.monotonic()
        self.positioned = False
        self.selection_samples = 0
        unreal.log(f"CALYSTO_DIRECTOR_TRAVERSAL phase={name} expected_floor={self.expected_floor}")

    def find_director(self, world: Any) -> Any:
        if self.director is not None:
            return self.director
        instance = unreal.GameplayStatics.get_game_instance(world)
        library = getattr(unreal, "SubsystemBlueprintLibrary", None)
        function = getattr(library, "get_game_instance_subsystem", None)
        if callable(function):
            self.director = function(world, self.classes["director"])
            if self.director:
                return self.director
        for name in ("get_subsystem", "get_game_instance_subsystem"):
            function = getattr(instance, name, None)
            if callable(function):
                self.director = function(self.classes["director"])
                if self.director:
                    return self.director
        # Legacy Python bindings may omit the library. Bound the reflection fallback
        # per tick and in total; do not scan the entire object registry in one frame.
        if self.subsystem_iterator is None:
            cls = getattr(unreal, "EFCalystoDirectorSubsystem", unreal.Object)
            self.subsystem_iterator = iter(unreal.ObjectIterator(cls))
        stop = time.monotonic() + 0.025
        for _ in range(2000):
            candidate = next(self.subsystem_iterator, None)
            if candidate is None:
                raise RuntimeError("The new Director subsystem is absent in this PIE GameInstance")
            self.iterator_count += 1
            if self.iterator_count > 500000:
                raise RuntimeError("Bounded subsystem discovery exhausted its object limit")
            if candidate.get_class() == self.classes["director"] and path_of(candidate).startswith(path_of(instance) + "."):
                self.director = candidate
                self.subsystem_iterator = None
                return candidate
            if time.monotonic() >= stop:
                break
        return None

    def interact(self, world: Any, entrance: bool) -> bool:
        actors = list(unreal.GameplayStatics.get_all_actors_of_class(world, self.classes["entrance" if entrance else "floor_door"]))
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        if not pawn:
            return False
        components = list(pawn.get_components_by_class(self.classes["interaction"]))
        if len(actors) != 1 or len(components) != 1:
            raise RuntimeError(f"Expected exactly one real door and ACF interaction component; got {len(actors)}/{len(components)}")
        door, interaction = actors[0], components[0]
        if not self.positioned:
            if entrance:
                location = door.get_actor_location()
                target = unreal.Vector(location.x - 80, location.y - 80, location.z)
            else:
                target = door.get_actor_transform().transform_location(unreal.Vector(0, -90, 0))
                capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
                if not capsule:
                    raise RuntimeError("The player capsule is required for the floor-door approach")
                target.z += float(method(capsule, "get_scaled_capsule_half_height")) + 2.0
            pawn.set_actor_location(target, False, True)
            method(interaction, "enable_detection", False)
            method(interaction, "enable_detection", True)
            self.positioned = True
        method(interaction, "refresh_interactions")
        best = method(interaction, "get_current_best_interactable_actor")
        overlaps = {path_of(actor) for actor in method(interaction, "get_overlapping_actors")}
        if path_of(best) != path_of(door) or path_of(door) not in overlaps:
            self.selection_samples = 0
            return False
        self.selection_samples += 1
        if self.selection_samples < SELECTION_SAMPLES:
            return False
        self.operations.append({"operation": "interact", "origin": "real_door_to_level" if entrance else "real_owned_floor_door",
                                "actor": path_of(door), "actor_class": path_of(door.get_class()),
                                "world": canonical_world(world), "requested_floor": self.expected_floor,
                                "selection_samples": self.selection_samples, "approach": "automated_pawn_teleport",
                                "elapsed_seconds": time.monotonic() - self.started})
        self.request_started = time.monotonic()
        method(interaction, "interact", "CalystoDirectorTraversal58")
        self.phase_to("waiting_for_floor")
        return True

    def audit_ready(self, world: Any, director: Any, snapshot: dict[str, Any]) -> None:
        diagnostic = json.loads(str(method(director, "get_diagnostics_json")))
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        if not pawn:
            raise RuntimeError("Ready floor has no released player pawn")
        p = pawn.get_actor_location()
        location = [float(p.x), float(p.y), float(p.z)]
        validate_floor_evidence(snapshot, diagnostic, self.expected_floor, self.mode, location)
        if self.require_architecture:
            architecture = diagnostic["attempts"][-1].get("native_observation", {})
            parents = architecture.get("reserved_architecture_parents")
            requested = architecture.get("reserved_architecture_meshes")
            verified = architecture.get("verified_architecture_meshes")
            if type(parents) is not int or parents < 1 or type(requested) is not int or requested < 1 or verified != requested:
                # One bounded read captures the exact failed floor, rather than
                # repeatedly generating a world to infer its support geometry.
                self.capture_geometry(world, diagnostic, f"floor_{self.expected_floor:02d}_architecture_failure_geometry.json")
                raise RuntimeError("Architecture traversal requires nonempty actual reserved/verified native meshes on every floor")
        if self.run_epoch is None:
            self.run_epoch, self.run_seed = snapshot["run_epoch"], snapshot["run_seed"]
            expected_seed = OPTIONS.get("expected_run_seed")
            if expected_seed is not None and int(expected_seed) != self.run_seed:
                raise RuntimeError("The actual entrance run seed differs from the externally preselected seed")
        if (snapshot["run_epoch"], snapshot["run_seed"]) != (self.run_epoch, self.run_seed):
            raise RuntimeError("A new run cannot substitute for consecutive floors")
        if diagnostic["request_id"] in {s["diagnostic"]["request_id"] for s in self.samples}:
            raise RuntimeError("A floor request was counted twice")
        self.ready_sample = {"snapshot": snapshot, "diagnostic": diagnostic, "world": canonical_world(world),
                             "player_location": location, "interaction_to_ready_seconds": time.monotonic() - self.request_started,
                             "origin": self.operations[-1], "floor_number": self.expected_floor}
        view = self.ready_sample["view_diagnostic"] = {"errors": [], "native_player_view": diagnostic.get("player_view")}
        camera = unreal.GameplayStatics.get_player_camera_manager(world, 0)
        for key, operation in (
                ("camera_location", lambda: list(method(camera, "get_camera_location").to_tuple())),
                ("camera_rotation", lambda: list(method(camera, "get_camera_rotation").to_tuple())),
                ("native_light_actors", lambda: [path_of(a) for a in
                    unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Light)])):
            try:
                view[key] = operation()
            except Exception as exc:
                view["errors"].append({"field": key, "error": str(exc)[:300]})
        self.screenshot_requested = False
        self.phase_to("entry_walk")
        self.begin_entry_walk(world, pawn)

    def begin_entry_walk(self, world: Any, pawn: Any) -> None:
        if not isinstance(pawn, unreal.Character):
            raise RuntimeError("Entry walk requires the actual released ACharacter")
        self.walk_pawn = pawn
        self.walk_movement = prop(pawn, "CharacterMovement")
        capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
        radius = float(method(capsule, "get_scaled_capsule_radius"))
        half_height = float(method(capsule, "get_scaled_capsule_half_height"))
        start = self.ready_sample["player_location"]
        walk = self.ready_sample["entry_walk"] = {
            "status": "PENDING", "scope": "grounded_protected_route_walk", "full_route_walk": "PENDING",
            "start_location": start, "end_location": start, "capsule_radius_cm": radius,
            "target_distance_cm": 2 * radius, "minimum_progress_cm": 1.5 * radius,
            "deadline_seconds": ROUTE_WALK_SECONDS, "short_entry_deadline_seconds": ENTRY_WALK_SECONDS,
            "short_entry_verified": False, "grounded_seconds": 0.0, "distance_cm": 0.0,
            "input_calls": 0, "navigation_query_count": 0, "observations": []}
        doors = list(unreal.GameplayStatics.get_all_actors_of_class(world, self.classes["floor_door"]))
        if len(doors) != 1:
            raise RuntimeError("Entry walk requires the sole accepted floor door")
        goal = doors[0].get_actor_transform().transform_location(unreal.Vector(0, -90, 0))
        feet = unreal.Vector(start[0], start[1], start[2] - half_height)
        query_started = time.monotonic()
        walk["navigation_query_count"] = 1
        # Invoke on the world's real system: Python's class-static dispatch uses
        # the Within=World CDO, whose ProcessEvent GetWorld check raises an ensure.
        # World.NavigationSystem is not exposed by Python. Inspect only actual
        # loaded systems and require the exact world outer, excluding all CDOs.
        systems = []
        for index, candidate in enumerate(unreal.ObjectIterator(unreal.NavigationSystemV1)):
            if index >= 64:
                raise RuntimeError("Navigation-system inspection exceeded its finite bound")
            if candidate.get_outer() == world:
                systems.append(candidate)
        if len(systems) != 1:
            raise RuntimeError("The accepted floor has no unique actual navigation system")
        navigation = systems[0]
        walk["navigation_system"] = path_of(navigation)
        route = method(navigation, "call_method", "FindPathToLocationSynchronously",
                       (world, feet, goal, pawn, None))
        walk["navigation_query_seconds"] = time.monotonic() - query_started
        walk["path_valid"] = bool(route and method(route, "is_valid"))
        walk["path_partial"] = bool(route and method(route, "is_partial"))
        if not walk["path_valid"] or walk["path_partial"]:
            raise RuntimeError("The single independent QA query did not return a complete navigation route")
        self.walk_points = [[float(p.x), float(p.y), float(p.z)] for p in prop(route, "PathPoints")]
        validate_entry_walk_route(self.walk_points, start, radius)
        walk["route_points"] = self.walk_points
        walk["route_length_cm"] = sum(math.dist(a, b) for a, b in zip(self.walk_points, self.walk_points[1:]))
        self.walk_waypoint = 1
        self.walk_last_time = time.monotonic()
        self.walk_was_grounded = False
        self.write_receipt("RUNNING")

    def entry_walk(self, world: Any, now: float) -> None:
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        snapshot = snapshot_dict(method(self.director, "get_snapshot"))
        if (canonical_world(world).lower() != DUNGEON_WORLD.lower() or pawn != self.walk_pawn or
                (snapshot["state"], snapshot["floor_number"], snapshot["run_epoch"], snapshot["run_seed"]) !=
                ("ready", self.expected_floor, self.run_epoch, self.run_seed)):
            raise RuntimeError("The accepted floor or released character changed during entry walking")
        walk = self.ready_sample["entry_walk"]
        p = pawn.get_actor_location()
        location = [float(p.x), float(p.y), float(p.z)]
        walk["end_location"] = location
        walk["elapsed_seconds"] = now - self.phase_started
        walk["distance_cm"] = math.dist(walk["start_location"][:2], location[:2])
        movement = self.walk_movement
        floor = prop(movement, "CurrentFloor")
        grounded = (bool(method(movement, "is_walking")) and bool(method(movement, "is_moving_on_ground"))
                    and bool(prop(floor, "bBlockingHit")) and bool(prop(floor, "bWalkableFloor")))
        if not grounded:
            if walk["input_calls"] == 0 and now - self.phase_started <= 0.5:
                self.walk_last_time = now
                return
            raise RuntimeError("Entry walking did not remain grounded on a walkable floor")
        # Python exports HasNativeBreak on the struct via to_tuple, not as a
        # GameplayStatics method (PyGenUtil excludes NativeBreakFunc).
        hit = method(prop(floor, "HitResult"), "to_tuple")
        if len(hit) != 18:
            raise RuntimeError("Required UE 5.8 BreakHitResult output contract is unavailable")
        actor, component = hit[9], hit[10]
        component_owner = method(component, "get_owner") if component else None
        try:
            component_tags = [str(value) for value in prop(component, "ComponentTags")]
        except Exception:
            component_tags = ["<unavailable>"]
        contact_contract = {
            "blocking_hit": bool(hit[0]), "start_penetrating": bool(hit[1]),
            "actor": path_of(actor), "actor_class": path_of(actor.get_class()) if actor else "",
            "component": path_of(component), "component_class": path_of(component.get_class()) if component else "",
            "component_owner": path_of(component_owner),
            "component_owner_class": path_of(component_owner.get_class()) if component_owner else "",
            "component_tags": component_tags,
            "actor_is_native_generator": bool(actor and path_of(actor.get_class()) == GENERATOR_CLASS),
            "component_is_ism": isinstance(component, unreal.InstancedStaticMeshComponent),
            "component_owner_matches_hit_actor": bool(component_owner == actor),
            "component_has_native_generated_tag": bool(component and method(component, "component_has_tag", "PCG Generated Component")),
        }
        walk["floor_contact"] = contact_contract
        if (not contact_contract["blocking_hit"] or contact_contract["start_penetrating"] or not actor or
                not contact_contract["actor_is_native_generator"] or not contact_contract["component_is_ism"] or
                not contact_contract["component_owner_matches_hit_actor"] or not contact_contract["component_has_native_generated_tag"]):
            raise RuntimeError("Character floor contact is not an owned generated blocking surface")
        if self.walk_was_grounded:
            walk["grounded_seconds"] += max(0.0, now - self.walk_last_time)
        self.walk_was_grounded, self.walk_last_time = True, now
        if not walk["observations"] or now - self.phase_started - walk["observations"][-1]["elapsed_seconds"] >= POLL_SECONDS:
            walk["observations"].append({"elapsed_seconds": now - self.phase_started, "location": location,
                                         "walking": True, "grounded": True, "floor_actor": path_of(actor),
                                         "floor_component": path_of(component), "hit_instance": int(hit[13])})
        if not walk["short_entry_verified"] and entry_walk_satisfied(
                walk["start_location"], location, walk["capsule_radius_cm"], walk["grounded_seconds"], walk["input_calls"]):
            walk["short_entry_verified"] = True
            walk["short_entry_seconds"] = now - self.phase_started
            self.write_receipt("RUNNING")
        if not walk["short_entry_verified"] and now - self.phase_started > ENTRY_WALK_SECONDS:
            raise RuntimeError("Short grounded entry walking exceeded five seconds")
        walk["remaining_goal_distance_cm"] = math.dist(location[:2], self.walk_points[-1][:2])
        if (walk["short_entry_verified"] and self.walk_waypoint == len(self.walk_points) - 1
                and walk["remaining_goal_distance_cm"] <= 30.0):
            walk["status"] = "PASS"
            walk["full_route_walk"] = "PASS"
            self.phase_to("ready_screenshot")
            self.write_receipt("RUNNING")
            return
        while self.walk_waypoint < len(self.walk_points) - 1 and math.dist(location[:2], self.walk_points[self.walk_waypoint][:2]) < 12:
            self.walk_waypoint += 1
        target = self.walk_points[self.walk_waypoint]
        dx, dy = target[0] - location[0], target[1] - location[1]
        length = math.hypot(dx, dy)
        if length > 1:
            method(pawn, "add_movement_input", unreal.Vector(dx / length, dy / length, 0), 1.0, False)
            walk["input_calls"] += 1

    def capture_geometry(self, world: Any, diagnostic: dict[str, Any], filename: str = "first_attempt_geometry.json") -> None:
        """One cooperative, read-only capture; incomplete bindings never affect acceptance."""
        began = time.monotonic()
        stop = began + GEOMETRY_CAPTURE_SECONDS - 0.05  # reserve serialization time
        output = self.output_dir / filename
        document = {"schema_version": 1, "purpose": "diagnostic_only", "attempt_id": diagnostic.get("attempt_id"),
                    "request_id": diagnostic.get("request_id"), "world": path_of(world),
                    "diagnostic": diagnostic, "actors": [], "markers": [], "errors": [],
                    "doorway_capsule_sweeps": [], "doorway_sweep_candidates": 0,
                    "truncation_reasons": [], "component_count": 0, "instance_count": 0,
                    "limits": {"seconds": GEOMETRY_CAPTURE_SECONDS, "components": GEOMETRY_MAX_COMPONENTS,
                               "instances": GEOMETRY_MAX_INSTANCES, "actors_scanned": 2048,
                               "doorway_capsule_sweeps": GEOMETRY_MAX_DOOR_SWEEPS}}
        doorway_transforms = []
        self.geometry_capture = {"path": str(output), "status": "CAPTURING", "attempt_id": diagnostic.get("attempt_id")}

        def vector(v: Any) -> list[float]:
            return [float(v.x), float(v.y), float(v.z)]

        def transform(t: Any) -> dict[str, Any]:
            if t is None:
                raise ValueError("Instance transform read failed")
            return {"location": vector(t.translation), "scale": vector(t.scale3d),
                    "rotation_xyzw": [float(t.rotation.x), float(t.rotation.y), float(t.rotation.z), float(t.rotation.w)]}

        def read(row: dict[str, Any], key: str, operation: Any) -> None:
            if time.monotonic() >= stop:
                raise TimeoutError("capture_time_limit")
            try:
                row[key] = operation()
            except Exception as exc:
                row[key] = None
                document["errors"].append({"path": row.get("path"), "field": key, "error": str(exc)[:300]})

        try:
            actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
            if len(actors) > 2048:
                document["truncation_reasons"].append("actor_scan_limit")
            scanned = []
            for actor in actors[:2048]:
                if time.monotonic() >= stop:
                    raise TimeoutError("capture_time_limit")
                scanned.append((actor, path_of(actor.get_class()), path_of(method(actor, "get_owner"))))
            roots = [a for a, cls, _ in scanned if cls == GENERATOR_CLASS]
            if len(roots) != 1:
                raise ValueError(f"Expected one generated root, found {len(roots)}")
            root = roots[0]
            root_path = path_of(root)
            document["generated_owner"] = root_path
            document["start_class"] = ""
            # NativeAdapter::Prepare resolves StartBlueprint on its Dungeon data object.
            read(document, "start_class", lambda: path_of(prop(prop(root, "Dungeon"), "StartBlueprint")))
            selected = [(a, cls, owner) for a, cls, owner in scanned if a == root or owner == root_path
                        or cls.startswith("/Game/Calysto/") or cls == FLOOR_DOOR_CLASS or cls == document["start_class"]]
            selected.sort(key=lambda item: (item[0] != root, item[1] not in {FLOOR_DOOR_CLASS, document["start_class"]}, path_of(item[0])))
            # Markers first so a truncated instance inventory still locates the route endpoints.
            for actor, cls, owner in selected:
                if cls not in {FLOOR_DOOR_CLASS, document["start_class"]}:
                    continue
                row = {"path": path_of(actor), "class": cls, "owner": owner,
                       "kind": "End" if cls == FLOOR_DOOR_CLASS else "Start"}
                document["markers"].append(row)
                read(row, "transform", lambda a=actor: transform(a.get_actor_transform()))
            for actor, cls, owner in selected:
                if time.monotonic() >= stop:
                    raise TimeoutError("capture_time_limit")
                row = {"path": path_of(actor), "class": cls, "owner": owner,
                       "owned_by_generated_root": actor == root or owner == root_path, "components": []}
                document["actors"].append(row)
                read(row, "transform", lambda a=actor: transform(a.get_actor_transform()))
                components = actor.get_components_by_class(unreal.ActorComponent)
                for component in components:
                    if document["component_count"] >= GEOMETRY_MAX_COMPONENTS:
                        raise TimeoutError("component_limit")
                    c = {"path": path_of(component), "class": path_of(component.get_class())}
                    row["components"].append(c)
                    document["component_count"] += 1
                    read(c, "can_ever_affect_navigation", lambda: bool(prop(component, "bCanEverAffectNavigation")))
                    read(c, "tags", lambda: [str(t) for t in prop(component, "ComponentTags")])
                    if isinstance(component, unreal.SceneComponent):
                        read(c, "world_transform", lambda: transform(method(component, "get_world_transform")))
                        read(c, "bounds", lambda: {"origin": vector(unreal.SystemLibrary.get_component_bounds(component)[0]),
                                                   "extent": vector(unreal.SystemLibrary.get_component_bounds(component)[1])})
                    if isinstance(component, unreal.PrimitiveComponent):
                        read(c, "collision_enabled", lambda: str(method(component, "get_collision_enabled")))
                        read(c, "collision_profile", lambda: str(method(component, "get_collision_profile_name")))
                        read(c, "collision_object_type", lambda: str(method(component, "get_collision_object_type")))
                        read(c, "pawn_collision_response", lambda: str(method(component, "get_collision_response_to_channel", unreal.CollisionChannel.ECC_PAWN)))
                    if isinstance(component, unreal.StaticMeshComponent):
                        read(c, "mesh", lambda: path_of(prop(component, "StaticMesh")))
                    if isinstance(component, unreal.InstancedStaticMeshComponent):
                        count = int(method(component, "get_instance_count"))
                        c.update(instance_count=count, instances=[])
                        for index in range(count):
                            if document["instance_count"] >= GEOMETRY_MAX_INSTANCES:
                                raise TimeoutError("instance_limit")
                            instance = {"index": index}
                            c["instances"].append(instance)
                            def instance_data(i: int = index) -> dict[str, Any]:
                                actual = component.get_instance_transform(i, world_space=True)
                                if actor == root and c.get("mesh") == WALL_DOOR_MESH and actual is not None:
                                    document["doorway_sweep_candidates"] += 1
                                    if len(doorway_transforms) < GEOMETRY_MAX_DOOR_SWEEPS:
                                        doorway_transforms.append((c["path"], i, actual))
                                return transform(actual)
                            read(instance, "world_transform", instance_data)
                            document["instance_count"] += 1
            if document["doorway_sweep_candidates"] > GEOMETRY_MAX_DOOR_SWEEPS:
                document["truncation_reasons"].append("doorway_sweep_limit")
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            capsule = pawn.get_component_by_class(unreal.CapsuleComponent) if pawn else None
            if doorway_transforms and not capsule:
                raise ValueError("Actual player capsule is unavailable for doorway diagnostics")
            if doorway_transforms:
                radius = float(method(capsule, "get_scaled_capsule_radius"))
                half_height = float(method(capsule, "get_scaled_capsule_half_height"))
                if not all(math.isfinite(v) and v > 0 for v in (radius, half_height)):
                    raise ValueError("Actual player capsule dimensions are invalid")
                for component_path, index, actual in doorway_transforms:
                    row = {"component": component_path, "instance_index": index, "mesh": WALL_DOOR_MESH,
                           "radius_cm": radius, "half_height_cm": half_height, "profile": "Pawn",
                           "trace_complex": False, "ignored_actor": path_of(pawn), "debug_draw": "None",
                           "ground_z_source": "wall_door_instance_origin", "ground_z": float(actual.translation.z)}
                    start = actual.transform_location(unreal.Vector(0, -100, 0))
                    end = actual.transform_location(unreal.Vector(0, 100, 0))
                    start.z = end.z = float(actual.translation.z) + half_height + 4.0
                    row.update(start=vector(start), end=vector(end))
                    document["doorway_capsule_sweeps"].append(row)

                    def sweep() -> dict[str, Any]:
                        hit = method(unreal.SystemLibrary, "capsule_trace_single_by_profile", world,
                                     start, end, radius, half_height, "Pawn", False, [pawn],
                                     unreal.DrawDebugTrace.NONE, True)
                        # Python's bool-success/out-struct binding returns HitResult or None.
                        if hit is None:
                            return {"blocking_hit": False}
                        if not isinstance(hit, unreal.HitResult):
                            raise ValueError(f"Unexpected capsule trace result type: {type(hit).__name__}")
                        values = method(unreal.GameplayStatics, "break_hit_result", hit)
                        if len(values) != 18:
                            raise ValueError("Required UE 5.8 BreakHitResult output contract is unavailable")
                        return {"blocking_hit": bool(values[0]), "initial_overlap": bool(values[1]),
                                "time": float(values[2]), "distance_cm": float(values[3]),
                                "location": vector(values[4]), "impact_point": vector(values[5]),
                                "normal": vector(values[6]), "impact_normal": vector(values[7]),
                                "actor": path_of(values[9]), "component": path_of(values[10]),
                                "hit_instance": int(values[13]), "face_index": int(values[15]),
                                "trace_start": vector(values[16]), "trace_end": vector(values[17])}
                    read(row, "result", sweep)
        except TimeoutError as exc:
            document["truncation_reasons"].append(str(exc))
        except Exception as exc:
            document["errors"].append({"error": str(exc)[:500]})
        document["capture_seconds"] = time.monotonic() - began
        document["partial"] = bool(document["errors"] or document["truncation_reasons"])
        document["status"] = "PARTIAL" if document["partial"] else "CAPTURED"
        try:
            raw = json.dumps(document, separators=(",", ":")).encode("utf-8")
            with output.open("xb") as stream:
                stream.write(raw)
            self.geometry_capture.update(status=document["status"], partial=document["partial"],
                capture_seconds=document["capture_seconds"], total_seconds=time.monotonic() - began,
                sha256=hashlib.sha256(raw).hexdigest(),
                truncation_reasons=document["truncation_reasons"], component_count=document["component_count"],
                instance_count=document["instance_count"])
            if self.geometry_capture["total_seconds"] > GEOMETRY_CAPTURE_SECONDS:
                self.geometry_capture.update(status="PARTIAL", partial=True)
                self.geometry_capture["truncation_reasons"].append("noninterruptible_read_or_write_exceeded_time_budget")
        except Exception as exc:
            self.geometry_capture.update(status="CAPTURE_ERROR", error=str(exc)[:500])

    def screenshot(self, world: Any) -> None:
        output = self.output_dir / f"floor_{self.expected_floor:02d}.png"
        if not self.screenshot_requested:
            if time.monotonic() - self.phase_started < READY_HOLD_SECONDS:
                return
            if output.exists():
                raise RuntimeError("Screenshot path already exists; stale images cannot verify this floor")
            accepted = method(unreal.AutomationLibrary, "take_high_res_screenshot", 1280, 720, str(output))
            if accepted is False:
                raise RuntimeError("Viewport rejected the floor screenshot request")
            self.screenshot_requested = True
            return
        if not output.is_file():
            return
        if output.stat().st_size > MAXIMUM_SCREENSHOT_BYTES:
            raise RuntimeError("Viewport screenshot exceeded its bounded artifact size")
        raw = output.read_bytes()
        if len(raw) < 32 or raw[:8] != b"\x89PNG\r\n\x1a\n" or raw[12:16] != b"IHDR" or raw[-12:] != b"\x00\x00\x00\x00IEND\xaeB\x60\x82":
            return
        self.ready_sample["screenshot"] = {"path": str(output), "sha256": hashlib.sha256(raw).hexdigest(),
                                           "visual_review": "PENDING"}
        self.samples.append(self.ready_sample)
        self.ready_sample = None
        self.write_receipt("RUNNING")
        if self.expected_floor < 3:
            self.expected_floor += 1
            self.phase_to("interact_floor_door")
        else:
            accepted = bool(method(self.director, "request_return_to_hub"))
            self.operations.append({"operation": "request_return_to_hub", "accepted": accepted,
                                    "reason": "explicit_test_return_after_three_floors", "counts_as_floor_cap_return": False})
            if not accepted:
                raise RuntimeError("Director rejected the explicit test Return to HUB request")
            self.phase_to("waiting_for_explicit_hub")

    def finish(self, success: bool, error: str = "") -> None:
        if self.finished or self.phase == "stopping_pie":
            return
        self.pending_success, self.error = success, error
        if self.ready_sample and self.ready_sample.get("entry_walk", {}).get("status") == "PENDING":
            self.ready_sample["entry_walk"].update(status="FAIL", error=error)
        self.phase_to("stopping_pie")
        if self.pie_observed and LEVEL_EDITOR.is_in_play_in_editor():
            unreal.EditorLevelLibrary.editor_end_play()
        else:
            self.complete()

    def complete(self) -> None:
        self.finished = True
        if self.callback is not None:
            unreal.unregister_slate_post_tick_callback(self.callback)
            self.callback = None
        if ([s["floor_number"] for s in self.samples] != EXPECTED_FLOORS or not self.explicit_hub_return_verified or
                not all(s.get("entry_walk", {}).get("status") == "PASS" for s in self.samples)):
            self.pending_success = False
        newly_dirty = sorted(set(dirty_packages()) - set(self.initial_dirty))
        if newly_dirty:
            self.pending_success = False
            self.error += f" New dirty packages appeared: {newly_dirty}"
        status = ("NATIVE_TRAVERSAL_ONLY" if self.mode == "native_parity" else "TRAVERSAL_OBSERVED") if self.pending_success else "FAIL"
        self.write_receipt(status)
        setattr(builtins, "_codex_calysto_director_traversal58", None)
        unreal.log(f"CALYSTO_DIRECTOR_TRAVERSAL_RESULT status={status} receipt={self.output_dir / 'traversal.json'}")

    def write_receipt(self, status: str) -> None:
        script = EXPECTED_ROOT / "Tools/Migration/Validate-CalystoDirectorTraversal58.py"
        document = {"receipt_schema_version": 1, "status": status, "mode": self.mode, "phase": self.phase,
                    "requires_realized_architecture": self.require_architecture,
                    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    "script": str(script), "script_sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
                    "project": str(PROJECT_ROOT), "elapsed_seconds": time.monotonic() - self.started,
                    "total_deadline_seconds": TOTAL_DEADLINE_SECONDS, "error": self.error,
                    "run_seed": self.run_seed, "run_epoch": self.run_epoch, "samples": self.samples,
                    "pending_floor_sample": self.ready_sample,
                    "geometry_capture": self.geometry_capture,
                    "short_entry_walk_verified": (len(self.samples) == 3 and all(s.get("entry_walk", {}).get("status") == "PASS" for s in self.samples)),
                    "full_route_walking_verified": ("PASS" if len(self.samples) == 3 and all(
                        s.get("entry_walk", {}).get("full_route_walk") == "PASS" for s in self.samples) else "PENDING"),
                    "operations": self.operations, "state_changes": self.states,
                    "initial_dirty_packages": self.initial_dirty, "final_dirty_packages": dirty_packages(),
                    "consecutive_floors_observed": [s["floor_number"] for s in self.samples] == EXPECTED_FLOORS,
                    "explicit_return_to_hub_verified": self.explicit_hub_return_verified,
                    "intended_final_floor_cap_return": "PENDING", "gameplay_verified": False,
                    "probability_set": "SEPARATE_NATIVE_TEST_GATE", "visual_review": "PENDING",
                    "protected_invariants": "SEPARATE_EXTERNAL_GATE", "process_exit_code": None,
                    "process_exit_and_log_audit": "PENDING", "release_accepted": False}
        temporary = self.output_dir / "traversal.json.tmp"
        temporary.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        temporary.replace(self.output_dir / "traversal.json")

    def tick(self, _delta: float) -> None:
        if self.finished:
            return
        now = time.monotonic()
        if now < self.next_poll and self.phase != "entry_walk":
            return
        self.next_poll = now + POLL_SECONDS
        try:
            if self.phase == "stopping_pie":
                if not LEVEL_EDITOR.is_in_play_in_editor():
                    self.complete()
                elif now - self.phase_started > PIE_STOP_SECONDS:
                    self.pending_success = False
                    self.error += " PIE did not stop within five seconds."
                    self.complete()
                return
            if now - self.started > TOTAL_DEADLINE_SECONDS - PIE_STOP_SECONDS:
                raise RuntimeError("Traversal reached its total deadline; five seconds remain reserved for stopping PIE")
            limits = {"waiting_for_external_pie": PIE_START_SECONDS, "interact_entrance": INTERACTION_SECONDS,
                      "interact_floor_door": INTERACTION_SECONDS, "waiting_for_floor": REQUEST_OBSERVATION_SECONDS,
                      "entry_walk": ROUTE_WALK_SECONDS, "ready_screenshot": SCREENSHOT_SECONDS,
                      "waiting_for_explicit_hub": RETURN_HUB_SECONDS}
            if now - self.phase_started > limits[self.phase]:
                raise RuntimeError(f"Bounded phase timed out: {self.phase}")
            world = UNREAL_EDITOR.get_game_world()
            if self.pie_observed and not LEVEL_EDITOR.is_in_play_in_editor():
                raise RuntimeError("The test PIE session ended before traversal completed")
            if self.phase == "waiting_for_external_pie":
                if not LEVEL_EDITOR.is_in_play_in_editor() or not world:
                    return
                self.pie_observed = True
                if canonical_world(world).lower() != HUB_WORLD.lower():
                    raise RuntimeError("The externally started PIE session must begin in HUB")
                director = self.find_director(world)
                if not director:
                    return
                if not callable(getattr(director, "get_diagnostics_json", None)):
                    raise RuntimeError("Required read-only GetDiagnosticsJson is not implemented; native traversal evidence cannot be certified")
                snapshot = snapshot_dict(method(director, "get_snapshot"))
                if snapshot["state"] != "idle" or snapshot["floor_number"] != 0:
                    raise RuntimeError("The actual entrance must start an idle new Director run")
                self.phase_to("interact_entrance")
                return
            if not world:
                return
            if self.phase == "entry_walk":
                self.entry_walk(world, now)
                return
            current = canonical_world(world).lower()
            if self.phase == "waiting_for_explicit_hub":
                if current == HUB_WORLD.lower():
                    self.explicit_hub_return_verified = True
                    self.finish(True)
                return
            director = self.find_director(world)
            if not director:
                return
            snapshot = snapshot_dict(method(director, "get_snapshot"))
            if self.run_epoch is not None and (snapshot["run_epoch"], snapshot["run_seed"]) != (self.run_epoch, self.run_seed):
                raise RuntimeError("The Director changed run identity during the consecutive-floor test")
            diagnostic = json.loads(str(method(director, "get_diagnostics_json")))
            attempts = diagnostic.get("attempts", [])
            observation = (attempts[-1].get("native_observation") or {}) if attempts else {}
            if (self.geometry_capture is None and self.mode == "native_parity"
                    and OPTIONS.get("capture_geometry", True) is True and snapshot["floor_number"] == 1 and snapshot["attempt_count"] == 1
                    and current == DUNGEON_WORLD.lower()
                    and diagnostic.get("transaction_phase") in {"StructuralVerification", "NavigationAndReservations"}):
                self.capture_geometry(world, diagnostic)
            state_key = (snapshot["state"], snapshot["floor_number"], snapshot["attempt_count"], snapshot["topology_seed"],
                         diagnostic.get("transaction_phase"), observation.get("code"))
            if not self.states or tuple(self.states[-1]["key"]) != state_key:
                self.states.append({"key": list(state_key), "elapsed_seconds": now - self.started,
                                    "snapshot": snapshot, "diagnostic": diagnostic})
                self.write_receipt("RUNNING")
            if snapshot["state"] in {"failed", "cancelled"}:
                raise RuntimeError(f"First terminal Director failure: {snapshot['failure_message'] or snapshot['state']}")
            if self.phase == "interact_entrance":
                self.interact(world, True)
                return
            if current == DUNGEON_WORLD.lower():
                self.saw_dungeon = True
            elif self.saw_dungeon:
                raise RuntimeError("Unexpected world/HUB bounce before the explicit test return")
            if self.phase == "waiting_for_floor":
                if current == DUNGEON_WORLD.lower() and snapshot["state"] == "ready":
                    self.audit_ready(world, director, snapshot)
                return
            if self.phase == "ready_screenshot":
                if snapshot["state"] != "ready" or snapshot["floor_number"] != self.expected_floor:
                    raise RuntimeError("The accepted floor changed before its screenshot was captured")
                self.screenshot(world)
                return
            if self.phase == "interact_floor_door":
                if snapshot["floor_number"] != self.expected_floor - 1 or snapshot["state"] != "ready":
                    raise RuntimeError("Floor progression changed before the real door interaction")
                self.interact(world, False)
        except Exception as exc:
            self.finish(False, f"{exc}\n{traceback.format_exc()}")


existing = getattr(builtins, "_codex_calysto_director_traversal58", None)
if existing is not None and not getattr(existing, "finished", True):
    raise RuntimeError("A Director traversal harness is already active")
STATE = Traversal()
# Manual console execution can retain the script as before.  MCP arming must
# return immediately so its caller can start PIE within this harness's bounded
# startup window; STATE and the callback handle are retained in builtins.
if not OPTIONS.get("mcp_arm", False):
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
STATE.callback = unreal.register_slate_post_tick_callback(STATE.tick)
builtins._codex_calysto_director_traversal58 = STATE
unreal.log(f"CALYSTO_DIRECTOR_TRAVERSAL armed=true mode={STATE.mode} deadline=150 output={STATE.output_dir}")
