"""Strict 25-floor live soak for Dungeon Director V4 in UE 5.8 PIE.

The validator keeps one GameInstance alive, starts a deterministic New Run,
and reaches Floors 2-25 exclusively through the real ACF interaction component
on the generated floor door.  It records wall-clock Floor Ready latency, active
world actors, exact population residue, process working set, and available
physical memory without saving Content or touching authored Calysto assets.
"""

import builtins
import ctypes
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
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V4_LIVE_SOAK_OUTPUT"]).resolve()
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V4_LIVE_SOAK_SEED", "202608210425"))

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = CONTENT_DIR / "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS = "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
START_POINT_CLASS = "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.BP_StartPoint_C"
ANCHOR_CLASS = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
INTERACTION_COMPONENT_CLASS = "/Script/AscentCombatFramework.ACFInteractionComponent"
NAV_BOUNDS_CLASS = "/Script/NavigationSystem.NavMeshBoundsVolume"
RECAST_NAVMESH_CLASS = "/Script/NavigationSystem.RecastNavMesh"

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
EXPECTED_GENERATIONS = 25
EXPECTED_DOOR_INTERACTIONS = 24
WARMUP_SAMPLE_COUNT = 4
P95_LIMIT_SECONDS = 25.76125
ABSOLUTE_READY_LIMIT_SECONDS = 30.0
WORKING_SET_RANGE_LIMIT_BYTES = 2 * 1024 * 1024 * 1024
BP_MASSIVE_DUNGEON_BASELINE = "47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B"

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

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
GLOBAL_TIMEOUT = 2400.0
PHASE_TIMEOUT = 180.0
READY_WORLD_TIME = 3.0
SELECTION_SAMPLES_REQUIRED = 3


class ProcessMemoryCounters(ctypes.Structure):
    _fields_ = [
        ("cb", ctypes.c_ulong),
        ("page_fault_count", ctypes.c_ulong),
        ("peak_working_set_size", ctypes.c_size_t),
        ("working_set_size", ctypes.c_size_t),
        ("quota_peak_paged_pool_usage", ctypes.c_size_t),
        ("quota_paged_pool_usage", ctypes.c_size_t),
        ("quota_peak_non_paged_pool_usage", ctypes.c_size_t),
        ("quota_non_paged_pool_usage", ctypes.c_size_t),
        ("pagefile_usage", ctypes.c_size_t),
        ("peak_pagefile_usage", ctypes.c_size_t),
    ]


class MemoryStatusEx(ctypes.Structure):
    _fields_ = [
        ("dwLength", ctypes.c_ulong),
        ("dwMemoryLoad", ctypes.c_ulong),
        ("ullTotalPhys", ctypes.c_ulonglong),
        ("ullAvailPhys", ctypes.c_ulonglong),
        ("ullTotalPageFile", ctypes.c_ulonglong),
        ("ullAvailPageFile", ctypes.c_ulonglong),
        ("ullTotalVirtual", ctypes.c_ulonglong),
        ("ullAvailVirtual", ctypes.c_ulonglong),
        ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
    ]


def memory_probe():
    """Use stable Win32 process/system memory APIs and fail closed on error."""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)

    get_current_process = kernel32.GetCurrentProcess
    get_current_process.argtypes = []
    get_current_process.restype = ctypes.c_void_p
    get_process_memory_info = psapi.GetProcessMemoryInfo
    get_process_memory_info.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ProcessMemoryCounters),
        ctypes.c_ulong,
    ]
    get_process_memory_info.restype = ctypes.c_int
    counters = ProcessMemoryCounters()
    counters.cb = ctypes.sizeof(counters)
    if not get_process_memory_info(
        get_current_process(), ctypes.byref(counters), counters.cb
    ):
        raise RuntimeError(
            "GetProcessMemoryInfo failed with Win32 error {}".format(
                ctypes.get_last_error()
            )
        )

    global_memory_status = kernel32.GlobalMemoryStatusEx
    global_memory_status.argtypes = [ctypes.POINTER(MemoryStatusEx)]
    global_memory_status.restype = ctypes.c_int
    status = MemoryStatusEx()
    status.dwLength = ctypes.sizeof(status)
    if not global_memory_status(ctypes.byref(status)):
        raise RuntimeError(
            "GlobalMemoryStatusEx failed with Win32 error {}".format(
                ctypes.get_last_error()
            )
        )
    result = {
        "measurement_status": "PASS",
        "process_api": "Win32 GetProcessMemoryInfo",
        "system_api": "Win32 GlobalMemoryStatusEx",
        "process_working_set_bytes": int(counters.working_set_size),
        "process_peak_working_set_bytes": int(counters.peak_working_set_size),
        "system_total_physical_bytes": int(status.ullTotalPhys),
        "system_available_physical_bytes": int(status.ullAvailPhys),
        "system_memory_load_percent": int(status.dwMemoryLoad),
    }
    if (
        result["process_working_set_bytes"] <= 0
        or result["system_total_physical_bytes"] <= 0
        or result["system_available_physical_bytes"] <= 0
        or not 0 <= result["system_memory_load_percent"] <= 100
    ):
        raise RuntimeError("Stable memory APIs returned an invalid measurement")
    return result


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
    raise RuntimeError("Missing reflected property {} on {}".format(name, object_path(owner)))


def reflected(owner, name):
    method = getattr(owner, name, None)
    if not callable(method):
        raise RuntimeError("Missing reflected method {} on {}".format(name, object_path(owner)))
    return method


def enum_name(value):
    name = getattr(value, "name", None)
    return str(name if name else value).split(".")[-1]


def normalized(value):
    return re.sub(r"[^a-z0-9]", "", enum_name(value).lower())


def kind_matches(value, expected):
    actual = normalized(value)
    aliases = {
        "newrun": ("newrun",),
        "advance": ("advance",),
    }
    return any(token in actual for token in aliases[expected.lower()])


def state_ready(snapshot):
    return "ready" in normalized(prop(snapshot, "State"))


def actors_of_class(world, actor_class):
    if not world or not actor_class:
        return []
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def actors_with_tag(world, tag):
    return list(unreal.GameplayStatics.get_all_actors_with_tag(world, tag)) if world else []


def components_of_class(actor, component_class):
    if not actor or not component_class:
        return []
    return list(actor.get_components_by_class(component_class))


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
            if candidate.get_class() == subsystem_class and object_path(candidate).startswith(prefix):
                return candidate
        except Exception:
            pass
    return None


def door_enabled(door):
    for name in ("bIsEnabled", "IsEnabled"):
        try:
            return bool(prop(door, name))
        except Exception:
            pass
    raise RuntimeError("The V4 floor door does not expose its enabled state")


def door_label_matches(label, floor):
    numbers = []
    for match in re.findall(r"\d[\d\s,\.]*", str(label)):
        digits = re.sub(r"\D", "", match)
        if digits:
            numbers.append(int(digits))
    return "\u2192" in str(label) and numbers == [int(floor), int(floor) + 1]


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
            result[path.relative_to(CONTENT_DIR).as_posix()] = [int(stat.st_size), int(stat.st_mtime_ns)]
    return result


def dirty_states():
    result = {}
    for object_name in MONITORED_OBJECTS:
        asset = unreal.load_asset(object_name)
        try:
            result[object_name] = bool(asset.get_outermost().is_dirty()) if asset else None
        except Exception:
            result[object_name] = None
    return result


def native_policy_document(policy):
    policy_class = unreal.load_class(None, POLICY_CLASS)
    if not policy or not policy_class or policy.get_class() != policy_class:
        raise RuntimeError("The authored policy is not the exact native Dungeon Director V4 class")
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
        "class": object_path(policy.get_class()),
        "schema_version": int(prop(policy, "SchemaVersion")),
        "generator_version": int(prop(policy, "GeneratorVersion")),
        "policy_id": str(prop(policy, "PolicyId")),
        "policy_hash": str(reflected(policy, "get_policy_hash")()).upper(),
    }
    if (
        document["class"] != POLICY_CLASS
        or document["schema_version"] != 4
        or document["generator_version"] != 4
        or document["policy_id"] != "CalystoDungeonDirectorV4"
        or not is_sha256(document["policy_hash"])
    ):
        raise RuntimeError("The authored V4 policy identity/version/hash is invalid")
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


def context_matches(snapshot, floor, serial, kind):
    return (
        int(prop(snapshot, "FloorNumber")) == floor
        and int(prop(snapshot, "GenerationSerial")) == serial
        and kind_matches(prop(snapshot, "TravelKind"), kind)
    )


def nearest_rank_p95(values):
    ordered = sorted(float(value) for value in values)
    if not ordered:
        return None
    rank = int(math.ceil(0.95 * len(ordered)))
    return ordered[max(0, min(len(ordered) - 1, rank - 1))]


def trend_document(values, warmup_count):
    all_values = [int(value) for value in values]
    post = all_values[min(warmup_count, max(0, len(all_values) - 1)) :]
    deltas = [current - previous for previous, current in zip(post, post[1:])]
    monotonic_growth = bool(deltas) and all(delta >= 0 for delta in deltas) and any(delta > 0 for delta in deltas)
    mean_x = (len(post) - 1) / 2.0 if post else 0.0
    mean_y = sum(post) / float(len(post)) if post else 0.0
    denominator = sum((index - mean_x) ** 2 for index in range(len(post)))
    slope = (
        sum((index - mean_x) * (value - mean_y) for index, value in enumerate(post)) / denominator
        if denominator > 0.0
        else 0.0
    )
    return {
        "warmup_samples_excluded": min(warmup_count, max(0, len(all_values) - 1)),
        "sample_count_post_warmup": len(post),
        "first_post_warmup": post[0] if post else None,
        "final": post[-1] if post else None,
        "minimum_post_warmup": min(post) if post else None,
        "maximum_post_warmup": max(post) if post else None,
        "range_post_warmup": max(post) - min(post) if post else None,
        "decrease_count_post_warmup": sum(1 for delta in deltas if delta < 0),
        "increase_count_post_warmup": sum(1 for delta in deltas if delta > 0),
        "monotonic_non_decreasing_growth": monotonic_growth,
        "least_squares_slope_per_floor": round(slope, 6),
        "values": all_values,
    }


class State:
    def __init__(self):
        self.phase = "load_map"
        self.started = time.monotonic()
        self.phase_started = self.started
        self.callback = None
        self.finished = False
        self.classes = {}
        self.policy = {}
        self.samples = []
        self.operations = []
        self.door_interactions = []
        self.current_sample = None
        self.initial_run_epoch = 0
        self.wait_label = ""
        self.expected_floor = 0
        self.expected_serial = 0
        self.expected_kind = ""
        self.wait_started = self.started
        self.wait_source_world_identity = 0
        self.wait_source_door_identity = 0
        self.wait_source_time = 0.0
        self.destination_world_seen = False
        self.saw_dungeon_world = False
        self.door_disabled_observed = {}
        self.door_disabled_actor = {}
        self.selection_samples = 0
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_states()
        self.initial_protected = protected_hashes()
        self.policy_sha_before = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None


STATE = State()


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("CALYSTO_V4_LIVE_SOAK phase={}".format(value))


def build_soak_metrics():
    if not STATE.samples:
        return {
            "measurement_status": "PENDING",
            "reason": "No accepted floor samples were available",
        }
    ready_values = [float(sample["runtime"]["ready_elapsed_seconds"]) for sample in STATE.samples]
    working_set_values = [int(sample["memory"]["process_working_set_bytes"]) for sample in STATE.samples]
    available_values = [int(sample["memory"]["system_available_physical_bytes"]) for sample in STATE.samples]
    actor_values = [int(sample["runtime"]["world_actor_count"]) for sample in STATE.samples]
    adjusted_actor_values = [
        int(sample["runtime"]["world_actor_count"] - sample["runtime"]["population_actor_count"])
        for sample in STATE.samples
    ]
    memory_trend = trend_document(working_set_values, WARMUP_SAMPLE_COUNT)
    available_trend = trend_document(available_values, WARMUP_SAMPLE_COUNT)
    actor_trend = trend_document(actor_values, WARMUP_SAMPLE_COUNT)
    adjusted_actor_trend = trend_document(adjusted_actor_values, WARMUP_SAMPLE_COUNT)
    return {
        "measurement_status": "PASS",
        "generation_count": len(STATE.samples),
        "real_acf_door_interactions": len(STATE.door_interactions),
        "floor_range": [STATE.samples[0]["floor"], STATE.samples[-1]["floor"]],
        "warmup_sample_count": WARMUP_SAMPLE_COUNT,
        "floor_ready_seconds": {
            "samples": ready_values,
            "minimum": min(ready_values),
            "median": sorted(ready_values)[len(ready_values) // 2],
            "p95_method": "nearest-rank",
            "p95": nearest_rank_p95(ready_values),
            "p95_limit": P95_LIMIT_SECONDS,
            "maximum": max(ready_values),
            "absolute_limit": ABSOLUTE_READY_LIMIT_SECONDS,
        },
        "process_working_set_bytes": {
            **memory_trend,
            "allowed_range_post_warmup": WORKING_SET_RANGE_LIMIT_BYTES,
            "api": "Win32 GetProcessMemoryInfo",
        },
        "system_available_physical_bytes": {
            **available_trend,
            "api": "Win32 GlobalMemoryStatusEx",
            "gate_scope": "measurement-only; other processes make trend non-authoritative",
        },
        "active_world_actor_count": actor_trend,
        "population_adjusted_world_actor_count": adjusted_actor_trend,
        "residue_criterion": (
            "Every accepted world has exactly one dungeon, one door, one start, "
            "one idle runtime PCG, zero anchors, and live population equal to its manifest."
        ),
        "trend_criterion": (
            "After four warmup floors, process working set and population-adjusted "
            "active-world actors may remain flat but may not form a non-decreasing "
            "series with positive net growth; working-set range is additionally capped at 2 GiB."
        ),
    }


def finish(success, error=""):
    if STATE.finished:
        return
    STATE.finished = True
    current_content = content_snapshot()
    final_dirty = dirty_states()
    final_protected = protected_hashes()
    asset_saves = sorted(
        path
        for path in set(STATE.initial_content) | set(current_content)
        if STATE.initial_content.get(path) != current_content.get(path)
    )
    dirty_changes = {
        name: {"before": STATE.initial_dirty.get(name), "after": final_dirty.get(name)}
        for name in set(STATE.initial_dirty) | set(final_dirty)
        if STATE.initial_dirty.get(name) != final_dirty.get(name)
    }
    protected_mismatches = sorted(
        name for name, before in STATE.initial_protected.items() if before != final_protected.get(name)
    )
    policy_sha_after = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
    metrics = build_soak_metrics()
    ready = metrics.get("floor_ready_seconds", {})
    memory = metrics.get("process_working_set_bytes", {})
    adjusted_actors = metrics.get("population_adjusted_world_actor_count", {})
    expected_labels = ["new_run_floor_1"] + ["advance_floor_{}".format(floor) for floor in range(2, 26)]
    expected_floors = list(range(1, 26))
    final_checks = {
        "accepted_generation_count_25": len(STATE.samples) == EXPECTED_GENERATIONS,
        "real_acf_door_interactions_24": len(STATE.door_interactions) == EXPECTED_DOOR_INTERACTIONS,
        "sample_labels_exact": [sample["label"] for sample in STATE.samples] == expected_labels,
        "floor_sequence_1_through_25": [sample["floor"] for sample in STATE.samples] == expected_floors,
        "generation_serial_sequence_1_through_25": [sample["serial"] for sample in STATE.samples] == expected_floors,
        "door_sources_1_through_24": [row["from_floor"] for row in STATE.door_interactions] == list(range(1, 25)),
        "door_disabled_before_every_ready": len(STATE.samples) == EXPECTED_GENERATIONS
        and all(STATE.door_disabled_observed.get(sample["label"], False) for sample in STATE.samples),
        "policy_hash_constant": bool(STATE.samples)
        and all(sample["hashes"]["policy"] == STATE.policy.get("policy_hash") for sample in STATE.samples),
        "run_epoch_constant": bool(STATE.samples)
        and STATE.initial_run_epoch > 0
        and all(sample["run_epoch"] == STATE.initial_run_epoch for sample in STATE.samples),
        "pcg_seed_unique_per_floor": len(STATE.samples) == EXPECTED_GENERATIONS
        and len({sample["pcg_seed"] for sample in STATE.samples}) == EXPECTED_GENERATIONS,
        "normal_history_only": bool(STATE.samples)
        and all(not sample["development_synthetic_history"] for sample in STATE.samples),
        "all_sample_checks_pass": len(STATE.samples) == EXPECTED_GENERATIONS
        and all(all(sample["checks"].values()) for sample in STATE.samples),
        "floor_ready_p95_within_baseline_factor": ready.get("p95") is not None
        and float(ready["p95"]) <= P95_LIMIT_SECONDS,
        "every_floor_ready_under_30_seconds": ready.get("maximum") is not None
        and float(ready["maximum"]) < ABSOLUTE_READY_LIMIT_SECONDS,
        "memory_measurement_complete": metrics.get("measurement_status") == "PASS"
        and all(sample["memory"].get("measurement_status") == "PASS" for sample in STATE.samples),
        "working_set_bounded": memory.get("range_post_warmup") is not None
        and int(memory["range_post_warmup"]) <= WORKING_SET_RANGE_LIMIT_BYTES,
        "working_set_not_monotonic_growth": memory.get("monotonic_non_decreasing_growth") is False,
        "actor_count_not_monotonic_growth": adjusted_actors.get("monotonic_non_decreasing_growth") is False,
        "runtime_residue_absent": len(STATE.samples) == EXPECTED_GENERATIONS
        and all(sample["runtime"]["residue_actor_count"] == 0 for sample in STATE.samples),
        "content_not_saved": not asset_saves,
        "dirty_state_preserved": not dirty_changes,
        "protected_hashes_stable": not protected_mismatches,
        "policy_bytes_stable": STATE.policy_sha_before is not None and STATE.policy_sha_before == policy_sha_after,
    }
    failed_final = sorted(name for name, passed in final_checks.items() if not passed)
    if success and failed_final:
        success = False
        error = "Final live-soak checks failed: " + ", ".join(failed_final)
    document = {
        "schema_version": 4,
        "generator_version": 4,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "success": bool(success),
        "status": "PASS" if success else "FAIL",
        "phase": STATE.phase,
        "error": error,
        "run_seed": RUN_SEED,
        "policy": STATE.policy,
        "expected_generation_count": EXPECTED_GENERATIONS,
        "expected_door_interactions": EXPECTED_DOOR_INTERACTIONS,
        "samples": STATE.samples,
        "operations": STATE.operations,
        "door_interactions": STATE.door_interactions,
        "door_disabled_observed": STATE.door_disabled_observed,
        "door_disabled_actor": STATE.door_disabled_actor,
        "soak_metrics": metrics,
        "final_checks": final_checks,
        "asset_saves": asset_saves,
        "dirty_changes": dirty_changes,
        "dirty_before": STATE.initial_dirty,
        "dirty_after": final_dirty,
        "protected_assets": {
            "before": STATE.initial_protected,
            "after": final_protected,
            "mismatches": protected_mismatches,
        },
        "policy_sha256_before": STATE.policy_sha_before,
        "policy_sha256_after": policy_sha_after,
    }
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CALYSTO_V4_LIVE_SOAK_RESULT " + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v4_live_soak_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message):
    finish(False, message)


def director_for_world(world):
    return find_subsystem(world, STATE.classes.get("director"))


def begin_wait(label, floor, serial, kind, source_world):
    STATE.wait_label = label
    STATE.expected_floor = int(floor)
    STATE.expected_serial = int(serial)
    STATE.expected_kind = kind
    STATE.wait_started = time.monotonic()
    STATE.wait_source_world_identity = object_identity(source_world)
    source_doors = actors_of_class(source_world, STATE.classes.get("door"))
    STATE.wait_source_door_identity = object_identity(source_doors[0]) if len(source_doors) == 1 else 0
    STATE.wait_source_time = world_time(source_world)
    STATE.destination_world_seen = canonical_world(source_world).lower() != DUNGEON_WORLD.lower()
    STATE.saw_dungeon_world = False
    STATE.door_disabled_observed[label] = False
    STATE.door_disabled_actor[label] = ""
    set_phase("wait_runtime_" + label)


def update_destination_world(world):
    if STATE.destination_world_seen or not world:
        return
    if object_identity(world) != STATE.wait_source_world_identity:
        STATE.destination_world_seen = True
        return
    if world_time(world) + 0.25 < STATE.wait_source_time:
        STATE.destination_world_seen = True


def observe_pre_ready_door(world):
    if not STATE.destination_world_seen or canonical_world(world).lower() != DUNGEON_WORLD.lower():
        return
    director = director_for_world(world)
    if not director:
        return
    snapshot = reflected(director, "get_snapshot")()
    if not context_matches(snapshot, STATE.expected_floor, STATE.expected_serial, STATE.expected_kind):
        return
    doors = actors_of_class(world, STATE.classes["door"])
    if (
        len(doors) == 1
        and object_identity(doors[0]) != STATE.wait_source_door_identity
        and not door_enabled(doors[0])
    ):
        STATE.door_disabled_observed[STATE.wait_label] = True
        STATE.door_disabled_actor[STATE.wait_label] = object_path(doors[0])


def audit_runtime(world, director):
    snapshot = reflected(director, "get_snapshot")()
    if not state_ready(snapshot) or not bool(prop(snapshot, "bDoorReady")):
        return None
    if not context_matches(snapshot, STATE.expected_floor, STATE.expected_serial, STATE.expected_kind):
        return None
    if not STATE.destination_world_seen:
        return None
    if not STATE.door_disabled_observed.get(STATE.wait_label, False):
        raise RuntimeError("Did not observe the destination V4 floor door disabled before readiness for " + STATE.wait_label)

    intent = reflected(director, "get_resolved_floor_intent")()
    manifest = reflected(director, "get_realized_floor_manifest")()
    ecology = reflected(director, "get_run_ecology")()
    floor = STATE.expected_floor
    serial = STATE.expected_serial
    hashes = hash_document(intent, manifest)
    size = prop(intent, "DungeonSize")
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
    category_checks = {}
    structural_category_checks = {}
    for item in list(prop(intent, "Categories")):
        key = category_key(prop(item, "Category"))
        if not key or key in category_results or key in structural_category_results:
            raise RuntimeError("Intent contains an unknown or duplicate V4 category")
        row = {
            "effective_chance": float(prop(item, "EffectiveChance")),
            "maximum": int(prop(item, "MaximumPerFloor")),
            "present": bool(prop(item, "bPresent")),
            "target_count": int(prop(item, "TargetCount")),
            "directive_count": int(prop(item, "DirectiveCount")),
        }
        if key in ("decoration", "lighting"):
            structural_category_results[key] = row
            structural_category_checks[key] = (
                math.isfinite(row["effective_chance"])
                and row["effective_chance"] == 0.0
                and row["maximum"] == 0
                and not row["present"]
                and row["target_count"] == 0
                and row["directive_count"] == 0
            )
            continue
        category_results[key] = row
        category_checks[key] = (
            math.isfinite(row["effective_chance"])
            and 0.0 <= row["effective_chance"] <= 0.900001
            and 0 <= row["maximum"] <= CATEGORY_CAPS[key]
            and row["target_count"] == counts[key]
            and row["directive_count"] == counts[key]
            and counts[key] <= row["maximum"]
            and (row["present"] or counts[key] == 0)
        )

    population = actors_with_tag(world, POPULATION_TAG)
    live_category_actors = {key: actors_with_tag(world, tag) for key, tag in CATEGORY_TAGS.items()}
    live_category_counts = {key: len(value) for key, value in live_category_actors.items()}
    population_identities = {object_identity(actor) for actor in population}
    category_identities = {
        key: {object_identity(actor) for actor in actors}
        for key, actors in live_category_actors.items()
    }
    category_union = set().union(*category_identities.values()) if category_identities else set()
    doors = actors_of_class(world, STATE.classes["door"])
    dungeons = actors_of_class(world, STATE.classes["dungeon"])
    starts = actors_of_class(world, STATE.classes["start"])
    anchors = actors_of_class(world, STATE.classes["anchor"])
    nav_bounds = actors_of_class(world, STATE.classes["nav_bounds"])
    recast = actors_of_class(world, STATE.classes["recast"])
    world_actors = actors_of_class(world, unreal.Actor)
    generated_actors = actors_with_tag(world, "PCG Generated Actor")
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    interactions = components_of_class(player, STATE.classes["interaction"])
    all_pcg = []
    for dungeon in dungeons:
        all_pcg.extend(dungeon.get_components_by_class(unreal.PCGComponent))
    runtime_pcg = [component for component in all_pcg if not component_is_editor_only(component)]
    pcg_generating = []
    for component in runtime_pcg:
        method = getattr(component, "is_generating", None)
        pcg_generating.append(bool(method()) if callable(method) else False)

    enabled = len(doors) == 1 and door_enabled(doors[0])
    label = str(reflected(doors[0], "get_interactable_name")()) if len(doors) == 1 else ""
    style = enum_name(prop(intent, "Style"))
    theme = enum_name(prop(intent, "Theme"))
    run_epoch = int(prop(snapshot, "RunEpoch"))
    snapshot_failure_code = normalized(prop(snapshot, "FailureCode"))
    snapshot_failure_message = str(prop(snapshot, "FailureMessage"))
    memory = memory_probe()
    residue_actor_count = (
        len(anchors)
        + abs(len(dungeons) - 1)
        + abs(len(doors) - 1)
        + abs(len(starts) - 1)
        + abs(len(population) - total)
        + abs(sum(live_category_counts.values()) - total)
    )

    checks = {
        "policy_valid": bool(prop(snapshot, "bPolicyValid")) and not str(prop(snapshot, "PolicyError")),
        "active_run": bool(prop(snapshot, "bHasActiveRun")),
        "ready_state": state_ready(snapshot),
        "door_and_companion_ready": bool(prop(snapshot, "bDoorReady")) and bool(prop(snapshot, "bCompanionReady")),
        "no_failure": snapshot_failure_code in ("", "none") and not snapshot_failure_message,
        "no_queued_intent": not bool(prop(snapshot, "bHasQueuedDirectorIntent")),
        "no_pending_travel": int(prop(snapshot, "PendingFloorNumber")) == 0 and int(prop(snapshot, "PendingGenerationSerial")) == 0,
        "intent_valid_v4": bool(prop(intent, "bIsValid")) and int(prop(intent, "GeneratorVersion")) == 4,
        "manifest_valid": bool(prop(manifest, "bIsValid")),
        "identity": int(prop(intent, "RunSeed")) == RUN_SEED
        and int(prop(intent, "FloorNumber")) == floor
        and int(prop(intent, "GenerationSerial")) == serial
        and int(prop(manifest, "RunSeed")) == RUN_SEED
        and int(prop(manifest, "FloorNumber")) == floor
        and int(prop(manifest, "GenerationSerial")) == serial
        and int(reflected(director, "get_current_floor")()) == floor,
        "snapshot_identity": int(prop(snapshot, "RunSeed")) == RUN_SEED
        and int(prop(snapshot, "FloorNumber")) == floor
        and int(prop(snapshot, "GenerationSerial")) == serial,
        "policy_identity": hashes["policy"] == STATE.policy["policy_hash"] == str(prop(snapshot, "PolicyHash")).upper(),
        "intent_manifest_identity": hashes["intent"] == str(prop(manifest, "IntentHash")).upper() == str(prop(snapshot, "IntentHash")).upper(),
        "manifest_snapshot_identity": hashes["manifest"] == str(prop(snapshot, "ManifestHash")).upper(),
        "ecology_identity": hashes["ecology"] == str(prop(ecology, "EcologyHash")).upper() == str(prop(snapshot, "EcologyHash")).upper(),
        "companion_identity": hashes["companion"] == str(prop(intent, "CompanionSnapshotHash")).upper() == str(prop(snapshot, "CompanionSnapshotHash")).upper(),
        "all_hashes_sha256": all(is_sha256(value) for value in hashes.values()) and is_sha256(prop(ecology, "RunDNAHash")),
        "ecology_initialized": bool(prop(ecology, "bInitialized")),
        "normal_history": not bool(prop(ecology, "bDevelopmentSyntheticHistory")),
        "style_valid": any(token in normalized(style) for token in ("standard", "compact", "branching")),
        "theme_valid": any(token in normalized(theme) for token in ("default", "forge", "shrine")),
        "certified_size": 26 <= int(prop(size, "X")) <= 30 and int(prop(size, "X")) == int(prop(size, "Y")) and int(prop(size, "Z")) == 1,
        "shape_ranges": 0.20 <= float(prop(intent, "CandidateAnchorDensity")) <= 0.50 and 0.30 <= float(prop(intent, "SidePathChance")) <= 0.70,
        "category_set": set(category_results) == set(CATEGORY_CAPS)
        and set(structural_category_results) == {"decoration", "lighting"}
        and all(category_checks.values())
        and all(structural_category_checks.values()),
        "hard_caps": all(0 <= counts[key] <= CATEGORY_CAPS[key] for key in CATEGORY_CAPS) and total <= INITIAL_ACTOR_CAP,
        "manifest_actor_count": int(prop(manifest, "SpawnedActorCount")) == total == len(instances) == len(directives),
        "stable_ids": len(instance_ids) == len(set(instance_ids)) and len(directive_ids) == len(set(directive_ids)) and all(value and value.lower() != "none" for value in instance_ids + directive_ids),
        "live_population": len(population) == total and live_category_counts == counts,
        "population_tags_partition": category_union == population_identities
        and sum(len(value) for value in category_identities.values()) == len(population_identities),
        "candidate_anchors_available": int(prop(manifest, "CandidateAnchorCount")) > 0,
        "anchors_destroyed": len(anchors) == 0,
        "one_dungeon_door_start": len(dungeons) == 1 and len(doors) == 1 and len(starts) == 1,
        "door_enabled_and_labeled": enabled and door_label_matches(label, floor),
        "real_acf_interaction_surface": player is not None and len(interactions) == 1,
        "native_navigation_gate_complete": len(nav_bounds) > 0 and len(recast) > 0 and bool(prop(snapshot, "bDoorReady")),
        "one_idle_runtime_pcg": len(runtime_pcg) == 1 and not any(pcg_generating),
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
        "memory_measurement_complete": memory["measurement_status"] == "PASS",
        "door_disabled_before_ready": STATE.door_disabled_observed.get(STATE.wait_label, False),
        "runtime_residue_zero": residue_actor_count == 0,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    sample = {
        "label": STATE.wait_label,
        "floor": floor,
        "serial": serial,
        "travel_kind": enum_name(prop(snapshot, "TravelKind")),
        "run_epoch": run_epoch,
        "pcg_seed": int(prop(intent, "PCGSeed")),
        "style": style,
        "theme": theme,
        "dungeon_size": [int(prop(size, "X")), int(prop(size, "Y")), int(prop(size, "Z"))],
        "counts": counts,
        "category_results": category_results,
        "structural_category_results": structural_category_results,
        "spawned_actor_count": int(prop(manifest, "SpawnedActorCount")),
        "candidate_anchor_count": int(prop(manifest, "CandidateAnchorCount")),
        "hashes": hashes,
        "development_synthetic_history": bool(prop(ecology, "bDevelopmentSyntheticHistory")),
        "door": {
            "actor": object_path(doors[0]) if len(doors) == 1 else "",
            "enabled": enabled,
            "label": label,
            "disabled_before_ready_observed": STATE.door_disabled_observed.get(STATE.wait_label, False),
        },
        "runtime": {
            "world": object_path(world),
            "world_actor_count": len(world_actors),
            "population_actor_count": len(population),
            "population_adjusted_world_actor_count": len(world_actors) - len(population),
            "generated_actor_tag_count": len(generated_actors),
            "dungeon_count": len(dungeons),
            "door_count": len(doors),
            "start_count": len(starts),
            "anchors_after_ready": len(anchors),
            "residue_actor_count": residue_actor_count,
            "runtime_pcg_components": len(runtime_pcg),
            "nav_bounds": len(nav_bounds),
            "recast_navmeshes": len(recast),
            "ready_elapsed_seconds": round(time.monotonic() - STATE.wait_started, 3),
        },
        "memory": memory,
        "checks": checks,
    }
    if failed:
        raise RuntimeError("Runtime sample {} failed: {}".format(STATE.wait_label, ", ".join(failed)))
    return sample


def begin_door_selection(world, sample):
    doors = actors_of_class(world, STATE.classes["door"])
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    interactions = components_of_class(player, STATE.classes["interaction"])
    if len(doors) != 1 or not player or len(interactions) != 1 or not door_enabled(doors[0]):
        raise RuntimeError("Cannot arm the real ACF door interaction for Floor {}".format(sample["floor"]))
    location = doors[0].get_actor_location()
    requested = unreal.Vector(location.x - 80.0, location.y - 80.0, location.z)
    player.set_actor_location(requested, False, True)
    distance = float(player.get_distance_to(doors[0]))
    if distance > 200.0:
        raise RuntimeError("Player remained outside V4 floor door range: {:.3f} cm".format(distance))
    interaction = interactions[0]
    reflected(interaction, "enable_detection")(False)
    reflected(interaction, "enable_detection")(True)
    reflected(interaction, "refresh_interactions")()
    STATE.selection_samples = 0
    set_phase("wait_door_selection_floor_{}".format(sample["floor"]))


def sample_door_selection(world):
    sample = STATE.current_sample
    doors = actors_of_class(world, STATE.classes["door"])
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    interactions = components_of_class(player, STATE.classes["interaction"])
    if len(doors) != 1 or len(interactions) != 1:
        STATE.selection_samples = 0
        return
    interaction = interactions[0]
    reflected(interaction, "refresh_interactions")()
    best = reflected(interaction, "get_current_best_interactable_actor")()
    door_path = object_path(doors[0])
    try:
        overlaps = {object_path(actor) for actor in interaction.get_overlapping_actors()}
    except Exception:
        overlaps = set()
    if object_path(best) == door_path and door_path in overlaps:
        STATE.selection_samples += 1
    else:
        STATE.selection_samples = 0
    if STATE.selection_samples < SELECTION_SAMPLES_REQUIRED:
        return

    floor = sample["floor"]
    serial = sample["serial"]
    if floor < 1 or floor >= EXPECTED_GENERATIONS:
        raise RuntimeError("Unexpected real-door source Floor {}".format(floor))
    if object_path(reflected(interaction, "get_current_best_interactable_actor")()) != door_path:
        raise RuntimeError("The real V4 floor door lost ACF selection before Interact")
    can_interact = getattr(doors[0], "can_be_interacted", None)
    if callable(can_interact) and not bool(can_interact(player)):
        raise RuntimeError("The selected V4 floor door rejected CanBeInteracted")
    record = {
        "source": "real_acf_interaction_component",
        "operation": "advance_via_real_acf_door",
        "from_floor": floor,
        "from_serial": serial,
        "to_floor": floor + 1,
        "to_serial": serial + 1,
        "door_actor": door_path,
        "door_label": sample["door"]["label"],
        "disabled_before_readiness": sample["door"]["disabled_before_ready_observed"],
        "selection_samples": STATE.selection_samples,
        "dispatched": True,
    }
    STATE.operations.append(record)
    STATE.door_interactions.append(record)
    begin_wait("advance_floor_{}".format(floor + 1), floor + 1, serial + 1, "Advance", world)
    reflected(interaction, "interact")("CalystoV4LiveSoakPIE58")


def handle_sample(world, sample):
    STATE.samples.append(sample)
    STATE.current_sample = sample
    if len(STATE.samples) == 1:
        STATE.initial_run_epoch = sample["run_epoch"]
    if sample["floor"] >= EXPECTED_GENERATIONS:
        finish(True)
        return
    begin_door_selection(world, sample)


def tick(delta_seconds):
    del delta_seconds
    if STATE.finished:
        return
    try:
        if time.monotonic() - STATE.started > GLOBAL_TIMEOUT:
            fail("Global timeout in phase " + STATE.phase)
            return
        if time.monotonic() - STATE.phase_started > PHASE_TIMEOUT:
            fail("Phase timeout in " + STATE.phase)
            return

        if STATE.phase == "load_map":
            if RUN_SEED <= 0 or not POLICY_FILE.is_file():
                raise RuntimeError("Live soak requires a positive seed and the authored V4 policy")
            if any(not row["exists"] for row in STATE.initial_protected.values()):
                raise RuntimeError("A protected live-soak invariant file is missing")
            bp_before = STATE.initial_protected["Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset"]["sha256"]
            if bp_before != BP_MASSIVE_DUNGEON_BASELINE:
                raise RuntimeError("BP_MassiveDungeon no longer matches the protected baseline")
            memory_probe()
            STATE.policy = native_policy_document(unreal.load_asset(POLICY_OBJECT))
            STATE.classes = {
                "director": unreal.load_class(None, DIRECTOR_CLASS),
                "door": unreal.load_class(None, DOOR_CLASS),
                "dungeon": unreal.load_class(None, DUNGEON_CLASS),
                "start": unreal.load_class(None, START_POINT_CLASS),
                "anchor": unreal.load_class(None, ANCHOR_CLASS),
                "interaction": unreal.load_class(None, INTERACTION_COMPONENT_CLASS),
                "nav_bounds": unreal.load_class(None, NAV_BOUNDS_CLASS),
                "recast": unreal.load_class(None, RECAST_NAVMESH_CLASS),
            }
            if not all(STATE.classes.values()):
                raise RuntimeError("A required V4/Calysto/ACF live-soak class failed to load")
            set_phase("wait_editor_map")
            if LEVEL_EDITOR.load_level(CONTROL_MAP) is False:
                raise RuntimeError("HUB control map failed to load")
            return

        if STATE.phase == "wait_editor_map":
            if canonical_world(UNREAL_EDITOR.get_editor_world()).lower() != CONTROL_WORLD.lower():
                return
            if time.monotonic() - STATE.phase_started < 3.0:
                return
            set_phase("wait_control_pie")
            LEVEL_EDITOR.editor_request_begin_play()
            return

        world = game_world()
        if STATE.phase == "wait_control_pie":
            if not world or canonical_world(world).lower() != CONTROL_WORLD.lower():
                return
            director = director_for_world(world)
            if not director or world_time(world) < 1.0:
                return
            operation = {
                "operation": "request_start_new_run_with_seed",
                "source": "director_api",
                "from_floor": 0,
                "expected_floor": 1,
                "expected_serial": 1,
                "arguments": [RUN_SEED],
                "accepted": None,
            }
            STATE.operations.append(operation)
            begin_wait("new_run_floor_1", 1, 1, "NewRun", world)
            accepted = bool(reflected(director, "request_start_new_run_with_seed")(RUN_SEED))
            operation["accepted"] = accepted
            if not accepted:
                raise RuntimeError("Director rejected the deterministic V4 New Run")
            return

        if STATE.phase.startswith("wait_runtime_"):
            update_destination_world(world)
            if world and canonical_world(world).lower() == DUNGEON_WORLD.lower():
                STATE.saw_dungeon_world = True
                observe_pre_ready_door(world)
                director = director_for_world(world)
                if director and world_time(world) >= READY_WORLD_TIME:
                    sample = audit_runtime(world, director)
                    if sample:
                        handle_sample(world, sample)
                        return
            elif STATE.saw_dungeon_world and world and canonical_world(world).lower() == CONTROL_WORLD.lower():
                director = director_for_world(world)
                if director:
                    snapshot = reflected(director, "get_snapshot")()
                    raise RuntimeError("{}: {}".format(prop(snapshot, "FailureCode"), prop(snapshot, "FailureMessage")))
            return

        if STATE.phase.startswith("wait_door_selection_floor_"):
            if not world or canonical_world(world).lower() != DUNGEON_WORLD.lower():
                return
            sample_door_selection(world)
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v4_live_soak_pie58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v4_live_soak_pie58 = STATE
unreal.log("CALYSTO_V4_LIVE_SOAK validator_registered=true seed={}".format(RUN_SEED))
