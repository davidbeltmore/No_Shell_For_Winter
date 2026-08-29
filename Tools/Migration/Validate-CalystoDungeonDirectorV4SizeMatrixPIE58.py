"""Strict 26..30 exact-size PIE matrix for Dungeon Director V4.

The validator keeps one unattended Development PIE GameInstance alive, arms
one transient candidate-size policy, and runs exactly twenty deterministic
New Runs for each edge in 26..30.  It never saves Content.  Every sample proves
the finalized V4 manifest, the live population, the Start-to-Door NavMesh path,
the single Calysto PCG request, readiness ordering, and protected-file safety.
"""

import builtins
import datetime
import hashlib
import json
import math
import os
import re
import time
import traceback
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir()).resolve()
CONTENT_DIR = Path(unreal.Paths.project_content_dir()).resolve()
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V4_SIZE_MATRIX_OUTPUT"]).resolve()
LOG_FILE = Path(os.environ["CODEX_CALYSTO_V4_SIZE_MATRIX_LOG"]).resolve()
BASE_SEED = int(os.environ.get("CODEX_CALYSTO_V4_SIZE_MATRIX_BASE_SEED", "202608212600"))
CASE_TIMEOUT_SECONDS = float(
    os.environ.get("CODEX_CALYSTO_V4_SIZE_MATRIX_CASE_TIMEOUT", "100")
)

SIZES = (26, 27, 28, 29, 30)
SEEDS_PER_SIZE = 20
EXPECTED_CASES = len(SIZES) * SEEDS_PER_SIZE
FLOOR_READY_ABSOLUTE_LIMIT_SECONDS = 30.0
COMPARABLE_V3_P95_SECONDS = 20.609
P95_RATIO_LIMIT = 1.25
P95_LIMIT_SECONDS = COMPARABLE_V3_P95_SECONDS * P95_RATIO_LIMIT
BP_MASSIVE_DUNGEON_BASELINE = (
    "47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B"
)

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = (
    CONTENT_DIR
    / "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
)
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
PCG_SUBSYSTEM_CLASS = "/Script/EFProceduralPCGRuntime.EFProceduralPCGSubsystem"
DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
)
START_POINT_CLASS = (
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.BP_StartPoint_C"
)
ANCHOR_CLASS = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
NAV_BOUNDS_CLASS = "/Script/NavigationSystem.NavMeshBoundsVolume"
RECAST_NAVMESH_CLASS = "/Script/NavigationSystem.RecastNavMesh"
FORCE_COMMAND = "EF.Calysto.Automation.ForceDungeonEdge"
CANDIDATE_SIZES_COMMAND = "EF.Calysto.Automation.SetCandidateValidatedSizes"

POPULATION_TAG = "EF.Calysto.Population.V4"
CATEGORY_TAGS = {
    "enemy": "EF.Calysto.V4.Category.Enemy",
    "npc": "EF.Calysto.V4.Category.NPC",
    "food": "EF.Calysto.V4.Category.Food",
    "chest": "EF.Calysto.V4.Category.Chest",
    "loose_loot": "EF.Calysto.V4.Category.LooseLoot",
    "clothing": "EF.Calysto.V4.Category.Clothing",
    "special_event": "EF.Calysto.V4.Category.SpecialEvent",
}
CATEGORY_CAPS = {
    "enemy": 25,
    "npc": 4,
    "food": 30,
    "chest": 10,
    "loose_loot": 4,
    "clothing": 10,
    "special_event": 6,
}
INITIAL_ACTOR_CAP = 89

MONITORED_OBJECTS = (
    POLICY_OBJECT,
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
    "/Game/FullSample/Player",
    "/Game/DazToUnreal/Female/Female",
    "/Game/DazToUnreal/Male/Male",
    "/Game/DazToUnreal/Multiple/Multiple",
)
PROTECTED_FILES = (
    "Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset",
    "Content/Calysto/Dungeon/Blueprint/Utility/BP_EndPoint.uasset",
    "Content/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine.uasset",
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
    "Content/DazToUnreal/Male/Male.uasset",
    "Content/DazToUnreal/Multiple/Multiple.uasset",
    "Plugins/EFCharacterCreationDazBridge/EFCharacterCreationDazBridge.uplugin",
)

FORBIDDEN_LOG_PATTERNS = (
    "Blueprint Runtime Error",
    "LogBlueprint: Error",
    "Accessed None",
    "Ensure condition failed",
    "Fatal error:",
    "Assertion failed:",
    "Object Transform",
    "GetAttributeFromPointIndex_0",
    "cancelled or cleaned",
    "GenerateLocal was already requested",
    "duplicate generation",
    "duplicated generation",
    "FLOOR_READY_REJECTED",
    "COMPANION_SNAPSHOT_DRIFT",
    "Calysto controlled generation failed",
    "Calysto generation failed",
    "LogPCG: Error",
    "LogEFCalystoDungeon: Error",
    "LogEFCalystoPopulationV4: Error",
    "LogEFProceduralPCGRuntime: Error",
    "LogProjectRunCompanions: Error",
    "LogEFCalystoFloorDoor: Error",
    "LogEFCalystoFloorDoor: Warning",
    "PolicyV3",
    "PlanV3",
    "THEME_V3",
    "CALYSTO_PHASE2",
)
GLOBAL_ERROR_MARKERS = (
    ("log_error", re.compile(r"\bLog[^:\]\s]+:\s+Error:", re.IGNORECASE)),
    ("blueprint_runtime_error", re.compile(r"Blueprint Runtime Error", re.IGNORECASE)),
    ("ensure", re.compile(r"Ensure condition failed", re.IGNORECASE)),
    ("fatal", re.compile(r"Fatal error:", re.IGNORECASE)),
    ("assertion", re.compile(r"Assertion failed:", re.IGNORECASE)),
)
KNOWN_NON_ACTIONABLE_ERRORS = ("LogTemp: Error: Can't Start the quest",)

READINESS_EVENTS = (
    "generate_local",
    "pcg_complete",
    "navigation_path_ready",
    "enemy_levels_ready",
    "population_realized",
    "companion_roster_ready",
    "door_enabled",
)

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def object_path(value):
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return ""


def canonical_world(world):
    return re.sub(r"uedpie_\d+_", "", object_path(world), flags=re.IGNORECASE)


def game_world():
    return EDITOR_LEVEL_LIBRARY.get_game_world()


def world_time(world):
    return float(unreal.GameplayStatics.get_time_seconds(world)) if world else 0.0


def object_identity(value):
    try:
        return int(hash(value)) if value else 0
    except Exception:
        return int(id(value)) if value else 0


def property_candidates(name):
    first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower()
    result = [name, snake, name[0].lower() + name[1:]]
    if len(name) > 1 and name[0] == "b" and name[1].isupper():
        stripped = name[1:]
        first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", stripped)
        result.append(re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower())
    return list(dict.fromkeys(result))


def prop(owner, name):
    for candidate in property_candidates(name):
        getter = getattr(owner, "get_editor_property", None)
        if callable(getter):
            try:
                return getter(candidate)
            except Exception:
                pass
        try:
            return getattr(owner, candidate)
        except Exception:
            pass
    raise RuntimeError(
        "Missing reflected property {} on {}".format(name, object_path(owner))
    )


def reflected(owner, name):
    method = getattr(owner, name, None)
    if not callable(method):
        raise RuntimeError(
            "Missing reflected method {} on {}".format(name, object_path(owner))
        )
    return method


def enum_name(value):
    name = getattr(value, "name", None)
    return str(name if name else value).split(".")[-1]


def normalized(value):
    return re.sub(r"[^a-z0-9]", "", enum_name(value).lower())


def state_ready(snapshot):
    token = normalized(prop(snapshot, "State"))
    return "ready" in token


def actors_of_class(world, actor_class):
    if not world or not actor_class:
        return []
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def actors_with_tag(world, tag):
    if not world:
        return []
    return list(unreal.GameplayStatics.get_all_actors_with_tag(world, tag))


def component_is_editor_only(component):
    method = getattr(component, "is_editor_only", None)
    if callable(method):
        try:
            return bool(method())
        except Exception:
            pass
    for name in ("bIsEditorOnly", "IsEditorOnly"):
        try:
            return bool(prop(component, name))
        except Exception:
            pass
    return False


def find_subsystem(world, subsystem_class):
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
            if (
                candidate.get_class() == subsystem_class
                and object_path(candidate).startswith(prefix)
            ):
                return candidate
        except Exception:
            pass
    return None


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_sha256(value):
    return bool(re.fullmatch(r"[0-9A-Fa-f]{64}", str(value)))


def finite_nonnegative(value):
    number = float(value)
    return math.isfinite(number) and number >= 0.0


def protected_hashes():
    result = {}
    for relative in PROTECTED_FILES:
        path = PROJECT_DIR / relative
        result[relative] = {
            "exists": path.is_file(),
            "length": path.stat().st_size if path.is_file() else None,
            "sha256": sha256(path) if path.is_file() else None,
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


def dirty_package_snapshot():
    packages = []
    utility = unreal.EditorLoadingAndSavingUtils
    for method_name in ("get_dirty_content_packages", "get_dirty_map_packages"):
        method = getattr(utility, method_name, None)
        if not callable(method):
            raise RuntimeError("Missing Unreal dirty-package API " + method_name)
        packages.extend(list(method()))
    names = sorted({object_path(package) for package in packages if object_path(package)})
    monitored = {}
    dirty_names = set(names)
    for object_name in MONITORED_OBJECTS:
        asset = unreal.load_asset(object_name)
        if not asset:
            monitored[object_name] = None
        else:
            monitored[object_name] = object_path(asset.get_outermost()) in dirty_names
    if any(value is None for value in monitored.values()):
        raise RuntimeError("A monitored package could not be loaded for dirty-state evidence")
    return {"all_dirty_packages": names, "monitored": monitored}


def native_policy_document(policy):
    policy_class = unreal.load_class(None, POLICY_CLASS)
    if not policy or not policy_class or policy.get_class() != policy_class:
        raise RuntimeError("The authored policy is not the exact native V4 class")
    validation = reflected(policy, "validate_policy")()
    if isinstance(validation, tuple):
        valid = bool(validation[0]) if validation else False
        error = str(validation[1]) if len(validation) > 1 else ""
    else:
        valid = bool(validation)
        error = ""
    if not valid:
        raise RuntimeError("Native V4 policy validation failed: " + error)
    document = {
        "object_path": POLICY_OBJECT,
        "class": object_path(policy.get_class()),
        "schema_version": int(prop(policy, "SchemaVersion")),
        "generator_version": int(prop(policy, "GeneratorVersion")),
        "policy_id": str(prop(policy, "PolicyId")),
        "policy_hash": str(reflected(policy, "get_policy_hash")()).upper(),
        "validated_dungeon_sizes": sorted(
            int(value) for value in list(prop(policy, "ValidatedDungeonSizes"))
        ),
        "uasset_path": str(POLICY_FILE),
        "uasset_sha256": sha256(POLICY_FILE) if POLICY_FILE.is_file() else "",
    }
    if (
        document["object_path"] != POLICY_OBJECT
        or document["class"] != POLICY_CLASS
        or document["schema_version"] != 4
        or document["generator_version"] != 4
        or document["policy_id"] != "CalystoDungeonDirectorV4"
        or document["validated_dungeon_sizes"] != list(SIZES)
        or not is_sha256(document["policy_hash"])
        or not is_sha256(document["uasset_sha256"])
    ):
        raise RuntimeError("The authored V4 policy identity/size/hash contract is invalid")
    return document


def category_key(value):
    token = normalized(value)
    if "decoration" in token:
        return "decoration"
    if "lighting" in token:
        return "lighting"
    if "specialevent" in token:
        return "special_event"
    if "looseloot" in token:
        return "loose_loot"
    for key in ("enemy", "npc", "food", "chest", "clothing"):
        if key in token:
            return key
    return ""


def snapshot_document(director):
    snapshot = reflected(director, "get_snapshot")()
    size = prop(snapshot, "DungeonSize")
    return {
        "policy_valid": bool(prop(snapshot, "bPolicyValid")),
        "policy_error": str(prop(snapshot, "PolicyError")),
        "has_active_run": bool(prop(snapshot, "bHasActiveRun")),
        "run_seed": int(prop(snapshot, "RunSeed")),
        "floor_number": int(prop(snapshot, "FloorNumber")),
        "generation_serial": int(prop(snapshot, "GenerationSerial")),
        "pcg_seed": int(prop(snapshot, "PCGSeed")),
        "dungeon_size": [
            int(prop(size, "X")),
            int(prop(size, "Y")),
            int(prop(size, "Z")),
        ],
        "forced_edge": int(prop(snapshot, "DevelopmentForcedDungeonEdge")),
        "policy_hash": str(prop(snapshot, "PolicyHash")).upper(),
        "ecology_hash": str(prop(snapshot, "EcologyHash")).upper(),
        "intent_hash": str(prop(snapshot, "IntentHash")).upper(),
        "manifest_hash": str(prop(snapshot, "ManifestHash")).upper(),
        "companion_hash": str(prop(snapshot, "CompanionSnapshotHash")).upper(),
        "state": enum_name(prop(snapshot, "State")),
        "travel_kind": enum_name(prop(snapshot, "TravelKind")),
        "door_ready": bool(prop(snapshot, "bDoorReady")),
        "companion_ready": bool(prop(snapshot, "bCompanionReady")),
        "pending_floor": int(prop(snapshot, "PendingFloorNumber")),
        "pending_serial": int(prop(snapshot, "PendingGenerationSerial")),
        "failure_code": enum_name(prop(snapshot, "FailureCode")),
        "failure_message": str(prop(snapshot, "FailureMessage")),
    }


def hash_document(intent, manifest):
    return {
        "policy": str(prop(intent, "PolicyHash")).upper(),
        "ecology": str(prop(intent, "EcologyHash")).upper(),
        "intent": str(prop(intent, "IntentHash")).upper(),
        "anchor_topology": str(prop(manifest, "AnchorTopologyHash")).upper(),
        "population": str(prop(manifest, "PopulationHash")).upper(),
        "resource": str(prop(manifest, "ResourceHash")).upper(),
        "companion": str(prop(manifest, "CompanionSnapshotHash")).upper(),
        "manifest": str(prop(manifest, "ManifestHash")).upper(),
    }


def door_enabled(door):
    for name in ("bIsEnabled", "IsEnabled"):
        try:
            return bool(prop(door, name))
        except Exception:
            pass
    raise RuntimeError("The V4 floor door does not expose its enabled state")


def vector_document(value):
    return {
        "x": round(float(value.x), 3),
        "y": round(float(value.y), 3),
        "z": round(float(value.z), 3),
    }


def navigation_path_document(pcg_subsystem, start, door):
    document = {
        "valid": False,
        "partial": None,
        "point_count": None,
        "projected_start": None,
        "projected_door": None,
        "door_endpoint_distance_2d": None,
        "start_actor_location": vector_document(start.get_actor_location()),
        "door_actor_location": vector_document(door.get_actor_location()),
        "method": "UEFProceduralPCGSubsystem::HasCurrentDungeonNavigationPath",
        "error": "",
    }
    try:
        if not pcg_subsystem:
            raise RuntimeError("The project-owned PCG runtime subsystem is unavailable")
        document["valid"] = bool(
            reflected(pcg_subsystem, "has_current_dungeon_navigation_path")()
        )
        document["partial"] = False if document["valid"] else None
        document["projected_start"] = document["valid"]
        document["projected_door"] = document["valid"]
        if not document["valid"]:
            document["error"] = "The native Start-to-Door readiness query returned false"
    except Exception as exc:
        document["error"] = str(exc)
    return document


def make_cases():
    definitions = []
    ordered_sizes = list(SIZES)
    for seed_index in range(SEEDS_PER_SIZE):
        shift = int((BASE_SEED + seed_index) % len(ordered_sizes))
        per_seed = ordered_sizes[shift:] + ordered_sizes[:shift]
        if seed_index % 2:
            per_seed = list(reversed(per_seed))
        for edge in per_seed:
            definitions.append(
                {
                    "edge": edge,
                    "seed_index": seed_index,
                    "run_seed": BASE_SEED + edge * 1000 + seed_index,
                }
            )
    return definitions


def new_event_state():
    return {
        "counts": {name: 0 for name in READINESS_EVENTS},
        "ordinals": {name: None for name in READINESS_EVENTS},
        "seconds": {name: None for name in READINESS_EVENTS},
        "lines": {name: [] for name in READINESS_EVENTS},
    }


class State:
    def __init__(self):
        self.started = time.monotonic()
        self.phase_started = self.started
        self.phase = "load_map"
        self.callback = None
        self.finished = False
        self.error = ""
        self.classes = {}
        self.policy = {}
        self.cases = make_cases()
        self.case_index = -1
        self.current = None
        self.results = []
        self.log_offset = 0
        self.log_partial = b""
        self.log_ordinal = 0
        self.candidate_armed = False
        self.candidate_arm_count = 0
        self.candidate_hash = ""
        self.candidate_clear_requested = False
        self.candidate_clear_verified = False
        self.candidate_clear_error = ""
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_package_snapshot()
        self.initial_protected = protected_hashes()
        self.policy_sha_before = sha256(POLICY_FILE) if POLICY_FILE.is_file() else ""
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
    unreal.log("CALYSTO_V4_SIZE_MATRIX phase={}".format(value))


def record_event(name, line):
    event = STATE.current["events"]
    event["counts"][name] += 1
    event["lines"][name].append(line)
    if event["ordinals"][name] is None:
        event["ordinals"][name] = STATE.log_ordinal
        event["seconds"][name] = round(
            time.monotonic() - STATE.current["started"], 3
        )


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
    markers = (
        ("generate_local", "adapter requested generatelocal exactly once"),
        ("pcg_complete", "pcgcomplete world="),
        ("navigation_path_ready", "navigationpathready world="),
        ("enemy_levels_ready", "enemylevelsready world="),
        ("population_realized", "populationrealized world="),
        ("companion_roster_ready", "companionrosterready world="),
        ("door_enabled", "doorenabled world="),
    )
    for raw in lines:
        STATE.log_ordinal += 1
        line = raw.decode("utf-8", errors="replace").rstrip("\r")
        lower = line.lower()
        for event_name, token in markers:
            if token in lower:
                record_event(event_name, line)
        for pattern in FORBIDDEN_LOG_PATTERNS:
            if pattern.lower() in lower:
                STATE.current["blocked_log"].append(
                    {"pattern": pattern, "line": line}
                )


def readiness_complete_and_ordered(events):
    counts = events["counts"]
    ordinals = [events["ordinals"][name] for name in READINESS_EVENTS]
    return (
        all(counts[name] == 1 for name in READINESS_EVENTS)
        and all(value is not None for value in ordinals)
        and ordinals == sorted(ordinals)
        and len(set(ordinals)) == len(ordinals)
    )


def global_log_document():
    document = {
        "method": "whole ABSLOG regex and blocked-token scan",
        "gate_status": "PASS",
        "actionable_count": 0,
        "known_non_actionable_count": 0,
        "entries": [],
    }
    if not LOG_FILE.is_file():
        document["gate_status"] = "FAIL"
        document["error"] = "The ABSLOG file is unavailable"
        return document
    with LOG_FILE.open("r", encoding="utf-8", errors="replace") as stream:
        for line_number, raw in enumerate(stream, 1):
            line = raw.rstrip("\r\n")
            markers = [name for name, regex in GLOBAL_ERROR_MARKERS if regex.search(line)]
            blocked = [pattern for pattern in FORBIDDEN_LOG_PATTERNS if pattern.lower() in line.lower()]
            if not markers and not blocked:
                continue
            known = any(token.lower() in line.lower() for token in KNOWN_NON_ACTIONABLE_ERRORS)
            document["entries"].append(
                {
                    "line_number": line_number,
                    "markers": markers,
                    "blocked_patterns": blocked,
                    "known_non_actionable": known,
                    "line": line,
                }
            )
            if known and not blocked:
                document["known_non_actionable_count"] += 1
            else:
                document["actionable_count"] += 1
    if document["actionable_count"]:
        document["gate_status"] = "FAIL"
    return document


def arm_candidate_sizes(world):
    if STATE.candidate_armed:
        raise RuntimeError("The V4 candidate policy was about to be armed twice")
    director = find_subsystem(world, STATE.classes["director"])
    if not director:
        raise RuntimeError("Dungeon Director subsystem is unavailable in the HUB")
    unreal.SystemLibrary.execute_console_command(
        world,
        "{} {}".format(CANDIDATE_SIZES_COMMAND, ",".join(str(x) for x in SIZES)),
    )
    snapshot = snapshot_document(director)
    if (
        not snapshot["policy_valid"]
        or snapshot["policy_error"]
        or not is_sha256(snapshot["policy_hash"])
    ):
        raise RuntimeError("The transient V4 candidate policy was not accepted")
    STATE.candidate_armed = True
    STATE.candidate_arm_count += 1
    STATE.candidate_hash = snapshot["policy_hash"]
    unreal.log(
        "CALYSTO_V4_SIZE_MATRIX candidate_armed_once=true sizes={} hash={}".format(
            ",".join(str(x) for x in SIZES), STATE.candidate_hash
        )
    )


def clear_candidate_sizes():
    if not STATE.candidate_armed or STATE.candidate_clear_requested:
        return
    try:
        world = game_world()
        director = find_subsystem(world, STATE.classes.get("director")) if world else None
        if not world or not director:
            raise RuntimeError("No live PIE Director exists for candidate cleanup")
        unreal.SystemLibrary.execute_console_command(
            world, CANDIDATE_SIZES_COMMAND + " clear"
        )
        STATE.candidate_clear_requested = True
        snapshot = snapshot_document(director)
        expected = STATE.policy.get("policy_hash", "")
        if (
            not snapshot["policy_valid"]
            or snapshot["policy_error"]
            or snapshot["policy_hash"] != expected
        ):
            raise RuntimeError(
                "Candidate clear did not restore source hash {} (observed {})".format(
                    expected, snapshot["policy_hash"]
                )
            )
        STATE.candidate_clear_verified = True
    except Exception as exc:
        STATE.candidate_clear_error = str(exc)


def begin_next_case(world):
    STATE.case_index += 1
    if STATE.case_index >= len(STATE.cases):
        finish(True)
        return
    definition = STATE.cases[STATE.case_index]
    STATE.current = {
        **definition,
        "case_number": STATE.case_index + 1,
        "started": time.monotonic(),
        "source_world_identity": object_identity(world),
        "source_world_time": world_time(world),
        "destination_world_seen": False,
        "saw_dungeon_world": False,
        "door_disabled_before_ready": False,
        "events": new_event_state(),
        "blocked_log": [],
        "request_accepted": False,
    }
    STATE.log_offset = LOG_FILE.stat().st_size if LOG_FILE.is_file() else 0
    STATE.log_partial = b""
    STATE.log_ordinal = 0
    director = find_subsystem(world, STATE.classes["director"])
    if not director:
        raise RuntimeError("Dungeon Director subsystem is unavailable between cases")
    unreal.SystemLibrary.execute_console_command(
        world, "{} {}".format(FORCE_COMMAND, definition["edge"])
    )
    accepted = bool(
        reflected(director, "request_start_new_run_with_seed")(
            definition["run_seed"]
        )
    )
    STATE.current["request_accepted"] = accepted
    if not accepted:
        raise RuntimeError(
            "New Run was rejected for edge={} seed={}".format(
                definition["edge"], definition["run_seed"]
            )
        )
    set_phase(
        "wait_case_{:03d}_edge_{}_seed_{}".format(
            STATE.case_index + 1, definition["edge"], definition["run_seed"]
        )
    )


def observe_destination_and_door(world):
    if not STATE.current or not world:
        return
    if not STATE.current["destination_world_seen"]:
        if object_identity(world) != STATE.current["source_world_identity"]:
            STATE.current["destination_world_seen"] = True
        elif world_time(world) + 0.25 < STATE.current["source_world_time"]:
            STATE.current["destination_world_seen"] = True
    if (
        not STATE.current["destination_world_seen"]
        or canonical_world(world).lower() != DUNGEON_WORLD.lower()
    ):
        return
    director = find_subsystem(world, STATE.classes["director"])
    if not director:
        return
    snapshot = snapshot_document(director)
    if (
        snapshot["run_seed"] != STATE.current["run_seed"]
        or snapshot["floor_number"] != 1
        or snapshot["generation_serial"] != 1
    ):
        return
    doors = actors_of_class(world, STATE.classes["door"])
    if len(doors) == 1 and not door_enabled(doors[0]):
        STATE.current["door_disabled_before_ready"] = True


def audit_ready_case(world, director):
    snapshot_native = reflected(director, "get_snapshot")()
    if not state_ready(snapshot_native) or not bool(prop(snapshot_native, "bDoorReady")):
        return None
    snapshot = snapshot_document(director)
    case = STATE.current
    if (
        snapshot["run_seed"] != case["run_seed"]
        or snapshot["floor_number"] != 1
        or snapshot["generation_serial"] != 1
        or "newrun" not in normalized(snapshot["travel_kind"])
        or not case["destination_world_seen"]
    ):
        return None
    scan_log()
    if not readiness_complete_and_ordered(case["events"]):
        return None

    intent = reflected(director, "get_resolved_floor_intent")()
    manifest = reflected(director, "get_realized_floor_manifest")()
    ecology = reflected(director, "get_run_ecology")()
    hashes = hash_document(intent, manifest)
    size = prop(intent, "DungeonSize")
    actual_size = [int(prop(size, axis)) for axis in ("X", "Y", "Z")]
    counts = {
        "enemy": int(prop(manifest, "EnemyCount")),
        "npc": int(prop(manifest, "NPCCount")),
        "food": int(prop(manifest, "FoodCount")),
        "chest": int(prop(manifest, "ChestCount")),
        "loose_loot": int(prop(manifest, "LooseLootCount")),
        "clothing": int(prop(manifest, "ClothingCount")),
        "special_event": int(prop(manifest, "SpecialEventCount")),
    }
    total = sum(counts.values())
    instances = list(prop(manifest, "Instances"))
    directives = list(prop(intent, "SpawnDirectives"))
    instance_ids = [str(prop(item, "StableInstanceId")) for item in instances]
    directive_ids = [str(prop(item, "StableInstanceId")) for item in directives]

    category_results = {}
    structural_category_results = {}
    category_valid = True
    for item in list(prop(intent, "Categories")):
        key = category_key(prop(item, "Category"))
        if not key or key in category_results or key in structural_category_results:
            category_valid = False
            continue
        row = {
            "effective_chance": float(prop(item, "EffectiveChance")),
            "maximum": int(prop(item, "MaximumPerFloor")),
            "present": bool(prop(item, "bPresent")),
            "target_count": int(prop(item, "TargetCount")),
            "directive_count": int(prop(item, "DirectiveCount")),
        }
        if key in ("decoration", "lighting"):
            structural_category_results[key] = row
            category_valid = category_valid and (
                math.isfinite(row["effective_chance"])
                and row["effective_chance"] == 0.0
                and row["maximum"] == 0
                and not row["present"]
                and row["target_count"] == 0
                and row["directive_count"] == 0
            )
            continue
        category_results[key] = row
        category_valid = category_valid and (
            math.isfinite(row["effective_chance"])
            and 0.0 <= row["effective_chance"] <= 0.900001
            and 0 <= row["maximum"] <= CATEGORY_CAPS[key]
            and row["target_count"] == counts[key]
            and row["directive_count"] == counts[key]
            and counts[key] <= row["maximum"]
            and (row["present"] or counts[key] == 0)
        )

    dungeons = actors_of_class(world, STATE.classes["dungeon"])
    starts = actors_of_class(world, STATE.classes["start"])
    doors = actors_of_class(world, STATE.classes["door"])
    anchors = actors_of_class(world, STATE.classes["anchor"])
    nav_bounds = actors_of_class(world, STATE.classes["nav_bounds"])
    recast = actors_of_class(world, STATE.classes["recast"])
    all_pcg = []
    for dungeon in dungeons:
        all_pcg.extend(dungeon.get_components_by_class(unreal.PCGComponent))
    runtime_pcg = [item for item in all_pcg if not component_is_editor_only(item)]
    pcg_generating = []
    for component in runtime_pcg:
        method = getattr(component, "is_generating", None)
        pcg_generating.append(bool(method()) if callable(method) else False)

    population = actors_with_tag(world, POPULATION_TAG)
    live_counts = {
        key: len(actors_with_tag(world, tag)) for key, tag in CATEGORY_TAGS.items()
    }
    pcg_subsystem = find_subsystem(world, STATE.classes["pcg_subsystem"])
    navigation = (
        navigation_path_document(pcg_subsystem, starts[0], doors[0])
        if len(starts) == 1 and len(doors) == 1
        else {"valid": False, "partial": None, "point_count": 0,
              "error": "Expected exactly one Start and Door"}
    )

    style = enum_name(prop(intent, "Style"))
    theme = enum_name(prop(intent, "Theme"))
    edge = case["edge"]
    profile_compatible = not (
        (edge >= 27 and normalized(style) == "compact")
        or (edge >= 29 and normalized(theme) == "shrine")
    )
    event_seconds = case["events"]["seconds"]
    floor_ready_seconds = round(
        event_seconds["door_enabled"] - event_seconds["generate_local"], 3
    )
    failure_code = normalized(snapshot["failure_code"])
    checks = {
        "request_accepted": case["request_accepted"],
        "policy_valid": snapshot["policy_valid"] and not snapshot["policy_error"],
        "v4_policy_hash": snapshot["policy_hash"] == STATE.candidate_hash == hashes["policy"],
        "active_ready_run": snapshot["has_active_run"] and state_ready(snapshot_native),
        "no_failure": failure_code in ("", "none") and not snapshot["failure_message"],
        "no_pending_travel": snapshot["pending_floor"] == 0 and snapshot["pending_serial"] == 0,
        "door_and_companion_ready": snapshot["door_ready"] and snapshot["companion_ready"],
        "intent_manifest_v4": bool(prop(intent, "bIsValid"))
        and int(prop(intent, "GeneratorVersion")) == 4
        and bool(prop(manifest, "bIsValid")),
        "identity": int(prop(intent, "RunSeed")) == case["run_seed"]
        and int(prop(intent, "FloorNumber")) == 1
        and int(prop(intent, "GenerationSerial")) == 1
        and int(prop(manifest, "RunSeed")) == case["run_seed"]
        and int(prop(manifest, "FloorNumber")) == 1
        and int(prop(manifest, "GenerationSerial")) == 1,
        "exact_forced_size": actual_size == [edge, edge, 1]
        and snapshot["dungeon_size"] == actual_size
        and snapshot["forced_edge"] == edge
        and int(prop(intent, "DevelopmentForcedDungeonEdge")) == edge,
        "style_theme_compatible": profile_compatible,
        "shape_ranges": 0.20 <= float(prop(intent, "CandidateAnchorDensity")) <= 0.50
        and 0.30 <= float(prop(intent, "SidePathChance")) <= 0.70,
        "all_hashes_sha256": all(is_sha256(value) for value in hashes.values())
        and is_sha256(prop(ecology, "RunDNAHash")),
        "hash_cross_links": hashes["intent"] == str(prop(manifest, "IntentHash")).upper()
        == snapshot["intent_hash"]
        and hashes["manifest"] == snapshot["manifest_hash"]
        and hashes["ecology"] == snapshot["ecology_hash"]
        and hashes["companion"] == snapshot["companion_hash"],
        "categories_and_caps": set(category_results) == set(CATEGORY_CAPS)
        and set(structural_category_results) == {"decoration", "lighting"}
        and category_valid
        and all(0 <= counts[key] <= CATEGORY_CAPS[key] for key in CATEGORY_CAPS)
        and total <= INITIAL_ACTOR_CAP,
        "manifest_actor_count": int(prop(manifest, "SpawnedActorCount")) == total
        == len(instances) == len(directives),
        "stable_instance_ids": len(instance_ids) == len(set(instance_ids))
        and len(directive_ids) == len(set(directive_ids))
        and all(value and normalized(value) != "none" for value in instance_ids + directive_ids),
        "live_population_matches": len(population) == total and live_counts == counts,
        "candidate_anchors_available": int(prop(manifest, "CandidateAnchorCount")) > 0,
        "anchors_destroyed": len(anchors) == 0,
        "one_dungeon_start_door": len(dungeons) == len(starts) == len(doors) == 1,
        "door_enabled": len(doors) == 1 and door_enabled(doors[0]),
        "door_disabled_before_ready": case["door_disabled_before_ready"],
        "one_idle_runtime_pcg": len(runtime_pcg) == 1 and not any(pcg_generating),
        "navigation_runtime_present": bool(nav_bounds) and bool(recast),
        "start_to_door_navmesh_path": navigation["valid"],
        "readiness_exact_and_ordered": readiness_complete_and_ordered(case["events"]),
        "one_generate_local": case["events"]["counts"]["generate_local"] == 1,
        "floor_ready_under_30_seconds": 0.0 <= floor_ready_seconds
        < FLOOR_READY_ABSOLUTE_LIMIT_SECONDS,
        "costs_finite": all(
            finite_nonnegative(value)
            for value in (
                prop(intent, "ThreatBudget"),
                prop(intent, "PlannedThreatCost"),
                prop(intent, "ResourceBudget"),
                prop(intent, "PlannedResourceCost"),
                prop(manifest, "RealizedThreatCost"),
                prop(manifest, "RealizedResourceCost"),
            )
        ),
        "no_blocked_case_log": not case["blocked_log"],
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    sample = {
        "case_number": case["case_number"],
        "edge": edge,
        "seed_index": case["seed_index"],
        "run_seed": case["run_seed"],
        "style": style,
        "theme": theme,
        "dungeon_size": actual_size,
        "pcg_seed": int(prop(intent, "PCGSeed")),
        "counts": counts,
        "category_results": category_results,
        "structural_category_results": structural_category_results,
        "spawned_actor_count": int(prop(manifest, "SpawnedActorCount")),
        "candidate_anchor_count": int(prop(manifest, "CandidateAnchorCount")),
        "hashes": hashes,
        "navigation": navigation,
        "readiness": case["events"],
        "duration_seconds": {
            "request_to_ready": round(time.monotonic() - case["started"], 3),
            "generate_local_to_door_enabled": floor_ready_seconds,
        },
        "blocked_log": case["blocked_log"],
        "checks": checks,
        "success": not failed,
        "failed_checks": failed,
    }
    if failed:
        raise RuntimeError(
            "V4 size case {} edge={} seed={} failed: {}".format(
                case["case_number"], edge, case["run_seed"], ", ".join(failed)
            )
        )
    return sample


def nearest_rank_p95(values):
    ordered = sorted(float(value) for value in values)
    if not ordered:
        return None
    index = max(0, min(len(ordered) - 1, math.ceil(0.95 * len(ordered)) - 1))
    return round(ordered[index], 3)


def finish(requested_success, error=""):
    if STATE.finished:
        return
    STATE.finished = True
    clear_candidate_sizes()
    scan_log()
    final_content = content_snapshot()
    try:
        final_dirty = dirty_package_snapshot()
        dirty_error = ""
    except Exception as exc:
        final_dirty = {"all_dirty_packages": [], "monitored": {}}
        dirty_error = str(exc)
    final_protected = protected_hashes()
    global_log = global_log_document()
    policy_sha_after = sha256(POLICY_FILE) if POLICY_FILE.is_file() else ""

    asset_saves = sorted(
        path
        for path in set(STATE.initial_content) | set(final_content)
        if STATE.initial_content.get(path) != final_content.get(path)
    )
    initial_dirty_set = set(STATE.initial_dirty["all_dirty_packages"])
    final_dirty_set = set(final_dirty.get("all_dirty_packages", []))
    newly_dirty = sorted(final_dirty_set - initial_dirty_set)
    dirty_transitions = []
    for name in MONITORED_OBJECTS:
        before = STATE.initial_dirty["monitored"].get(name)
        after = final_dirty.get("monitored", {}).get(name)
        if before != after:
            dirty_transitions.append({"object_path": name, "before": before, "after": after})
    protected_mismatches = sorted(
        name
        for name, before in STATE.initial_protected.items()
        if before != final_protected.get(name)
    )
    durations = [
        row["duration_seconds"]["generate_local_to_door_enabled"]
        for row in STATE.results
    ]
    p95 = nearest_rank_p95(durations)
    size_summary = {}
    for edge in SIZES:
        rows = [row for row in STATE.results if row["edge"] == edge]
        size_summary[str(edge)] = {
            "expected_cases": SEEDS_PER_SIZE,
            "completed_cases": len(rows),
            "unique_seeds": len({row["run_seed"] for row in rows}),
            "passed_cases": sum(1 for row in rows if row["success"]),
            "minimum_ready_seconds": min(
                (row["duration_seconds"]["generate_local_to_door_enabled"] for row in rows),
                default=None,
            ),
            "maximum_ready_seconds": max(
                (row["duration_seconds"]["generate_local_to_door_enabled"] for row in rows),
                default=None,
            ),
        }

    final_checks = {
        "exactly_100_cases": len(STATE.cases) == EXPECTED_CASES
        and len(STATE.results) == EXPECTED_CASES,
        "twenty_unique_seeds_per_size": all(
            size_summary[str(edge)]["completed_cases"] == SEEDS_PER_SIZE
            and size_summary[str(edge)]["unique_seeds"] == SEEDS_PER_SIZE
            for edge in SIZES
        ),
        "all_cases_passed": len(STATE.results) == EXPECTED_CASES
        and all(row["success"] for row in STATE.results),
        "candidate_armed_once": STATE.candidate_armed
        and STATE.candidate_arm_count == 1
        and is_sha256(STATE.candidate_hash),
        "candidate_cleared_to_source": STATE.candidate_clear_requested
        and STATE.candidate_clear_verified
        and not STATE.candidate_clear_error,
        "policy_hash_constant": len(STATE.results) == EXPECTED_CASES
        and all(row["hashes"]["policy"] == STATE.candidate_hash for row in STATE.results),
        "p95_within_1_25_baseline": p95 is not None
        and p95 <= P95_LIMIT_SECONDS
        and p95 < FLOOR_READY_ABSOLUTE_LIMIT_SECONDS,
        "content_not_saved": not asset_saves,
        "no_new_dirty_state": not dirty_error and not newly_dirty and not dirty_transitions,
        "protected_hashes_stable": not protected_mismatches,
        "policy_bytes_stable": bool(STATE.policy_sha_before)
        and STATE.policy_sha_before == policy_sha_after,
        "strict_global_log_scan": global_log["gate_status"] == "PASS",
    }
    failed_final = sorted(name for name, passed in final_checks.items() if not passed)
    success = bool(requested_success) and not failed_final
    final_error = error
    if requested_success and failed_final:
        final_error = "Final V4 size-matrix checks failed: " + ", ".join(failed_final)

    document = {
        "schema_version": 4,
        "generator_version": 4,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "PASS" if success else "FAIL",
        "success": success,
        "phase": STATE.phase,
        "error": final_error,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "contract": {
            "configuration": "Development Editor unattended PIE only",
            "sizes": list(SIZES),
            "seeds_per_size": SEEDS_PER_SIZE,
            "expected_cases": EXPECTED_CASES,
            "candidate_command": CANDIDATE_SIZES_COMMAND,
            "force_command": FORCE_COMMAND,
            "candidate_command_execution_count": STATE.candidate_arm_count,
            "candidate_command_executed_once_before_first_new_run": (
                STATE.candidate_arm_count == 1
            ),
            "candidate_command_verified_by_policy_hash": is_sha256(STATE.candidate_hash),
            "candidate_command_clear_requested": STATE.candidate_clear_requested,
            "candidate_command_clear_verified_by_source_hash": STATE.candidate_clear_verified,
            "candidate_command_clear_error": STATE.candidate_clear_error,
            "readiness_order": "GenerateLocal <= PCGComplete <= NavigationPathReady <= EnemyLevelsReady <= PopulationRealized <= CompanionRosterReady <= DoorEnabled",
            "p95_method": "nearest-rank ceil(0.95*N)",
            "comparable_v3_p95_seconds": COMPARABLE_V3_P95_SECONDS,
            "p95_ratio_limit": P95_RATIO_LIMIT,
            "p95_limit_seconds": P95_LIMIT_SECONDS,
            "absolute_floor_ready_limit_seconds_exclusive": FLOOR_READY_ABSOLUTE_LIMIT_SECONDS,
        },
        "base_seed": BASE_SEED,
        "requested_sizes": list(SIZES),
        "seeds_per_size": SEEDS_PER_SIZE,
        "case_count": len(STATE.cases),
        "completed_case_count": len(STATE.results),
        "case_timeout_seconds": CASE_TIMEOUT_SECONDS,
        "cases": STATE.results,
        "size_summary": size_summary,
        "performance": {
            "sample_count": len(durations),
            "p95_ready_seconds": p95,
            "minimum_ready_seconds": min(durations) if durations else None,
            "maximum_ready_seconds": max(durations) if durations else None,
            "mean_ready_seconds": round(sum(durations) / len(durations), 3)
            if durations else None,
        },
        "policy": {
            **STATE.policy,
            "candidate_validated_dungeon_sizes": list(SIZES),
            "candidate_policy_hash": STATE.candidate_hash,
            "policy_sha256_before": STATE.policy_sha_before,
            "policy_sha256_after": policy_sha_after,
        },
        "asset_saves": asset_saves,
        "asset_mutations": sorted(set(asset_saves) | set(newly_dirty)),
        "dirty_state": {
            "method": "EditorLoadingAndSavingUtils full dirty-package inventory",
            "error": dirty_error,
            "initial": STATE.initial_dirty,
            "final": final_dirty,
            "newly_dirty": newly_dirty,
            "monitored_transitions": dirty_transitions,
        },
        "protected_assets": {
            "before": STATE.initial_protected,
            "after": final_protected,
            "mismatches": protected_mismatches,
        },
        "global_log_errors": global_log,
        "final_checks": final_checks,
        "phase_history": STATE.phase_history,
    }
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log("CALYSTO_V4_SIZE_MATRIX_RESULT " + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v4_size_matrix_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message):
    STATE.error = message
    finish(False, message)


def tick(_delta_seconds):
    if STATE.finished:
        return
    try:
        global_timeout = EXPECTED_CASES * (CASE_TIMEOUT_SECONDS + 15.0) + 600.0
        if elapsed() > global_timeout:
            fail("Global V4 size-matrix timeout in phase " + STATE.phase)
            return

        if STATE.phase == "load_map":
            if BASE_SEED <= 0 or CASE_TIMEOUT_SECONDS < 30.0:
                raise RuntimeError("The matrix requires a positive seed and timeout >= 30s")
            if len(STATE.cases) != EXPECTED_CASES:
                raise RuntimeError("The V4 matrix did not create exactly 100 cases")
            if len({(row["edge"], row["seed_index"]) for row in STATE.cases}) != EXPECTED_CASES:
                raise RuntimeError("The V4 matrix contains duplicate edge/seed-index cases")
            if not POLICY_FILE.is_file():
                raise RuntimeError("The authored V4 policy asset is missing")
            if any(not value["exists"] for value in STATE.initial_protected.values()):
                raise RuntimeError("A protected V4/Calysto invariant file is missing")
            bp_hash = STATE.initial_protected[
                "Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset"
            ]["sha256"]
            if bp_hash != BP_MASSIVE_DUNGEON_BASELINE:
                raise RuntimeError("BP_MassiveDungeon does not match its protected baseline")
            STATE.policy = native_policy_document(unreal.load_asset(POLICY_OBJECT))
            STATE.classes = {
                "director": unreal.load_class(None, DIRECTOR_CLASS),
                "pcg_subsystem": unreal.load_class(None, PCG_SUBSYSTEM_CLASS),
                "door": unreal.load_class(None, DOOR_CLASS),
                "dungeon": unreal.load_class(None, DUNGEON_CLASS),
                "start": unreal.load_class(None, START_POINT_CLASS),
                "anchor": unreal.load_class(None, ANCHOR_CLASS),
                "nav_bounds": unreal.load_class(None, NAV_BOUNDS_CLASS),
                "recast": unreal.load_class(None, RECAST_NAVMESH_CLASS),
            }
            if not all(STATE.classes.values()):
                raise RuntimeError("A required V4/Calysto runtime class failed to load")
            set_phase("wait_editor_hub")
            if LEVEL_EDITOR.load_level(CONTROL_MAP) is False:
                raise RuntimeError("The HUB control map failed to load")
            return

        if STATE.phase == "wait_editor_hub":
            if canonical_world(UNREAL_EDITOR.get_editor_world()).lower() != CONTROL_WORLD.lower():
                if phase_age() > 120.0:
                    raise RuntimeError("The HUB editor world did not become ready")
                return
            if phase_age() < 3.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            set_phase("wait_initial_pie_hub")
            return

        if STATE.phase in ("wait_initial_pie_hub", "wait_hub_between_cases"):
            world = game_world()
            if (
                LEVEL_EDITOR.is_in_play_in_editor()
                and world
                and canonical_world(world).lower() == CONTROL_WORLD.lower()
                and world_time(world) >= 1.0
            ):
                if STATE.phase == "wait_initial_pie_hub":
                    arm_candidate_sizes(world)
                begin_next_case(world)
                return
            if phase_age() > 120.0:
                raise RuntimeError("PIE HUB did not become ready between matrix cases")
            return

        if STATE.phase.startswith("wait_case_"):
            scan_log()
            world = game_world()
            if world and canonical_world(world).lower() == DUNGEON_WORLD.lower():
                STATE.current["saw_dungeon_world"] = True
                observe_destination_and_door(world)
                director = find_subsystem(world, STATE.classes["director"])
                if director and world_time(world) >= 1.0:
                    sample = audit_ready_case(world, director)
                    if sample:
                        STATE.results.append(sample)
                        STATE.current = None
                        unreal.GameplayStatics.open_level(
                            world, unreal.Name(CONTROL_MAP), True, ""
                        )
                        set_phase("wait_hub_between_cases")
                        return
            elif (
                world
                and STATE.current["saw_dungeon_world"]
                and canonical_world(world).lower() == CONTROL_WORLD.lower()
            ):
                raise RuntimeError("A size case returned to HUB before Floor Ready")
            if time.monotonic() - STATE.current["started"] > CASE_TIMEOUT_SECONDS:
                raise RuntimeError(
                    "Case timeout edge={} seed={}".format(
                        STATE.current["edge"], STATE.current["run_seed"]
                    )
                )
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v4_size_matrix_pie58", None)
if existing is not None:
    try:
        if getattr(existing, "callback", None) is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v4_size_matrix_pie58 = STATE
unreal.log(
    "CALYSTO_V4_SIZE_MATRIX validator_registered=true cases={} sizes={} seeds_per_size={}".format(
        EXPECTED_CASES, ",".join(str(x) for x in SIZES), SEEDS_PER_SIZE
    )
)
