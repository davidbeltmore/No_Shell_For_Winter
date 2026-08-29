"""Development-only exact-size certification matrix for Dungeon Director V3.

This script is launched by Run-CalystoDungeonSizeMatrixPIE58.ps1.  It keeps one
Editor/PIE session alive for the requested size/seed set, arms the native
unattended-PIE override before each New Run, and continues after a failed floor
returns safely to the HUB.  A candidate validated-size set is armed once for
the session and cleared before shutdown.  It never saves an asset.  Dirty-state
unknowns, incomplete physical measurements, and ambiguous log ordering fail
closed while remaining distinct from actual on-disk mutations.
"""

import builtins
import datetime
import hashlib
import json
import os
import re
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir()).resolve()
CONTENT_DIR = Path(unreal.Paths.project_content_dir()).resolve()
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_SIZE_MATRIX_OUTPUT"])
LOG_FILE = Path(os.environ["CODEX_CALYSTO_SIZE_MATRIX_LOG"])
SIZES = tuple(
    int(value)
    for value in os.environ.get("CODEX_CALYSTO_SIZE_MATRIX_SIZES", "").split(",")
    if value.strip()
)
SEEDS_PER_SIZE = int(
    os.environ.get("CODEX_CALYSTO_SIZE_MATRIX_SEEDS_PER_SIZE", "20")
)
BASE_SEED = int(os.environ.get("CODEX_CALYSTO_SIZE_MATRIX_BASE_SEED", "202608140000"))
EXPLICIT_SEEDS = tuple(
    int(value)
    for value in os.environ.get("CODEX_CALYSTO_SIZE_MATRIX_SEEDS", "").split(",")
    if value.strip()
)
CASE_TIMEOUT_SECONDS = float(
    os.environ.get("CODEX_CALYSTO_SIZE_MATRIX_CASE_TIMEOUT", "100")
)
CASE_ORDER = os.environ.get(
    "CODEX_CALYSTO_SIZE_MATRIX_CASE_ORDER", "rotating"
).strip().lower()
CERTIFICATION_MIN_UNIQUE_SEEDS = 20

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy"
POLICY_FILE = (
    CONTENT_DIR
    / "_Game"
    / "Data"
    / "CalystoDungeon"
    / "V3"
    / "DA_CalystoDungeonDirectorPolicy.uasset"
)
SUBSYSTEM_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS = "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
ANCHOR_CLASS = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
PLAYER_START_CLASS = "/Script/Engine.PlayerStart"
NAV_BOUNDS_CLASS = "/Script/NavigationSystem.NavMeshBoundsVolume"
RECAST_CLASS = "/Script/NavigationSystem.RecastNavMesh"
FORCE_COMMAND = "EF.Calysto.Automation.ForceDungeonEdge"
CANDIDATE_SIZES_COMMAND = "EF.Calysto.Automation.SetCandidateValidatedSizes"

PROTECTED_FILES = (
    "Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset",
    "Content/Calysto/Dungeon/Blueprint/Utility/BP_EndPoint.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner.uasset",
    "Content/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.uasset",
    "Content/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_SpawnStartAndEnd.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_SetRoomTheme.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_DungeonSpawner.uasset",
    "Content/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.uasset",
    "Content/Procedural/Maps/DungeonGeneration.umap",
    "Content/Procedural/DoorToLevel.uasset",
    "Content/FullSample/Player.uasset",
    "Content/DazToUnreal/Female/Female.uasset",
    "Content/DazToUnreal/Multiple/Multiple.uasset",
    "Content/DazToUnreal/Male/Male.uasset",
)
MONITORED_OBJECTS = (
    POLICY_OBJECT,
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
)
FORBIDDEN_LOG_PATTERNS = (
    "Blueprint Runtime Error",
    "LogBlueprint: Error",
    "Accessed None",
    "Ensure condition failed",
    "Fatal error:",
    "Assertion failed:",
    "cancelled or cleaned",
    "duplicate generation",
    "Object Transform",
    "GetAttributeFromPointIndex_0",
    "LogPCG: Error",
    "LogEFCalystoDungeon: Error",
    "LogEFCalystoPopulation: Error",
    "LogEFProceduralPCGRuntime: Error",
    "Calysto controlled generation failed",
    "Calysto generation failed",
)
GLOBAL_ERROR_MARKERS = (
    ("log_error", re.compile(r"\bLog[^:\]\s]+:\s+Error:", re.IGNORECASE)),
    ("blueprint_runtime_error", re.compile(r"Blueprint Runtime Error", re.IGNORECASE)),
    ("ensure", re.compile(r"Ensure condition failed", re.IGNORECASE)),
    ("fatal", re.compile(r"Fatal error:", re.IGNORECASE)),
    ("assertion", re.compile(r"Assertion failed:", re.IGNORECASE)),
)
KNOWN_NON_CALYSTO_ERROR_NOISE = (
    "LogTemp: Error: Can't Start the quest",
)
CALYSTO_ERROR_TOKENS = (
    "LogEFCalystoDungeon:",
    "LogEFCalystoPopulation:",
    "LogEFProceduralPCGRuntime:",
    "LogPCG:",
    "Calysto controlled generation failed",
    "Calysto generation failed",
    "Object Transform",
    "GetAttributeFromPointIndex_0",
)
VALID_CASE_ORDERS = ("rotating", "seed-major", "size-major")

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL = unreal.EditorLevelLibrary


def object_path(value):
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return "<invalid UObject>"


def canonical_world_path(world):
    return re.sub(r"uedpie_\d+_", "", object_path(world), flags=re.IGNORECASE)


def game_world():
    return EDITOR_LEVEL.get_game_world()


def world_time(world):
    return float(unreal.GameplayStatics.get_time_seconds(world)) if world else 0.0


def camel_to_snake(value):
    first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", value)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower()


def struct_property(owner, name):
    for candidate in (name, camel_to_snake(name)):
        try:
            return owner.get_editor_property(candidate)
        except Exception:
            pass
        try:
            return getattr(owner, candidate)
        except Exception:
            pass
    raise RuntimeError("Missing reflected property {} on {}".format(name, object_path(owner)))


def reflected(owner, name):
    method = getattr(owner, name, None)
    if not callable(method):
        raise RuntimeError("Missing reflected method {} on {}".format(name, object_path(owner)))
    return method


def reflected_bool(owner, *names):
    for name in names:
        try:
            value = struct_property(owner, name)
            return bool(value() if callable(value) else value)
        except Exception:
            pass
    raise RuntimeError("Missing reflected bool {} on {}".format(names, object_path(owner)))


def actors_of_class(world, actor_class):
    if not world or not actor_class:
        return []
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def vector_document(value):
    if value is None:
        return None
    result = {}
    for axis in ("x", "y", "z"):
        try:
            component = getattr(value, axis)
        except Exception:
            try:
                component = struct_property(value, axis.upper())
            except Exception:
                return None
        result[axis] = round(float(component), 3)
    return result


def bounds_from_min_max(minimum, maximum):
    if not minimum or not maximum:
        return None
    size = {
        axis: round(float(maximum[axis]) - float(minimum[axis]), 3)
        for axis in ("x", "y", "z")
    }
    center = {
        axis: round((float(maximum[axis]) + float(minimum[axis])) * 0.5, 3)
        for axis in ("x", "y", "z")
    }
    return {
        "min": {axis: round(float(minimum[axis]), 3) for axis in ("x", "y", "z")},
        "max": {axis: round(float(maximum[axis]), 3) for axis in ("x", "y", "z")},
        "center": center,
        "size": size,
    }


def bounds_from_points(points):
    rows = [point for point in points if point]
    if not rows:
        return None
    minimum = {axis: min(point[axis] for point in rows) for axis in ("x", "y", "z")}
    maximum = {axis: max(point[axis] for point in rows) for axis in ("x", "y", "z")}
    return bounds_from_min_max(minimum, maximum)


def merge_bounds(left, right):
    if not left:
        return right
    if not right:
        return left
    minimum = {
        axis: min(float(left["min"][axis]), float(right["min"][axis]))
        for axis in ("x", "y", "z")
    }
    maximum = {
        axis: max(float(left["max"][axis]), float(right["max"][axis]))
        for axis in ("x", "y", "z")
    }
    return bounds_from_min_max(minimum, maximum)


def actor_bounds_document(actor):
    if not actor:
        return None
    try:
        origin, extent = actor.get_actor_bounds(False, True)
    except TypeError:
        try:
            origin, extent = actor.get_actor_bounds(False)
        except Exception:
            return None
    except Exception:
        return None
    origin_document = vector_document(origin)
    extent_document = vector_document(extent)
    if not origin_document or not extent_document:
        return None
    minimum = {
        axis: origin_document[axis] - extent_document[axis]
        for axis in ("x", "y", "z")
    }
    maximum = {
        axis: origin_document[axis] + extent_document[axis]
        for axis in ("x", "y", "z")
    }
    result = bounds_from_min_max(minimum, maximum)
    result["origin"] = origin_document
    result["extent"] = extent_document
    return result


def new_spatial_observation():
    return {
        "method": (
            "Dungeon actor bounds plus transient population-anchor, floor-door, "
            "and PlayerStart world-space extents sampled once per PIE tick"
        ),
        "dungeon_actor": {
            "observation_count": 0,
            "last_bounds": None,
            "union_bounds": None,
        },
        "anchors": {
            "observation_count": 0,
            "peak_actor_count": 0,
            "peak_bounds": None,
            "union_bounds": None,
        },
        "floor_doors": {
            "observation_count": 0,
            "peak_actor_count": 0,
            "union_bounds": None,
        },
        "player_starts": {
            "observation_count": 0,
            "peak_actor_count": 0,
            "union_bounds": None,
        },
    }


def observe_actor_locations(world, actor_class):
    actors = actors_of_class(world, actor_class)
    points = []
    for actor in actors:
        try:
            points.append(vector_document(actor.get_actor_location()))
        except Exception:
            pass
    return actors, bounds_from_points(points)


def update_location_observation(document, actors, bounds):
    if not actors:
        return
    document["observation_count"] += 1
    document["union_bounds"] = merge_bounds(document["union_bounds"], bounds)
    if len(actors) > document["peak_actor_count"]:
        document["peak_actor_count"] = len(actors)
        document["peak_bounds"] = bounds
    elif len(actors) == document["peak_actor_count"]:
        document["peak_bounds"] = merge_bounds(document.get("peak_bounds"), bounds)


def capture_spatial_observation(world):
    if not STATE.current or not world:
        return
    spatial = STATE.current["physical_bounds"]
    dungeons = actors_of_class(world, STATE.dungeon_class)
    if len(dungeons) == 1:
        bounds = actor_bounds_document(dungeons[0])
        if bounds:
            target = spatial["dungeon_actor"]
            target["observation_count"] += 1
            target["last_bounds"] = bounds
            target["union_bounds"] = merge_bounds(target["union_bounds"], bounds)
    for key, actor_class in (
        ("anchors", STATE.anchor_class),
        ("floor_doors", STATE.door_class),
        ("player_starts", STATE.player_start_class),
    ):
        actors, bounds = observe_actor_locations(world, actor_class)
        update_location_observation(spatial[key], actors, bounds)


def quantized_spatial_fingerprint(document):
    if not document:
        return ""
    source = {
        "dungeon": document["dungeon_actor"].get("last_bounds"),
        "anchor_peak_count": document["anchors"].get("peak_actor_count", 0),
        "anchor_union": document["anchors"].get("union_bounds"),
        "door_union": document["floor_doors"].get("union_bounds"),
        "player_start_union": document["player_starts"].get("union_bounds"),
    }
    if not any((source["dungeon"], source["anchor_union"], source["door_union"])):
        return ""
    canonical = json.dumps(source, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest().upper()


def component_is_editor_only(component):
    method = getattr(component, "is_editor_only", None)
    if callable(method):
        try:
            return bool(method())
        except Exception:
            pass
    try:
        return bool(struct_property(component, "bIsEditorOnly"))
    except Exception:
        return False


def find_game_instance_subsystem(world, subsystem_class):
    game_instance = unreal.GameplayStatics.get_game_instance(world) if world else None
    if not game_instance:
        return None
    for method_name in ("get_subsystem", "get_game_instance_subsystem"):
        method = getattr(game_instance, method_name, None)
        if callable(method):
            try:
                result = method(subsystem_class)
                if result:
                    return result
            except Exception:
                pass
    prefix = object_path(game_instance)
    for candidate in unreal.ObjectIterator(unreal.Object):
        try:
            if candidate.get_class() == subsystem_class and object_path(candidate).startswith(prefix):
                return candidate
        except Exception:
            pass
    return None


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def protected_hashes():
    result = {}
    for relative in PROTECTED_FILES:
        path = PROJECT_DIR / relative
        result[relative] = {
            "exists": path.is_file(),
            "length": path.stat().st_size if path.is_file() else None,
            "sha256": file_sha256(path) if path.is_file() else None,
        }
    return result


def content_snapshot():
    result = {}
    for suffix in ("*.uasset", "*.umap"):
        for path in CONTENT_DIR.rglob(suffix):
            stat = path.stat()
            result[path.relative_to(CONTENT_DIR).as_posix()] = [
                int(stat.st_size),
                int(stat.st_mtime_ns),
            ]
    return result


def package_dirty(object_name):
    asset = unreal.load_asset(object_name)
    if not asset:
        return None
    try:
        method = getattr(asset.get_outermost(), "is_dirty", None)
        return bool(method()) if callable(method) else None
    except Exception:
        return None


def dirty_states():
    probe = {
        "method": "EditorLoadingAndSavingUtils dirty-package inventory",
        "error": "",
        "dirty_package_count": None,
    }
    try:
        dirty_packages = []
        utility = unreal.EditorLoadingAndSavingUtils
        for method_name in ("get_dirty_content_packages", "get_dirty_map_packages"):
            method = getattr(utility, method_name, None)
            if not callable(method):
                raise RuntimeError("Missing Unreal dirty-package API " + method_name)
            dirty_packages.extend(list(method()))
        dirty_package_names = {object_path(package) for package in dirty_packages}
        probe["dirty_package_count"] = len(dirty_package_names)
        result = {}
        for name in MONITORED_OBJECTS:
            asset = unreal.load_asset(name)
            if not asset:
                result[name] = None
                continue
            package_name = object_path(asset.get_outermost())
            result[name] = package_name in dirty_package_names
        return result, probe
    except Exception as exc:
        probe["method"] = "Per-package is_dirty fallback"
        probe["error"] = str(exc)
        return {name: package_dirty(name) for name in MONITORED_OBJECTS}, probe


def policy_document():
    asset = unreal.load_asset(POLICY_OBJECT)
    if not asset:
        raise RuntimeError("The sole V3 policy is unavailable")
    return {
        "object_path": POLICY_OBJECT,
        "schema_version": int(struct_property(asset, "SchemaVersion")),
        "generator_version": int(struct_property(asset, "GeneratorVersion")),
        "policy_id": str(struct_property(asset, "PolicyId")),
        "validated_dungeon_sizes": sorted(
            int(value) for value in list(struct_property(asset, "ValidatedDungeonSizes"))
        ),
        "uasset_path": str(POLICY_FILE),
        "uasset_sha256": file_sha256(POLICY_FILE) if POLICY_FILE.is_file() else None,
    }


def make_cases():
    seed_count = len(EXPLICIT_SEEDS) if EXPLICIT_SEEDS else SEEDS_PER_SIZE

    def seed_for(edge, index):
        if EXPLICIT_SEEDS:
            return EXPLICIT_SEEDS[index]
        return BASE_SEED + edge * 1000 + index

    if CASE_ORDER == "size-major":
        return [
            {"edge": edge, "seed_index": index, "run_seed": seed_for(edge, index)}
            for edge in SIZES
            for index in range(seed_count)
        ]

    cases = []
    ordered_sizes = list(SIZES)
    for index in range(seed_count):
        if CASE_ORDER == "rotating" and ordered_sizes:
            shift = int((BASE_SEED + index) % len(ordered_sizes))
            per_seed_sizes = ordered_sizes[shift:] + ordered_sizes[:shift]
            if index % 2:
                per_seed_sizes = list(reversed(per_seed_sizes))
        else:
            per_seed_sizes = ordered_sizes
        for edge in per_seed_sizes:
            cases.append({
                "edge": edge,
                "seed_index": index,
                "run_seed": seed_for(edge, index),
            })
    return cases


class State:
    def __init__(self):
        self.started = time.monotonic()
        self.phase_started = self.started
        self.phase = "load_map"
        self.callback = None
        self.finished = False
        self.error = ""
        self.cases = make_cases()
        self.case_index = -1
        self.current = None
        self.results = []
        self.subsystem_class = None
        self.door_class = None
        self.dungeon_class = None
        self.anchor_class = None
        self.player_start_class = None
        self.nav_bounds_class = None
        self.recast_class = None
        self.log_offset = 0
        self.log_partial = b""
        self.case_log_line_ordinal = 0
        self.candidate_policy_armed = False
        self.candidate_policy_arm_count = 0
        self.candidate_policy_expected_hash = ""
        self.candidate_policy_observed_hash = ""
        self.candidate_policy_verified = False
        self.candidate_policy_clear_requested = False
        self.candidate_policy_clear_verified = False
        self.candidate_policy_clear_error = ""
        self.initial_content = content_snapshot()
        self.initial_dirty, self.initial_dirty_probe = dirty_states()
        self.initial_protected = protected_hashes()
        self.initial_policy = policy_document()
        self.final_content = {}
        self.final_dirty = {}
        self.final_dirty_probe = {}
        self.final_protected = {}
        self.global_log_errors = {}
        self.phase_history = [{"phase": self.phase, "elapsed_seconds": 0.0}]


STATE = State()


def elapsed():
    return time.monotonic() - STATE.started


def phase_age():
    return time.monotonic() - STATE.phase_started


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    STATE.phase_history.append({"phase": value, "elapsed_seconds": round(elapsed(), 3)})
    unreal.log("[CalystoSizeMatrixPIE58] phase=" + value)


def new_attempt(line, line_ordinal=None):
    attempt = {
        "attempt": len(STATE.current["attempts"]) + 1,
        "opening_log": line,
        "generate_local_count": 0,
        "event_ordinals": {
            "open_level": line_ordinal,
            "generate_local": None,
            "pcg_complete": None,
            "navigation_path_ready": None,
            "population_realized": None,
            "door_enabled": None,
        },
        "events": ([{
            "event": "open_level",
            "line_ordinal": line_ordinal,
            "elapsed_seconds": round(time.monotonic() - STATE.current["started"], 3),
            "log_line": line,
        }] if line_ordinal is not None else []),
        "timings_seconds": {
            "open_level": round(time.monotonic() - STATE.current["started"], 3),
            "generate_local": None,
            "pcg_complete": None,
            "navigation_path_ready": None,
            "population_realized": None,
            "door_enabled": None,
        },
    }
    STATE.current["attempts"].append(attempt)
    return attempt


def current_attempt(line="", line_ordinal=None):
    if not STATE.current["attempts"]:
        return new_attempt(
            line or "marker observed before Opening Calysto V3 log", line_ordinal
        )
    return STATE.current["attempts"][-1]


def record_attempt_event(attempt, event, line, line_ordinal, now):
    attempt["events"].append({
        "event": event,
        "line_ordinal": line_ordinal,
        "elapsed_seconds": now,
        "log_line": line,
    })
    if attempt["event_ordinals"].get(event) is None:
        attempt["event_ordinals"][event] = line_ordinal
    if attempt["timings_seconds"].get(event) is None:
        attempt["timings_seconds"][event] = now


def scan_log():
    if not STATE.current or not LOG_FILE.is_file():
        return
    with LOG_FILE.open("rb") as stream:
        stream.seek(STATE.log_offset)
        chunk = stream.read()
        STATE.log_offset = stream.tell()
    if not chunk:
        return
    data = STATE.log_partial + chunk
    lines = data.split(b"\n")
    STATE.log_partial = lines.pop() if lines else b""
    for raw in lines:
        STATE.case_log_line_ordinal += 1
        line_ordinal = STATE.case_log_line_ordinal
        line = raw.decode("utf-8", errors="replace").rstrip("\r")
        lower = line.lower()
        now = round(time.monotonic() - STATE.current["started"], 3)
        if "opening calysto v3 run=" in lower:
            new_attempt(line, line_ordinal)
        elif "adapter requested generatelocal exactly once" in lower:
            attempt = current_attempt(line, line_ordinal)
            attempt["generate_local_count"] += 1
            record_attempt_event(attempt, "generate_local", line, line_ordinal, now)
        elif "pcgcomplete world=" in lower:
            attempt = current_attempt(line, line_ordinal)
            record_attempt_event(attempt, "pcg_complete", line, line_ordinal, now)
        elif "navigationpathready world=" in lower:
            attempt = current_attempt(line, line_ordinal)
            record_attempt_event(
                attempt, "navigation_path_ready", line, line_ordinal, now
            )
        elif "populationrealized world=" in lower:
            attempt = current_attempt(line, line_ordinal)
            record_attempt_event(
                attempt, "population_realized", line, line_ordinal, now
            )
        elif "doorenabled world=" in lower:
            attempt = current_attempt(line, line_ordinal)
            record_attempt_event(attempt, "door_enabled", line, line_ordinal, now)
        for pattern in FORBIDDEN_LOG_PATTERNS:
            if pattern.lower() in lower:
                STATE.current["errors"].append({"pattern": pattern, "line": line})


def global_log_error_document():
    document = {
        "method": "Whole-log line scan before the final JSON receipt is logged",
        "gate_status": "PASS",
        "total_count": 0,
        "actionable_count": 0,
        "known_non_calysto_noise_count": 0,
        "calysto_count": 0,
        "other_count": 0,
        "entries": [],
    }
    if not LOG_FILE.is_file():
        document["gate_status"] = "PENDING"
        document["error"] = "Log file was unavailable for the global error scan"
        return document
    try:
        with LOG_FILE.open("r", encoding="utf-8", errors="replace") as stream:
            for line_number, raw_line in enumerate(stream, 1):
                line = raw_line.rstrip("\r\n")
                markers = [
                    name for name, pattern in GLOBAL_ERROR_MARKERS
                    if pattern.search(line)
                ]
                if not markers:
                    continue
                known_noise = any(token.lower() in line.lower()
                                  for token in KNOWN_NON_CALYSTO_ERROR_NOISE)
                is_calysto = any(token.lower() in line.lower()
                                 for token in CALYSTO_ERROR_TOKENS)
                classification = (
                    "known_non_calysto_noise" if known_noise
                    else "calysto" if is_calysto
                    else "other"
                )
                document["entries"].append({
                    "line_number": line_number,
                    "markers": markers,
                    "classification": classification,
                    "actionable": not known_noise,
                    "line": line,
                })
                document["total_count"] += 1
                if known_noise:
                    document["known_non_calysto_noise_count"] += 1
                else:
                    document["actionable_count"] += 1
                    document[classification + "_count"] += 1
        if document["actionable_count"]:
            document["gate_status"] = "FAIL"
        return document
    except Exception as exc:
        document["gate_status"] = "PENDING"
        document["error"] = str(exc)
        return document


def snapshot_document(subsystem):
    snapshot = reflected(subsystem, "get_snapshot")()
    size = struct_property(snapshot, "DungeonSize")
    return {
        "policy_valid": bool(struct_property(snapshot, "bPolicyValid")),
        "policy_error": str(struct_property(snapshot, "PolicyError")),
        "run_seed": int(struct_property(snapshot, "RunSeed")),
        "floor_number": int(struct_property(snapshot, "FloorNumber")),
        "generation_serial": int(struct_property(snapshot, "GenerationSerial")),
        "pcg_seed": int(struct_property(snapshot, "PCGSeed")),
        "dungeon_size": {
            "x": int(struct_property(size, "X")),
            "y": int(struct_property(size, "Y")),
            "z": int(struct_property(size, "Z")),
        },
        "policy_hash": str(struct_property(snapshot, "PolicyHash")),
        "ecology_hash": str(struct_property(snapshot, "EcologyHash")),
        "intent_hash": str(struct_property(snapshot, "IntentHash")),
        "manifest_hash": str(struct_property(snapshot, "ManifestHash")),
        "travel_state": str(struct_property(snapshot, "TravelState")),
        "generation_state": str(struct_property(snapshot, "GenerationState")),
    }


def runtime_ready_sample(world, subsystem):
    snapshot = snapshot_document(subsystem)
    if (
        not snapshot["policy_valid"]
        or snapshot["run_seed"] != STATE.current["run_seed"]
        or snapshot["floor_number"] != 1
        or snapshot["generation_serial"] != 1
        or "ready" not in snapshot["generation_state"].lower()
        or not snapshot["manifest_hash"]
    ):
        return None
    intent = reflected(subsystem, "get_resolved_floor_intent")()
    manifest = reflected(subsystem, "get_realized_floor_manifest")()
    if not bool(struct_property(intent, "bIsValid")) or not bool(struct_property(manifest, "bIsValid")):
        return None
    doors = actors_of_class(world, STATE.door_class)
    dungeons = actors_of_class(world, STATE.dungeon_class)
    nav_bounds = actors_of_class(world, STATE.nav_bounds_class)
    recast = actors_of_class(world, STATE.recast_class)
    pcg_components = []
    for dungeon in dungeons:
        pcg_components.extend(dungeon.get_components_by_class(unreal.PCGComponent))
    runtime_pcg = [component for component in pcg_components if not component_is_editor_only(component)]
    if (
        len(doors) != 1
        or len(dungeons) != 1
        or len(runtime_pcg) != 1
        or not nav_bounds
        or not recast
        or not reflected_bool(doors[0], "bIsEnabled", "is_enabled")
    ):
        return None
    door = doors[0]
    door_tags = sorted(
        str(value) for value in door.get_editor_property("tags")
    )
    repair_tag = "EF.Calysto.TopologyRepair.V3"
    repair_hash_prefix = "EF.Calysto.TopologyRepairHash."
    repair_hashes = sorted(
        value[len(repair_hash_prefix):]
        for value in door_tags
        if value.startswith(repair_hash_prefix)
    )
    repair_applied = repair_tag in door_tags
    if (
        (repair_applied and (len(repair_hashes) != 1 or not repair_hashes[0]))
        or (not repair_applied and repair_hashes)
    ):
        return None
    door_location = door.get_actor_location()
    door_rotation = door.get_actor_rotation()
    return {
        "snapshot": snapshot,
        "intent_hash": str(struct_property(intent, "IntentHash")),
        "manifest_hash": str(struct_property(manifest, "ManifestHash")),
        "anchor_topology_hash": str(struct_property(manifest, "AnchorTopologyHash")),
        "population_hash": str(struct_property(manifest, "PopulationHash")),
        "resource_hash": str(struct_property(manifest, "ResourceHash")),
        "candidate_anchor_count": int(struct_property(manifest, "CandidateAnchorCount")),
        "runtime": {
            "dungeon_count": len(dungeons),
            "door_count": len(doors),
            "runtime_pcg_component_count": len(runtime_pcg),
            "nav_bounds_count": len(nav_bounds),
            "recast_navmesh_count": len(recast),
            "door_enabled": True,
            "topology_repair_action": "repaired" if repair_applied else "none",
            "topology_repair_hash": repair_hashes[0] if repair_hashes else "",
            "door_transform": {
                "x": round(float(door_location.x), 3),
                "y": round(float(door_location.y), 3),
                "z": round(float(door_location.z), 3),
                "yaw": round(float(door_rotation.yaw), 3),
            },
        },
    }


def attempt_order_valid(attempt):
    ordinals = attempt["event_ordinals"]
    ordered = (
        ordinals["generate_local"],
        ordinals["pcg_complete"],
        ordinals["navigation_path_ready"],
        ordinals["population_realized"],
        ordinals["door_enabled"],
    )
    return (
        all(value is not None for value in ordered)
        and len(set(ordered)) == len(ordered)
        and list(ordered) == sorted(ordered)
    )


def finalize_physical_bounds(case):
    document = case["physical_bounds"]
    document["spatial_fingerprint"] = quantized_spatial_fingerprint(document)
    dungeon_bounds = document["dungeon_actor"].get("last_bounds")
    document["dungeon_bounds_observed"] = bool(
        dungeon_bounds
        and float(dungeon_bounds["size"]["x"]) > 0.0
        and float(dungeon_bounds["size"]["y"]) > 0.0
    )
    document["transient_anchors_observed"] = (
        int(document["anchors"].get("peak_actor_count", 0)) > 0
    )
    sample = case.get("sample") or {}
    document["candidate_anchors_proven_by_manifest"] = (
        int(sample.get("candidate_anchor_count", 0)) > 0
    )
    document["measurement_status"] = (
        "PASS" if (
            document["dungeon_bounds_observed"]
            and (
                document["transient_anchors_observed"]
                or document["candidate_anchors_proven_by_manifest"]
            )
        ) else "PENDING"
    )
    return document


def finalize_case(reason, sample=None):
    scan_log()
    case = STATE.current
    case["elapsed_seconds"] = round(time.monotonic() - case["started"], 3)
    case["reason"] = reason
    case["sample"] = sample
    finalize_physical_bounds(case)
    case["resolved"] = sample["snapshot"] if sample else case.get("last_snapshot")
    case["errors"] = list({(row["pattern"], row["line"]): row for row in case["errors"]}.values())
    case["generate_local_count"] = sum(row["generate_local_count"] for row in case["attempts"])
    case["one_generate_local_per_attempt"] = bool(case["attempts"]) and all(
        row["generate_local_count"] == 1 for row in case["attempts"]
    )
    final_attempt = case["attempts"][-1] if case["attempts"] else None
    case["timings_seconds"] = final_attempt["timings_seconds"] if final_attempt else {
        "open_level": None,
        "generate_local": None,
        "pcg_complete": None,
        "navigation_path_ready": None,
        "population_realized": None,
        "door_enabled": None,
    }
    exact_size = bool(case["resolved"]) and case["resolved"]["dungeon_size"] == {
        "x": case["edge"], "y": case["edge"], "z": 1
    }
    generation_ready_seconds = None
    if final_attempt:
        generated_at = final_attempt["timings_seconds"]["generate_local"]
        ready_at = final_attempt["timings_seconds"]["door_enabled"]
        if generated_at is not None and ready_at is not None:
            generation_ready_seconds = round(ready_at - generated_at, 3)
    case["generation_ready_seconds"] = generation_ready_seconds
    case["checks"] = {
        "request_accepted": bool(case["request_accepted"]),
        "exact_forced_size": exact_size,
        "single_attempt": len(case["attempts"]) == 1,
        "one_generate_local_per_attempt": case["one_generate_local_per_attempt"],
        "pcg_complete": bool(final_attempt and final_attempt["timings_seconds"]["pcg_complete"] is not None),
        "start_to_door_navigation_ready": bool(final_attempt and final_attempt["timings_seconds"]["navigation_path_ready"] is not None),
        "population_realized": bool(final_attempt and final_attempt["timings_seconds"]["population_realized"] is not None),
        "door_enabled": bool(final_attempt and final_attempt["timings_seconds"]["door_enabled"] is not None),
        "floor_ready_under_30_seconds": bool(
            generation_ready_seconds is not None and generation_ready_seconds < 30.0
        ),
        "telemetry_order": bool(final_attempt and attempt_order_valid(final_attempt)),
        "runtime_shape_valid": bool(sample),
        "physical_bounds_complete": (
            case["physical_bounds"]["measurement_status"] == "PASS"
        ),
        "no_forbidden_errors": not case["errors"],
    }
    case["success"] = all(case["checks"].values())
    del case["started"]
    STATE.results.append(case)
    STATE.current = None


def arm_candidate_validated_sizes(world):
    if STATE.candidate_policy_armed:
        return
    source_policy = unreal.load_asset(POLICY_OBJECT)
    candidate_hash_method = (
        getattr(source_policy, "get_policy_hash_with_validated_dungeon_sizes", None)
        if source_policy else None
    )
    if not callable(candidate_hash_method):
        raise RuntimeError("The V3 policy cannot compute the transient candidate hash")
    expected_hash = str(candidate_hash_method(list(SIZES))).upper()
    if not re.fullmatch(r"[0-9A-F]{64}", expected_hash):
        raise RuntimeError("The transient candidate policy produced an invalid expected hash")
    values = ",".join(str(edge) for edge in SIZES)
    unreal.SystemLibrary.execute_console_command(
        world, "{} {}".format(CANDIDATE_SIZES_COMMAND, values)
    )
    subsystem = find_game_instance_subsystem(world, STATE.subsystem_class)
    if not subsystem:
        raise RuntimeError("The Dungeon Director subsystem vanished while arming the candidate")
    observed_snapshot = snapshot_document(subsystem)
    observed_hash = str(observed_snapshot.get("policy_hash", "")).upper()
    if not observed_snapshot.get("policy_valid") or observed_hash != expected_hash:
        raise RuntimeError(
            "Candidate policy command was not accepted: expected hash {} observed {} valid={}".format(
                expected_hash, observed_hash, observed_snapshot.get("policy_valid")
            )
        )
    STATE.candidate_policy_armed = True
    STATE.candidate_policy_arm_count += 1
    STATE.candidate_policy_expected_hash = expected_hash
    STATE.candidate_policy_observed_hash = observed_hash
    STATE.candidate_policy_verified = True
    unreal.log(
        "[CalystoSizeMatrixPIE58] candidate_validated_dungeon_sizes=" + values
    )


def clear_candidate_validated_sizes():
    if not STATE.candidate_policy_armed or STATE.candidate_policy_clear_requested:
        return
    try:
        world = game_world()
        if not world:
            STATE.candidate_policy_clear_error = "No PIE world was available to clear"
            return
        unreal.SystemLibrary.execute_console_command(
            world, CANDIDATE_SIZES_COMMAND + " clear"
        )
        STATE.candidate_policy_clear_requested = True
        source_policy = unreal.load_asset(POLICY_OBJECT)
        source_hash_method = getattr(source_policy, "get_policy_hash", None) if source_policy else None
        source_validate_method = getattr(source_policy, "validate_policy", None) if source_policy else None
        subsystem = find_game_instance_subsystem(world, STATE.subsystem_class)
        if (not callable(source_hash_method) or not callable(source_validate_method)
                or not subsystem):
            raise RuntimeError("Source policy or subsystem unavailable after candidate clear")
        source_is_valid = bool(source_validate_method())
        expected_source_hash = str(source_hash_method()).upper() if source_is_valid else ""
        cleared_snapshot = snapshot_document(subsystem)
        observed_source_hash = str(cleared_snapshot.get("policy_hash", "")).upper()
        if (bool(cleared_snapshot.get("policy_valid")) != source_is_valid
                or observed_source_hash != expected_source_hash):
            raise RuntimeError(
                "Candidate policy clear was not accepted: expected source hash {} observed {} valid={}".format(
                    expected_source_hash,
                    observed_source_hash,
                    cleared_snapshot.get("policy_valid"),
                )
            )
        STATE.candidate_policy_clear_verified = True
        unreal.log("[CalystoSizeMatrixPIE58] candidate_validated_dungeon_sizes=clear")
    except Exception as exc:
        STATE.candidate_policy_clear_error = str(exc)


def begin_next_case(world):
    STATE.case_index += 1
    if STATE.case_index >= len(STATE.cases):
        set_phase("complete")
        finish()
        return
    definition = STATE.cases[STATE.case_index]
    STATE.current = {
        **definition,
        "started": time.monotonic(),
        "request_accepted": False,
        "saw_dungeon_world": False,
        "attempts": [],
        "errors": [],
        "last_snapshot": None,
        "physical_bounds": new_spatial_observation(),
    }
    STATE.log_offset = LOG_FILE.stat().st_size if LOG_FILE.is_file() else 0
    STATE.log_partial = b""
    STATE.case_log_line_ordinal = 0
    subsystem = find_game_instance_subsystem(world, STATE.subsystem_class)
    if not subsystem:
        raise RuntimeError("Dungeon Director subsystem unavailable in HUB")
    unreal.SystemLibrary.execute_console_command(
        world, "{} {}".format(FORCE_COMMAND, definition["edge"])
    )
    accepted = bool(reflected(subsystem, "request_start_new_run_with_seed")(definition["run_seed"]))
    STATE.current["request_accepted"] = accepted
    if not accepted:
        finalize_case("new_run_rejected")
        set_phase("wait_hub_between_cases")
        return
    set_phase("wait_case_{}_edge_{}_seed_{}".format(
        STATE.case_index, definition["edge"], definition["run_seed"]
    ))


def topology_group_document(results):
    groups = {}
    for row in results:
        sample = row.get("sample") or {}
        topology_hash = sample.get("anchor_topology_hash")
        if not topology_hash:
            continue
        member = {
            "edge": row["edge"],
            "run_seed": row["run_seed"],
            "seed_index": row["seed_index"],
            "spatial_fingerprint": row["physical_bounds"].get(
                "spatial_fingerprint", ""
            ),
            "dungeon_bounds": row["physical_bounds"]["dungeon_actor"].get(
                "last_bounds"
            ),
            "anchor_bounds": row["physical_bounds"]["anchors"].get(
                "union_bounds"
            ),
        }
        groups.setdefault(topology_hash, []).append(member)
    result = []
    for topology_hash, members in sorted(groups.items()):
        edges = sorted({member["edge"] for member in members})
        fingerprints = sorted({
            member["spatial_fingerprint"] for member in members
            if member["spatial_fingerprint"]
        })
        result.append({
            "anchor_topology_hash": topology_hash,
            "member_count": len(members),
            "edges": edges,
            "is_cross_size_alias": len(edges) > 1,
            "all_measured_physical_bounds_equal": (
                len(fingerprints) == 1
                and all(member["spatial_fingerprint"] for member in members)
            ),
            "physical_fingerprints": fingerprints,
            "members": members,
        })
    return result


def physical_alias_group_document(results):
    groups = {}
    for row in results:
        fingerprint = row.get("physical_bounds", {}).get("spatial_fingerprint", "")
        if not fingerprint:
            continue
        sample = row.get("sample") or {}
        groups.setdefault(fingerprint, []).append({
            "edge": row["edge"],
            "run_seed": row["run_seed"],
            "seed_index": row["seed_index"],
            "anchor_topology_hash": sample.get("anchor_topology_hash", ""),
        })
    result = []
    for fingerprint, members in sorted(groups.items()):
        edges = sorted({member["edge"] for member in members})
        if len(edges) < 2:
            continue
        result.append({
            "spatial_fingerprint": fingerprint,
            "edges": edges,
            "member_count": len(members),
            "anchor_topology_hashes": sorted({
                member["anchor_topology_hash"] for member in members
                if member["anchor_topology_hash"]
            }),
            "members": members,
        })
    return result


def finish():
    if STATE.finished:
        return
    STATE.finished = True
    clear_candidate_validated_sizes()
    STATE.final_content = content_snapshot()
    STATE.final_dirty, STATE.final_dirty_probe = dirty_states()
    STATE.final_protected = protected_hashes()
    STATE.global_log_errors = global_log_error_document()
    asset_saves = sorted(
        "/Game/" + name.rsplit(".", 1)[0]
        for name in set(STATE.initial_content) | set(STATE.final_content)
        if STATE.initial_content.get(name) != STATE.final_content.get(name)
    )
    newly_dirty = sorted(
        name for name, dirty in STATE.final_dirty.items()
        if dirty is True and STATE.initial_dirty.get(name) is not True
    )
    unknown_dirty_states = sorted(
        name for name in MONITORED_OBJECTS
        if STATE.initial_dirty.get(name) is None or STATE.final_dirty.get(name) is None
    )
    preexisting_dirty_states = sorted(
        name for name in MONITORED_OBJECTS if STATE.initial_dirty.get(name) is True
    )
    dirty_state_transitions = [
        {
            "object_path": name,
            "initial": STATE.initial_dirty.get(name),
            "final": STATE.final_dirty.get(name),
        }
        for name in MONITORED_OBJECTS
        if (
            STATE.initial_dirty.get(name) is not None
            and STATE.final_dirty.get(name) is not None
            and STATE.initial_dirty.get(name) != STATE.final_dirty.get(name)
        )
    ]
    dirty_state_gate = (
        "FAIL" if (newly_dirty or dirty_state_transitions)
        else "PENDING" if (unknown_dirty_states or preexisting_dirty_states)
        else "PASS"
    )
    protected_mismatches = sorted(
        name for name in STATE.initial_protected
        if STATE.initial_protected[name] != STATE.final_protected.get(name)
    )
    summaries = {}
    for edge in SIZES:
        rows = [row for row in STATE.results if row["edge"] == edge]
        requested_rows = [row for row in STATE.cases if row["edge"] == edge]
        unique_seed_count = len({row["run_seed"] for row in rows})
        passed_requested_matrix = (
            bool(rows)
            and len(rows) == len(requested_rows)
            and all(row["success"] for row in rows)
        )
        summaries[str(edge)] = {
            "requested_seed_count": len(requested_rows),
            "completed_seed_count": len(rows),
            "unique_seed_count": unique_seed_count,
            "passed_seed_count": sum(1 for row in rows if row["success"]),
            "failed_seed_count": sum(1 for row in rows if not row["success"]),
            "passed_requested_matrix": passed_requested_matrix,
            "certified": (
                passed_requested_matrix
                and unique_seed_count >= CERTIFICATION_MIN_UNIQUE_SEEDS
            ),
        }
    policy_hashes = sorted({
        row["resolved"]["policy_hash"]
        for row in STATE.results if row.get("resolved") and row["resolved"].get("policy_hash")
    })
    topology_groups = topology_group_document(STATE.results)
    physical_alias_groups = physical_alias_group_document(STATE.results)
    certified_sizes = [
        int(edge) for edge, row in summaries.items() if row["certified"]
    ]
    success = (
        len(STATE.results) == len(STATE.cases)
        and all(row["success"] for row in STATE.results)
        and not asset_saves
        and dirty_state_gate == "PASS"
        and not protected_mismatches
        and len(policy_hashes) == 1
        and STATE.global_log_errors.get("gate_status") == "PASS"
        and STATE.candidate_policy_armed
        and STATE.candidate_policy_arm_count == 1
        and STATE.candidate_policy_verified
        and STATE.candidate_policy_clear_requested
        and STATE.candidate_policy_clear_verified
    )
    document = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "PASS" if success else "FAIL",
        "success": success,
        "contract": {
            "configuration": "Development Editor unattended PIE only",
            "shipping_override": "not compiled; command and setter are guarded by !UE_BUILD_SHIPPING",
            "normal_runtime": "continues to select only from policy ValidatedDungeonSizes",
            "command": FORCE_COMMAND,
            "candidate_validated_sizes_command": CANDIDATE_SIZES_COMMAND,
            "candidate_command_executed_once_before_first_new_run": (
                STATE.candidate_policy_armed
                and STATE.candidate_policy_arm_count == 1
            ),
            "candidate_command_execution_count": STATE.candidate_policy_arm_count,
            "candidate_command_verified_by_policy_hash": (
                STATE.candidate_policy_verified
            ),
            "candidate_command_clear_requested": (
                STATE.candidate_policy_clear_requested
            ),
            "candidate_command_clear_verified_by_source_hash": (
                STATE.candidate_policy_clear_verified
            ),
            "candidate_command_clear_error": STATE.candidate_policy_clear_error,
            "event_order_method": (
                "strictly increasing case-relative completed log-line ordinals"
            ),
            "physical_bounds_gate": (
                "non-zero dungeon actor bounds plus candidate anchors observed "
                "transiently or proven by the finalized native manifest"
            ),
            "case_order": CASE_ORDER,
            "certification_min_unique_seeds": CERTIFICATION_MIN_UNIQUE_SEEDS,
            "floor_ready_limit_seconds_exclusive": 30.0,
        },
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "requested_sizes": list(SIZES),
        "case_order": CASE_ORDER,
        "seeds_per_size": len(EXPLICIT_SEEDS) if EXPLICIT_SEEDS else SEEDS_PER_SIZE,
        "case_timeout_seconds": CASE_TIMEOUT_SECONDS,
        "case_count": len(STATE.cases),
        "completed_case_count": len(STATE.results),
        "screened_sizes": [
            int(edge) for edge, row in summaries.items() if row["passed_requested_matrix"]
        ],
        "certified_sizes": certified_sizes,
        "size_summary": summaries,
        "topology_groups": topology_groups,
        "cross_size_topology_aliases": [
            row for row in topology_groups if row["is_cross_size_alias"]
        ],
        "cross_size_physical_bounds_aliases": physical_alias_groups,
        "cases": STATE.results,
        "policy": {
            **policy_document(),
            "candidate_validated_dungeon_sizes": sorted(int(edge) for edge in SIZES),
            "candidate_policy_hash": STATE.candidate_policy_expected_hash,
            "candidate_policy_observed_hash": STATE.candidate_policy_observed_hash,
            "policy_hashes": policy_hashes,
            "initial": STATE.initial_policy,
        },
        "asset_mutations": sorted(set(asset_saves) | set(newly_dirty)),
        "asset_saves": asset_saves,
        "asset_monitor": {
            "method": (
                "all Content package size/mtime delta plus fail-closed dirty-state "
                "inventory for monitored V3/Calysto packages"
            ),
            "dirty_state_gate": dirty_state_gate,
            "unknown_dirty_states": unknown_dirty_states,
            "preexisting_dirty_states": preexisting_dirty_states,
            "dirty_state_transitions": dirty_state_transitions,
            "initial_dirty_probe": STATE.initial_dirty_probe,
            "final_dirty_probe": STATE.final_dirty_probe,
            "initial_package_count": len(STATE.initial_content),
            "final_package_count": len(STATE.final_content),
            "initial_dirty_states": STATE.initial_dirty,
            "final_dirty_states": STATE.final_dirty,
        },
        "protected_assets": {
            "method": "SHA-256 and byte length before/after the same Editor session",
            "before": STATE.initial_protected,
            "after": STATE.final_protected,
            "mismatches": protected_mismatches,
        },
        "global_log_errors": STATE.global_log_errors,
        "phase_history": STATE.phase_history,
        "error": STATE.error,
    }
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("[CalystoSizeMatrixPIE58] result=" + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
    builtins._codex_calysto_size_matrix_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail_fatal(message):
    STATE.error = message
    finish()


def tick(_delta_seconds):
    try:
        if STATE.finished:
            return
        maximum_seconds = len(STATE.cases) * (CASE_TIMEOUT_SECONDS + 15.0) + 300.0
        if elapsed() > maximum_seconds:
            fail_fatal("Global matrix timeout in phase " + STATE.phase)
            return

        if STATE.phase == "load_map":
            if not SIZES or any(edge < 18 or edge > 30 for edge in SIZES):
                fail_fatal("Matrix sizes must be within 18..30")
                return
            if CASE_ORDER not in VALID_CASE_ORDERS:
                fail_fatal(
                    "Matrix case order must be one of " + ", ".join(VALID_CASE_ORDERS)
                )
                return
            if not STATE.cases or any(row["run_seed"] <= 0 for row in STATE.cases):
                fail_fatal("Matrix requires positive seeds")
                return
            STATE.subsystem_class = unreal.load_class(None, SUBSYSTEM_CLASS)
            STATE.door_class = unreal.load_class(None, DOOR_CLASS)
            STATE.dungeon_class = unreal.load_class(None, DUNGEON_CLASS)
            STATE.anchor_class = unreal.load_class(None, ANCHOR_CLASS)
            STATE.player_start_class = unreal.load_class(None, PLAYER_START_CLASS)
            STATE.nav_bounds_class = unreal.load_class(None, NAV_BOUNDS_CLASS)
            STATE.recast_class = unreal.load_class(None, RECAST_CLASS)
            if not all((STATE.subsystem_class, STATE.door_class, STATE.dungeon_class,
                        STATE.anchor_class, STATE.player_start_class,
                        STATE.nav_bounds_class, STATE.recast_class)):
                fail_fatal("A required V3 runtime class did not load")
                return
            if not POLICY_FILE.is_file() or unreal.load_asset(POLICY_OBJECT) is None:
                fail_fatal("The sole V3 policy is unavailable")
                return
            set_phase("load_hub")
            if LEVEL_EDITOR.load_level(CONTROL_MAP) is False:
                fail_fatal("HUB control map failed to load")
                return
            set_phase("wait_editor_hub")
            return

        if STATE.phase == "wait_editor_hub":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if canonical_world_path(editor_world).lower() != CONTROL_WORLD.lower():
                if phase_age() > 120.0:
                    fail_fatal("HUB editor world did not become ready")
                return
            if phase_age() < 3.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            set_phase("wait_initial_pie_hub")
            return

        if STATE.phase in ("wait_initial_pie_hub", "wait_hub_between_cases"):
            world = game_world()
            if (LEVEL_EDITOR.is_in_play_in_editor() and world
                    and canonical_world_path(world).lower() == CONTROL_WORLD.lower()
                    and world_time(world) >= 1.0):
                if STATE.phase == "wait_initial_pie_hub":
                    arm_candidate_validated_sizes(world)
                begin_next_case(world)
                return
            if phase_age() > 120.0:
                fail_fatal("PIE HUB did not become ready between matrix cases")
            return

        if STATE.phase == "wait_pie_restart_end":
            if not LEVEL_EDITOR.is_in_play_in_editor():
                LEVEL_EDITOR.editor_request_begin_play()
                set_phase("wait_hub_between_cases")
            return

        if STATE.phase.startswith("wait_case_"):
            scan_log()
            world = game_world()
            if world and canonical_world_path(world).lower() == DUNGEON_WORLD.lower():
                STATE.current["saw_dungeon_world"] = True
                capture_spatial_observation(world)
                subsystem = find_game_instance_subsystem(world, STATE.subsystem_class)
                if subsystem and world_time(world) >= 1.0:
                    observed_snapshot = snapshot_document(subsystem)
                    if observed_snapshot["run_seed"] == STATE.current["run_seed"]:
                        STATE.current["last_snapshot"] = observed_snapshot
                    sample = runtime_ready_sample(world, subsystem)
                    final_attempt = STATE.current["attempts"][-1] if STATE.current["attempts"] else None
                    if sample and final_attempt and attempt_order_valid(final_attempt):
                        finalize_case("door_ready", sample)
                        unreal.GameplayStatics.open_level(world, unreal.Name(CONTROL_MAP), True, "")
                        set_phase("wait_hub_between_cases")
                        return
            elif (world and STATE.current["saw_dungeon_world"]
                  and canonical_world_path(world).lower() == CONTROL_WORLD.lower()):
                finalize_case("safe_return_to_hub_before_door_ready")
                set_phase("wait_hub_between_cases")
                return
            if time.monotonic() - STATE.current["started"] > CASE_TIMEOUT_SECONDS:
                finalize_case("case_timeout")
                if LEVEL_EDITOR.is_in_play_in_editor():
                    EDITOR_LEVEL.editor_end_play()
                    set_phase("wait_pie_restart_end")
                else:
                    set_phase("wait_hub_between_cases")
                return
    except Exception as exc:
        fail_fatal("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_size_matrix_pie58", None)
if existing is not None:
    try:
        if getattr(existing, "callback", None) is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_size_matrix_pie58 = STATE
unreal.log("[CalystoSizeMatrixPIE58] Development exact-size validator registered")
