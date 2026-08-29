"""Validate the Calysto Dungeon Director V3 contract in UE 5.8 PIE.

The protected launcher runs this script through Content/Python/init_unreal.py.
It loads the sole V3 policy read-only, exercises fixed-seed new-run/replay/
reroll, advances through the real ACF door from Floor 1 to the configured soak
floor, and then restarts the same seed. It measures Content, dirty-package,
runtime-residue, process-memory, and floor-ready deltas instead of assuming that
no asset changed.
"""

import builtins
import ctypes
import datetime
import hashlib
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
        "CODEX_CALYSTO_DIRECTOR_V3_PIE_OUTPUT",
        PROJECT_DIR
        / "Saved"
        / "Migration"
        / "CalystoDungeonDirectorV3"
        / "SeedReplayRerollAdvancePIE58.json",
    )
)
RUN_SEED = int(
    os.environ.get("CODEX_CALYSTO_DIRECTOR_V3_RUN_SEED", "2026080201")
)

DUNGEON_MAP = "/Game/Procedural/Maps/DungeonGeneration"
DUNGEON_WORLD_PATH = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD_PATH = "/Game/_Game/Hub/HUB.HUB"
DOOR_CLASS_PATH = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS_PATH = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
)
START_POINT_CLASS_PATH = (
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.BP_StartPoint_C"
)
INTERACTION_COMPONENT_CLASS_PATH = (
    "/Script/AscentCombatFramework.ACFInteractionComponent"
)
SUBSYSTEM_CLASS_PATH = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
ANCHOR_CLASS_PATH = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
NAV_BOUNDS_CLASS_PATH = "/Script/NavigationSystem.NavMeshBoundsVolume"
RECAST_NAVMESH_CLASS_PATH = "/Script/NavigationSystem.RecastNavMesh"
GENERATED_ACTOR_TAG = "PCG Generated Actor"

V3_POLICY_PATH = (
    "/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy"
)
V3_POLICY_CLASS_PATH = (
    "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicy"
)
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
MONITORED_ASSETS = (
    V3_POLICY_PATH,
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
)

MAX_TEST_FLOOR = int(
    os.environ.get("CODEX_CALYSTO_DIRECTOR_V3_MAX_TEST_FLOOR", "10")
)
GLOBAL_TIMEOUT_SECONDS = 1800.0
PHASE_TIMEOUT_SECONDS = 180.0
SELECTION_SAMPLES_REQUIRED = 3
FLOOR_READY_TIME_SECONDS = 3.0
POPULATION_TAG = "EF.Calysto.Population.V3"
START_POINT_REPAIR_TAG = "EF.Calysto.StartPointRepair.V3"
START_POINT_REPAIR_HASH_PREFIX = "EF.Calysto.StartPointRepairHash."
REQUIRE_START_POINT_REPAIR = (
    os.environ.get("CODEX_CALYSTO_DIRECTOR_V3_REQUIRE_START_REPAIR", "0") == "1"
)

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


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


def process_working_set_bytes():
    counters = ProcessMemoryCounters()
    counters.cb = ctypes.sizeof(counters)
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
    if not get_process_memory_info(
        get_current_process(), ctypes.byref(counters), counters.cb
    ):
        raise RuntimeError(
            "GetProcessMemoryInfo failed with Win32 error {}".format(
                ctypes.get_last_error()
            )
        )
    return int(counters.working_set_size)


def object_path(value):
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return "<invalid UObject>"


def canonical_world_path(world):
    return re.sub(r"uedpie_\d+_", "", object_path(world), flags=re.IGNORECASE)


def world_time(world):
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


def reflected(owner, method_name):
    method = getattr(owner, method_name, None)
    if not callable(method):
        raise RuntimeError(
            "Missing reflected method {} on {}".format(method_name, object_path(owner))
        )
    return method


def camel_to_snake(value):
    first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", value)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower()


def property_candidates(name):
    candidates = [name, camel_to_snake(name)]
    if len(name) > 1 and name[0] == "b" and name[1].isupper():
        candidates.append(camel_to_snake(name[1:]))
    candidates.append(name[0].lower() + name[1:])
    return list(dict.fromkeys(candidates))


def struct_property(owner, name):
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
    raise RuntimeError("Missing reflected property {} on {}".format(name, owner))


def reflected_bool_property(owner, *names):
    for name in names:
        try:
            return bool(struct_property(owner, name))
        except Exception:
            pass
    raise RuntimeError(
        "Missing reflected bool property {} on {}".format(
            "/".join(names), object_path(owner)
        )
    )


def enum_text(value):
    name = getattr(value, "name", None)
    if name:
        return str(name)
    return str(value).split(".")[-1]


def canonical_reference(value):
    for method_name in ("to_soft_object_path", "get_asset_path_name"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    return object_path(value) or str(value)


def vector_document(value):
    return {
        "x": int(struct_property(value, "X")),
        "y": int(struct_property(value, "Y")),
        "z": int(struct_property(value, "Z")),
    }


def snapshot_document(snapshot):
    return {
        "has_active_run": bool(struct_property(snapshot, "bHasActiveRun")),
        "policy_valid": bool(struct_property(snapshot, "bPolicyValid")),
        "policy_error": str(struct_property(snapshot, "PolicyError")),
        "run_seed": int(struct_property(snapshot, "RunSeed")),
        "floor_number": int(struct_property(snapshot, "FloorNumber")),
        "generation_serial": int(struct_property(snapshot, "GenerationSerial")),
        "pcg_seed": int(struct_property(snapshot, "PCGSeed")),
        "style": enum_text(struct_property(snapshot, "Style")),
        "dungeon_size": vector_document(struct_property(snapshot, "DungeonSize")),
        "candidate_anchor_density": float(
            struct_property(snapshot, "CandidateAnchorDensity")
        ),
        "side_path_chance": float(struct_property(snapshot, "SidePathChance")),
        "threat_budget": float(struct_property(snapshot, "ThreatBudget")),
        "resource_budget": float(struct_property(snapshot, "ResourceBudget")),
        "enemy_count": int(struct_property(snapshot, "EnemyCount")),
        "food_count": int(struct_property(snapshot, "FoodCount")),
        "chest_count": int(struct_property(snapshot, "ChestCount")),
        "loot_count": int(struct_property(snapshot, "LootCount")),
        "special_event_count": int(
            struct_property(snapshot, "SpecialEventCount")
        ),
        "realized_threat_cost": float(
            struct_property(snapshot, "RealizedThreatCost")
        ),
        "realized_resource_cost": float(
            struct_property(snapshot, "RealizedResourceCost")
        ),
        "policy_hash": str(struct_property(snapshot, "PolicyHash")),
        "ecology_hash": str(struct_property(snapshot, "EcologyHash")),
        "intent_hash": str(struct_property(snapshot, "IntentHash")),
        "manifest_hash": str(struct_property(snapshot, "ManifestHash")),
        "has_queued_director_intent": bool(
            struct_property(snapshot, "bHasQueuedDirectorIntent")
        ),
        "travel_kind": enum_text(struct_property(snapshot, "TravelKind")),
        "travel_state": enum_text(struct_property(snapshot, "TravelState")),
        "pending_floor_number": int(
            struct_property(snapshot, "PendingFloorNumber")
        ),
        "pending_generation_serial": int(
            struct_property(snapshot, "PendingGenerationSerial")
        ),
        "generation_state": enum_text(
            struct_property(snapshot, "GenerationState")
        ),
    }


def spawn_directive_document(item):
    return {
        "stable_id": str(struct_property(item, "StableId")),
        "category": enum_text(struct_property(item, "Category")),
        "actor_class": canonical_reference(struct_property(item, "ActorClass")),
        "count": int(struct_property(item, "Count")),
        "cost_per_actor": float(struct_property(item, "CostPerActor")),
        "relative_weight": int(struct_property(item, "RelativeWeight")),
    }


def outcome_document(outcome):
    return {
        "is_valid": bool(struct_property(outcome, "bIsValid")),
        "combat": float(struct_property(outcome, "Combat")),
        "survival": float(struct_property(outcome, "Survival")),
        "resources": float(struct_property(outcome, "Resources")),
        "pace": float(struct_property(outcome, "Pace")),
        "deaths": int(struct_property(outcome, "Deaths")),
    }


def intent_document(intent):
    theme_weights = []
    for item in list(struct_property(intent, "ThemeWeights")):
        theme_weights.append(
            {
                "theme_id": str(struct_property(item, "ThemeId")),
                "room_type": canonical_reference(struct_property(item, "RoomType")),
                "weight": int(struct_property(item, "Weight")),
            }
        )
    return {
        "is_valid": bool(struct_property(intent, "bIsValid")),
        "generator_version": int(struct_property(intent, "GeneratorVersion")),
        "run_seed": int(struct_property(intent, "RunSeed")),
        "floor_number": int(struct_property(intent, "FloorNumber")),
        "generation_serial": int(struct_property(intent, "GenerationSerial")),
        "pcg_seed": int(struct_property(intent, "PCGSeed")),
        "policy_hash": str(struct_property(intent, "PolicyHash")),
        "ecology_hash": str(struct_property(intent, "EcologyHash")),
        "intent_hash": str(struct_property(intent, "IntentHash")),
        "frozen_outcome": outcome_document(
            struct_property(intent, "FrozenOutcome")
        ),
        "outcome_hash": str(struct_property(intent, "OutcomeHash")),
        "style": enum_text(struct_property(intent, "Style")),
        "scale": float(struct_property(intent, "Scale")),
        "branching": float(struct_property(intent, "Branching")),
        "threat": float(struct_property(intent, "Threat")),
        "abundance": float(struct_property(intent, "Abundance")),
        "mystery": float(struct_property(intent, "Mystery")),
        "dungeon_size": vector_document(struct_property(intent, "DungeonSize")),
        "candidate_anchor_density": float(
            struct_property(intent, "CandidateAnchorDensity")
        ),
        "side_path_chance": float(struct_property(intent, "SidePathChance")),
        "room_min_size": int(struct_property(intent, "RoomMinSize")),
        "room_max_size": int(struct_property(intent, "RoomMaxSize")),
        "difficulty_tier": int(struct_property(intent, "DifficultyTier")),
        "threat_budget": float(struct_property(intent, "ThreatBudget")),
        "resource_budget": float(struct_property(intent, "ResourceBudget")),
        "enemy_presence_chance": float(
            struct_property(intent, "EnemyPresenceChance")
        ),
        "food_presence_chance": float(
            struct_property(intent, "FoodPresenceChance")
        ),
        "chest_presence_chance": float(
            struct_property(intent, "ChestPresenceChance")
        ),
        "loot_presence_chance": float(
            struct_property(intent, "LootPresenceChance")
        ),
        "special_event_presence_chance": float(
            struct_property(intent, "SpecialEventPresenceChance")
        ),
        "enemy_count": int(struct_property(intent, "EnemyCount")),
        "food_count": int(struct_property(intent, "FoodCount")),
        "chest_count": int(struct_property(intent, "ChestCount")),
        "loot_count": int(struct_property(intent, "LootCount")),
        "special_event_count": int(
            struct_property(intent, "SpecialEventCount")
        ),
        "dominant_theme": str(struct_property(intent, "DominantTheme")),
        "theme_weights": theme_weights,
        "spawn_directives": [
            spawn_directive_document(item)
            for item in list(struct_property(intent, "SpawnDirectives"))
        ],
    }


def manifest_document(manifest):
    return {
        "is_valid": bool(struct_property(manifest, "bIsValid")),
        "run_seed": int(struct_property(manifest, "RunSeed")),
        "floor_number": int(struct_property(manifest, "FloorNumber")),
        "generation_serial": int(struct_property(manifest, "GenerationSerial")),
        "pcg_seed": int(struct_property(manifest, "PCGSeed")),
        "intent_hash": str(struct_property(manifest, "IntentHash")),
        "anchor_topology_hash": str(
            struct_property(manifest, "AnchorTopologyHash")
        ),
        "population_hash": str(struct_property(manifest, "PopulationHash")),
        "resource_hash": str(struct_property(manifest, "ResourceHash")),
        "manifest_hash": str(struct_property(manifest, "ManifestHash")),
        "candidate_anchor_count": int(
            struct_property(manifest, "CandidateAnchorCount")
        ),
        "enemy_count": int(struct_property(manifest, "EnemyCount")),
        "food_count": int(struct_property(manifest, "FoodCount")),
        "chest_count": int(struct_property(manifest, "ChestCount")),
        "loot_count": int(struct_property(manifest, "LootCount")),
        "special_event_count": int(
            struct_property(manifest, "SpecialEventCount")
        ),
        "realized_threat_cost": float(
            struct_property(manifest, "RealizedThreatCost")
        ),
        "realized_resource_cost": float(
            struct_property(manifest, "RealizedResourceCost")
        ),
        "spawned_actor_count": int(
            struct_property(manifest, "SpawnedActorCount")
        ),
        "spawn_directives": [
            spawn_directive_document(item)
            for item in list(struct_property(manifest, "SpawnDirectives"))
        ],
    }


def floor_record_document(intent, manifest):
    intent_doc = intent_document(intent)
    manifest_doc = manifest_document(manifest)
    return {
        **intent_doc,
        "manifest_hash": manifest_doc["manifest_hash"],
        "realized_threat_cost": manifest_doc["realized_threat_cost"],
        "realized_resource_cost": manifest_doc["realized_resource_cost"],
        "intent": intent_doc,
        "manifest": manifest_doc,
    }


def snapshot_matches_floor_record(snapshot, record):
    fields = (
        "run_seed",
        "floor_number",
        "generation_serial",
        "pcg_seed",
        "style",
        "dungeon_size",
        "candidate_anchor_density",
        "side_path_chance",
        "threat_budget",
        "resource_budget",
        "enemy_count",
        "food_count",
        "chest_count",
        "loot_count",
        "special_event_count",
        "realized_threat_cost",
        "realized_resource_cost",
        "policy_hash",
        "ecology_hash",
        "intent_hash",
        "manifest_hash",
    )
    return all(snapshot[field] == record[field] for field in fields)


def find_game_instance_subsystem(world, subsystem_class):
    game_instance = unreal.GameplayStatics.get_game_instance(world) if world else None
    if not game_instance or not subsystem_class:
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
    game_instance_path = object_path(game_instance)
    for candidate in unreal.ObjectIterator(unreal.Object):
        try:
            if (
                candidate.get_class() == subsystem_class
                and object_path(candidate).startswith(game_instance_path)
            ):
                return candidate
        except Exception:
            continue
    return None


def normalized_schema_name(value):
    return re.sub(r"[^a-z0-9]", "", str(value).lower())


def content_snapshot():
    result = {}
    for suffix in ("*.uasset", "*.umap"):
        for path in CONTENT_ROOT.rglob(suffix):
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size),
                int(stat.st_mtime_ns),
            )
    return result


def package_from_relative(relative_path):
    return "/Game/" + relative_path.rsplit(".", 1)[0]


def package_is_dirty(asset):
    if not asset:
        return False
    try:
        package = asset.get_outermost()
        method = getattr(package, "is_dirty", None)
        return bool(method()) if callable(method) else False
    except Exception:
        return False


def monitored_dirty_states():
    states = {}
    for asset_path in MONITORED_ASSETS:
        asset = unreal.load_asset(asset_path)
        states[asset_path] = package_is_dirty(asset)
    return states


def validate_v3_policy_read_only():
    asset = unreal.load_asset(V3_POLICY_PATH)
    policy_class = unreal.load_class(None, V3_POLICY_CLASS_PATH)
    if asset is None:
        raise RuntimeError("Missing Dungeon Director V3 policy: " + V3_POLICY_PATH)
    if policy_class is None:
        raise RuntimeError("Missing V3 native policy class: " + V3_POLICY_CLASS_PATH)
    if asset.get_class() != policy_class:
        raise RuntimeError(
            "{} uses {} instead of {}".format(
                V3_POLICY_PATH, object_path(asset.get_class()), V3_POLICY_CLASS_PATH
            )
        )

    schema_version = int(struct_property(asset, "SchemaVersion"))
    generator_version = int(struct_property(asset, "GeneratorVersion"))
    policy_id = str(struct_property(asset, "PolicyId"))
    validated_sizes = list(struct_property(asset, "ValidatedDungeonSizes"))
    limits = struct_property(asset, "Limits")
    policy_population_caps = {
        "enemy_count": int(struct_property(limits, "MaxEnemies")),
        "food_count": int(struct_property(limits, "MaxFood")),
        "chest_count": int(struct_property(limits, "MaxChests")),
        "loot_count": int(struct_property(limits, "MaxLoot")),
        "special_event_count": int(
            struct_property(limits, "MaxSpecialEvents")
        ),
        "spawned_actor_count": int(
            struct_property(limits, "MaxDirectorActors")
        ),
    }
    if schema_version != 3 or generator_version != 3:
        raise RuntimeError(
            "V3 policy version mismatch: schema={} generator={}".format(
                schema_version, generator_version
            )
        )
    if not policy_id or policy_id.lower() == "none":
        raise RuntimeError("V3 policy has no stable PolicyId")
    if not validated_sizes:
        raise RuntimeError("V3 policy has no validated dungeon sizes")

    validator = getattr(asset, "validate_policy", None)
    if not callable(validator):
        raise RuntimeError("V3 policy has no reflected ValidatePolicy wrapper")
    validation_result = validator()
    validation_error = ""
    if isinstance(validation_result, tuple):
        if not validation_result:
            raise RuntimeError("ValidatePolicy returned an empty tuple")
        validation_ok = bool(validation_result[0])
        if len(validation_result) > 1:
            validation_error = str(validation_result[1])
    elif isinstance(validation_result, bool):
        validation_ok = validation_result
    else:
        raise RuntimeError(
            "ValidatePolicy returned unsupported Python value {!r}".format(
                validation_result
            )
        )
    if not validation_ok:
        raise RuntimeError("Native V3 policy validation failed: " + validation_error)

    policy_file = (
        CONTENT_ROOT
        / "_Game"
        / "Data"
        / "CalystoDungeon"
        / "V3"
        / "DA_CalystoDungeonDirectorPolicy.uasset"
    )
    if not policy_file.is_file():
        raise RuntimeError("V3 policy package is absent on disk")
    return {
        "path": V3_POLICY_PATH,
        "class": object_path(asset.get_class()),
        "schema_version": schema_version,
        "generator_version": generator_version,
        "policy_id": policy_id,
        "validated_dungeon_size_count": len(validated_sizes),
        "population_caps": policy_population_caps,
        "native_validation": "PASS",
        "sha256": hashlib.sha256(policy_file.read_bytes()).hexdigest().upper(),
        "access": "read_only",
    }


def component_is_editor_only(component):
    method = getattr(component, "is_editor_only", None)
    if callable(method):
        try:
            return bool(method())
        except Exception:
            pass
    for name in ("bIsEditorOnly", "is_editor_only"):
        try:
            return bool(struct_property(component, name))
        except Exception:
            pass
    return False


def overlapping_actor_paths(component):
    try:
        return sorted(object_path(actor) for actor in component.get_overlapping_actors())
    except Exception:
        return []


def quantized_transform(actor):
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    scale = actor.get_actor_scale3d()
    return (
        round(float(location.x), 1),
        round(float(location.y), 1),
        round(float(location.z), 1),
        round(float(rotation.pitch), 1),
        round(float(rotation.yaw), 1),
        round(float(rotation.roll), 1),
        round(float(scale.x), 3),
        round(float(scale.y), 3),
        round(float(scale.z), 3),
    )


def runtime_layout_fingerprint(world, dungeons, doors):
    """Hash stable generated non-pawn actor classes and transforms.

    This is Development-only evidence. It does not retain a layout history in
    the runtime plugin and deliberately excludes moving pawns/player state.
    """
    generated = list(
        unreal.GameplayStatics.get_all_actors_with_tag(world, GENERATED_ACTOR_TAG)
    )
    static_generated = [
        actor
        for actor in generated
        if actor and not isinstance(actor, unreal.Pawn)
    ]
    used_tagged_records = bool(static_generated)
    if not static_generated:
        # Defensive fallback for PCG nodes that do not propagate the standard
        # tag: retain only Calysto/project door actors, never gameplay pawns.
        all_actors = actors_of_class(world, unreal.Actor)
        static_generated = [
            actor
            for actor in all_actors
            if actor
            and not isinstance(actor, unreal.Pawn)
            and (
                "/Game/Calysto/" in object_path(actor.get_class())
                or actor in dungeons
                or actor in doors
            )
        ]
    records = sorted(
        (
            object_path(actor.get_class()),
            *quantized_transform(actor),
        )
        for actor in static_generated
    )
    if not records:
        raise RuntimeError("No stable generated actors were available for fingerprinting")
    canonical = json.dumps(records, separators=(",", ":"), ensure_ascii=True)
    return {
        "sha256": hashlib.sha256(canonical.encode("utf-8")).hexdigest().upper(),
        "record_count": len(records),
        "source": (
            "pcg_generated_actor_tag"
            if used_tagged_records
            else "calysto_actor_fallback"
        ),
    }


class State:
    def __init__(self):
        self.phase = "load_map"
        self.started_at = time.monotonic()
        self.phase_started_at = self.started_at
        self.callback = None
        self.finished = False
        self.door_class = None
        self.dungeon_class = None
        self.start_point_class = None
        self.interaction_component_class = None
        self.subsystem_class = None
        self.anchor_class = None
        self.nav_bounds_class = None
        self.recast_navmesh_class = None
        self.subsystem = None
        self.door = None
        self.player = None
        self.interaction_component = None
        self.current_floor_record = None
        self.wait_label = ""
        self.expected_floor = 0
        self.expected_serial = 0
        self.expected_kind = ""
        self.wait_started_at = self.started_at
        self.selection_samples = 0
        self.interactions = 0
        self.last_runtime_diagnostic_at = 0.0
        self.phase_history = []
        self.floor_samples = []
        self.operations = []
        self.v3_policy = {}
        self.initial_content_snapshot = content_snapshot()
        self.initial_dirty_states = monitored_dirty_states()
        self.final_dirty_states = dict(self.initial_dirty_states)
        self.asset_mutations = []
        self.asset_saves = []
        self.soak_metrics = {}
        self.baseline_floor_record = None
        self.replay_floor_record = None
        self.reroll_floor_record = None
        self.door_disabled_observed = {}
        self.checks = {
            "v3_policy_read_only": False,
            "v3_policy_class_valid": False,
            "v3_policy_versions_valid": False,
            "map_loaded": False,
            "pie_started": False,
            "fixed_seed_new_run": False,
            "floor_1_generation_1": False,
            "replay_accepted": False,
            "replay_same_floor_and_serial": False,
            "replay_exact_intent_and_manifest": False,
            "reroll_accepted": False,
            "reroll_same_floor_serial_incremented": False,
            "reroll_changed_intent_manifest_and_pcg_seed": False,
            "floor_1_door_selected": False,
            "floor_1_door_interacted": False,
            "door_labels_increment_floor": False,
            "no_floor_3_cap": False,
            "floor_1_to_configured_max_via_real_doors": False,
            "configured_max_floor_door_enabled_and_selectable": False,
            "debug_jump_floor_25": False,
            "debug_jump_floor_50": False,
            "debug_jump_floor_100": False,
            "generated_context_pcg_seeds_unique": False,
            "same_seed_restart_floor_1_generation_1": False,
            "same_seed_restart_exact_intent_and_manifest": False,
            "replay_layout_fingerprint_exact": False,
            "reroll_layout_fingerprint_changed": False,
            "same_seed_restart_layout_fingerprint_exact": False,
            "generated_layout_fingerprints_distinct": False,
            "one_runtime_dungeon_door_and_pcg_per_sample": False,
            "runtime_pcg_completed": False,
            "runtime_generation_state_ready": False,
            "navigation_present": False,
            "realized_manifests_valid": False,
            "manifest_hashes_present": False,
            "population_caps_respected": False,
            "population_actor_residue_absent": False,
            "start_point_contract_valid": False,
            "required_start_point_repair_exercised": not REQUIRE_START_POINT_REPAIR,
            "same_seed_restart_world_actor_count_exact": False,
            "working_set_bounded_and_non_monotonic": False,
            "floor_ready_p95_under_30_seconds": False,
            "door_disabled_before_readiness": False,
            "floor_record_snapshot_consistent": False,
            "positive_pcg_seed_range": False,
            "policy_hash_stable": False,
            "run_seed_stable": False,
            "queued_director_intent_absent": False,
            "asset_mutations_empty": False,
            "asset_saves_empty": False,
        }
        for floor in range(2, MAX_TEST_FLOOR + 1):
            self.checks["floor_{}_runtime_ready".format(floor)] = False
        for floor in range(1, MAX_TEST_FLOOR):
            self.checks["floor_{}_door_selected".format(floor)] = False
            self.checks["floor_{}_door_interacted".format(floor)] = False
        self.error = ""


STATE = State()


def phase_age():
    return time.monotonic() - STATE.phase_started_at


def set_phase(value):
    STATE.phase = value
    STATE.phase_started_at = time.monotonic()
    STATE.phase_history.append(
        {
            "phase": value,
            "elapsed_seconds": round(STATE.phase_started_at - STATE.started_at, 3),
        }
    )
    unreal.log("[CalystoDungeonMasterPIE58] phase=" + value)


def refresh_asset_delta():
    current_content = content_snapshot()
    disk_changes = sorted(
        package_from_relative(path)
        for path in set(STATE.initial_content_snapshot) | set(current_content)
        if STATE.initial_content_snapshot.get(path) != current_content.get(path)
    )
    current_dirty = monitored_dirty_states()
    STATE.final_dirty_states = current_dirty
    newly_dirty = sorted(
        path
        for path, dirty in current_dirty.items()
        if dirty and not STATE.initial_dirty_states.get(path, False)
    )
    STATE.asset_saves = disk_changes
    STATE.asset_mutations = sorted(set(disk_changes) | set(newly_dirty))
    STATE.checks["asset_mutations_empty"] = not STATE.asset_mutations
    STATE.checks["asset_saves_empty"] = not STATE.asset_saves


def evidence(success):
    refresh_asset_delta()
    return {
        "schema_version": 3,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": (
            "UE58_CALYSTO_DUNGEON_DIRECTOR_V3_PIE_PASS"
            if success
            else "UE58_CALYSTO_DUNGEON_DIRECTOR_V3_PIE_FAIL"
        ),
        "success": bool(success),
        "run_seed": RUN_SEED,
        "max_test_floor": MAX_TEST_FLOOR,
        "require_start_point_repair": REQUIRE_START_POINT_REPAIR,
        "control_map": CONTROL_MAP,
        "dungeon_map": DUNGEON_MAP,
        "class_paths": {
            "door": DOOR_CLASS_PATH,
            "dungeon": DUNGEON_CLASS_PATH,
            "start_point": START_POINT_CLASS_PATH,
            "interaction_component": INTERACTION_COMPONENT_CLASS_PATH,
            "subsystem": SUBSYSTEM_CLASS_PATH,
            "anchor": ANCHOR_CLASS_PATH,
        },
        "contract": (
            "Dungeon Director V3 GameInstance-only ecology; no SaveGame persistence. "
            "A fixed-seed run starts at Floor 1 / GenerationSerial 1; replay preserves "
            "the exact floor intent and realized manifest; reroll advances the serial "
            "on the same floor; the real ACF door advances through Floor {}."
        ).format(MAX_TEST_FLOOR),
        "v3_policy": STATE.v3_policy,
        "checks": STATE.checks,
        "operations": STATE.operations,
        "interactions": STATE.interactions,
        "floor_samples": STATE.floor_samples,
        "soak_metrics": STATE.soak_metrics,
        "phase": STATE.phase,
        "phase_history": STATE.phase_history,
        "error": STATE.error,
        "asset_mutations": STATE.asset_mutations,
        "asset_saves": STATE.asset_saves,
        "asset_monitor": {
            "method": "all Content package size/mtime delta plus newly dirty monitored Calysto/V3 packages",
            "initial_content_package_count": len(STATE.initial_content_snapshot),
            "initial_dirty_states": STATE.initial_dirty_states,
            "final_dirty_states": STATE.final_dirty_states,
        },
    }


def write_result(success):
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    document = evidence(success)
    OUTPUT_FILE.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log(
        "[CalystoDungeonMasterPIE58] result="
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
    builtins._codex_calysto_dungeon_master_pie58 = None
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


def begin_wait(label, floor, serial, kind):
    STATE.wait_label = label
    STATE.expected_floor = int(floor)
    STATE.expected_serial = int(serial)
    STATE.expected_kind = kind
    STATE.wait_started_at = time.monotonic()
    STATE.subsystem = None
    STATE.door = None
    STATE.player = None
    STATE.interaction_component = None
    STATE.current_floor_record = None
    STATE.door_disabled_observed[label] = False
    set_phase("wait_runtime_" + label)


def context_matches(snapshot, floor, serial, kind):
    return (
        snapshot["floor_number"] == floor
        and snapshot["generation_serial"] == serial
        and normalized_schema_name(kind)
        in normalized_schema_name(snapshot["travel_kind"])
    )


def observe_pre_ready_door(world):
    subsystem = find_game_instance_subsystem(world, STATE.subsystem_class)
    if not subsystem:
        return
    snapshot = snapshot_document(reflected(subsystem, "get_snapshot")())
    if not context_matches(
        snapshot, STATE.expected_floor, STATE.expected_serial, STATE.expected_kind
    ):
        return
    doors = actors_of_class(world, STATE.door_class)
    if len(doors) != 1:
        return
    if not reflected_bool_property(doors[0], "bIsEnabled", "is_enabled"):
        STATE.door_disabled_observed[STATE.wait_label] = True


def resolve_expected_runtime(world):
    subsystem = find_game_instance_subsystem(world, STATE.subsystem_class)
    if not subsystem:
        return None
    snapshot = snapshot_document(reflected(subsystem, "get_snapshot")())
    if not snapshot["policy_valid"]:
        raise RuntimeError("Dungeon Director V3 policy invalid: " + snapshot["policy_error"])
    if snapshot["policy_error"]:
        raise RuntimeError(
            "Calysto reported a policy error despite a valid flag: "
            + snapshot["policy_error"]
        )
    if snapshot["has_queued_director_intent"]:
        raise RuntimeError("Dungeon Director V3 PIE requires no queued debug intent")
    if not snapshot["has_active_run"]:
        return None
    if not context_matches(
        snapshot, STATE.expected_floor, STATE.expected_serial, STATE.expected_kind
    ):
        return None
    if (
        "idle" not in snapshot["travel_state"].lower()
        or snapshot["pending_floor_number"] != 0
        or snapshot["pending_generation_serial"] != 0
    ):
        return None

    intent = reflected(subsystem, "get_resolved_floor_intent")()
    manifest = reflected(subsystem, "get_realized_floor_manifest")()
    floor_record = floor_record_document(intent, manifest)
    if (
        not floor_record["is_valid"]
        or not floor_record["manifest"]["is_valid"]
        or floor_record["intent_hash"] != floor_record["manifest"]["intent_hash"]
        or floor_record["run_seed"] != floor_record["manifest"]["run_seed"]
        or floor_record["floor_number"] != floor_record["manifest"]["floor_number"]
        or floor_record["generation_serial"]
        != floor_record["manifest"]["generation_serial"]
        or floor_record["pcg_seed"] != floor_record["manifest"]["pcg_seed"]
        or not snapshot_matches_floor_record(snapshot, floor_record)
    ):
        return None
    if int(reflected(subsystem, "get_current_floor")()) != STATE.expected_floor:
        return None

    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    doors = actors_of_class(world, STATE.door_class)
    dungeons = actors_of_class(world, STATE.dungeon_class)
    start_points = actors_of_class(world, STATE.start_point_class)
    interaction_components = components_of_class(
        player, STATE.interaction_component_class
    )
    nav_bounds = actors_of_class(world, STATE.nav_bounds_class)
    recast_navmeshes = actors_of_class(world, STATE.recast_navmesh_class)
    anchors = actors_of_class(world, STATE.anchor_class)
    population_actors = list(
        unreal.GameplayStatics.get_all_actors_with_tag(world, POPULATION_TAG)
    )
    world_actors = actors_of_class(world, unreal.Actor)
    all_pcg_components = []
    for dungeon in dungeons:
        all_pcg_components.extend(dungeon.get_components_by_class(unreal.PCGComponent))
    runtime_pcg_components = [
        component
        for component in all_pcg_components
        if not component_is_editor_only(component)
    ]
    generating_states = []
    pcg_state_sources = []
    for component in runtime_pcg_components:
        method = getattr(component, "is_generating", None)
        if callable(method):
            generating_states.append(bool(method()))
            pcg_state_sources.append("python_is_generating")
        else:
            # UE 5.8 does not expose UPCGComponent::IsGenerating to Python on
            # every build. Reaching Idle with the door enabled is only possible
            # after the native generated delegate, navigation, and one-shot
            # NotifyFloorReady gate have all succeeded.
            generating_states.append(False)
            pcg_state_sources.append("native_floor_ready_gate")

    if (
        not player
        or len(doors) != 1
        or len(dungeons) != 1
        or len(start_points) != 1
        or len(interaction_components) != 1
        or len(runtime_pcg_components) != 1
        or any(generating_states)
        or not nav_bounds
        or not recast_navmeshes
    ):
        now = time.monotonic()
        if now - STATE.last_runtime_diagnostic_at >= 5.0:
            STATE.last_runtime_diagnostic_at = now
            unreal.log(
                "[CalystoDungeonMasterPIE58] readiness_blockers="
                + json.dumps(
                    {
                        "label": STATE.wait_label,
                        "player": bool(player),
                        "doors": len(doors),
                        "dungeons": len(dungeons),
                        "start_points": len(start_points),
                        "interaction_components": len(interaction_components),
                        "all_pcg_components": len(all_pcg_components),
                        "runtime_pcg_components": len(runtime_pcg_components),
                        "pcg_generating_states": generating_states,
                        "nav_bounds": len(nav_bounds),
                        "recast_navmeshes": len(recast_navmeshes),
                    },
                    sort_keys=True,
                )
            )
        return None
    door_enabled = reflected_bool_property(doors[0], "bIsEnabled", "is_enabled")
    if not door_enabled:
        return None
    door_label = str(reflected(doors[0], "get_interactable_name")())
    expected_door_label = "Floor {} → {}".format(
        STATE.expected_floor, STATE.expected_floor + 1
    )
    if door_label != expected_door_label:
        raise RuntimeError(
            "Generated door label {!r} did not match {!r}".format(
                door_label, expected_door_label
            )
        )

    STATE.subsystem = subsystem
    STATE.player = player
    STATE.door = doors[0]
    STATE.interaction_component = interaction_components[0]
    STATE.current_floor_record = floor_record
    layout_fingerprint = runtime_layout_fingerprint(world, dungeons, doors)
    start_point_tags = sorted(
        str(value) for value in start_points[0].get_editor_property("tags")
    )
    start_point_repair_hashes = sorted(
        value[len(START_POINT_REPAIR_HASH_PREFIX) :]
        for value in start_point_tags
        if value.startswith(START_POINT_REPAIR_HASH_PREFIX)
    )
    return {
        "label": STATE.wait_label,
        "world": object_path(world),
        "world_time_seconds": round(world_time(world), 3),
        "snapshot": snapshot,
        "floor_record": floor_record,
        "runtime": {
            "dungeon": object_path(dungeons[0]),
            "door": object_path(doors[0]),
            "door_runtime_enabled": door_enabled,
            "door_label": door_label,
            "start_point": object_path(start_points[0]),
            "start_point_count": len(start_points),
            "start_point_transform": list(quantized_transform(start_points[0])),
            "start_point_repair_applied": START_POINT_REPAIR_TAG
            in start_point_tags,
            "start_point_repair_hashes": start_point_repair_hashes,
            "player": object_path(player),
            "interaction_component": object_path(interaction_components[0]),
            "pcg_component_count": len(all_pcg_components),
            "runtime_pcg_component_count": len(runtime_pcg_components),
            "pcg_generating_states": generating_states,
            "pcg_state_sources": pcg_state_sources,
            "nav_bounds_count": len(nav_bounds),
            "recast_navmesh_count": len(recast_navmeshes),
            "anchor_count_after_ready": len(anchors),
            "population_actor_count": len(population_actors),
            "world_actor_count": len(world_actors),
            "working_set_bytes": process_working_set_bytes(),
            "floor_ready_elapsed_seconds": round(
                time.monotonic() - STATE.wait_started_at, 3
            ),
            "door_disabled_before_ready_observed": STATE.door_disabled_observed.get(
                STATE.wait_label, False
            ),
            "layout_fingerprint": layout_fingerprint,
        },
    }


def require_operation(method_name, operation, *args):
    accepted = bool(reflected(STATE.subsystem, method_name)(*args))
    STATE.operations.append(
        {
            "operation": operation,
            "accepted": accepted,
            "from_floor_record": STATE.current_floor_record,
            "arguments": list(args),
        }
    )
    if not accepted:
        raise RuntimeError("Calysto rejected operation: " + operation)


def arm_start_point_suppression(world, context):
    unreal.SystemLibrary.execute_console_command(
        world, "EF.Calysto.Automation.SuppressStartPointOnce"
    )
    STATE.operations.append(
        {
            "operation": "development_suppress_start_point_once",
            "dispatched": True,
            "context": context,
            "arguments": [],
        }
    )


def handle_runtime_sample(sample):
    label = sample["label"]
    STATE.floor_samples.append(sample)
    floor_record = sample["floor_record"]
    if not STATE.door_disabled_observed.get(label, False):
        raise RuntimeError(
            "Did not observe the generated door disabled before readiness for " + label
        )
    if label == "fixed_new_run":
        STATE.baseline_floor_record = floor_record
        STATE.checks["fixed_seed_new_run"] = floor_record["run_seed"] == RUN_SEED
        STATE.checks["floor_1_generation_1"] = (
            floor_record["floor_number"] == 1
            and floor_record["generation_serial"] == 1
        )
        set_phase("request_replay")
        return
    if label == "replay":
        STATE.replay_floor_record = floor_record
        STATE.checks["replay_same_floor_and_serial"] = (
            floor_record["floor_number"] == 1
            and floor_record["generation_serial"] == 1
        )
        STATE.checks["replay_exact_intent_and_manifest"] = (
            floor_record == STATE.baseline_floor_record
        )
        set_phase("request_reroll")
        return
    if label == "reroll":
        STATE.reroll_floor_record = floor_record
        STATE.checks["reroll_same_floor_serial_incremented"] = (
            floor_record["floor_number"] == 1
            and floor_record["generation_serial"] == 2
        )
        STATE.checks["reroll_changed_intent_manifest_and_pcg_seed"] = (
            floor_record["intent_hash"]
            != STATE.baseline_floor_record["intent_hash"]
            and floor_record["manifest_hash"]
            != STATE.baseline_floor_record["manifest_hash"]
            and floor_record["pcg_seed"]
            != STATE.baseline_floor_record["pcg_seed"]
        )
        begin_selection()
        return
    if label.startswith("floor_"):
        floor = int(label.split("_", 1)[1])
        if floor < 2 or floor > MAX_TEST_FLOOR:
            raise RuntimeError("Unexpected advanced Floor sample: " + label)
        STATE.checks["floor_{}_runtime_ready".format(floor)] = True
        if floor >= 4:
            STATE.checks["no_floor_3_cap"] = True
        begin_selection()
        return
    if label.startswith("debug_"):
        floor = int(label.split("_", 1)[1])
        STATE.checks["debug_jump_floor_{}".format(floor)] = True
        if floor == 25:
            set_phase("request_debug_jump_50")
        elif floor == 50:
            set_phase("request_debug_jump_100")
        elif floor == 100:
            set_phase("request_same_seed_restart")
        else:
            raise RuntimeError("Unexpected debug-jump sample: " + label)
        return
    if label == "same_seed_restart":
        STATE.checks["same_seed_restart_floor_1_generation_1"] = (
            floor_record["run_seed"] == RUN_SEED
            and floor_record["floor_number"] == 1
            and floor_record["generation_serial"] == 1
        )
        STATE.checks["same_seed_restart_exact_intent_and_manifest"] = (
            floor_record == STATE.baseline_floor_record
        )
        finalize_success()
        return
    raise RuntimeError("Unknown runtime sample label: " + label)


def begin_selection():
    door_location = STATE.door.get_actor_location()
    requested_location = unreal.Vector(
        door_location.x - 80.0,
        door_location.y - 80.0,
        door_location.z,
    )
    STATE.player.set_actor_location(requested_location, False, True)
    distance = float(STATE.player.get_distance_to(STATE.door))
    if distance > 200.0:
        raise RuntimeError(
            "Player remained outside Calysto door range: {:.3f} cm".format(distance)
        )
    reflected(STATE.interaction_component, "enable_detection")(False)
    reflected(STATE.interaction_component, "enable_detection")(True)
    reflected(STATE.interaction_component, "refresh_interactions")()
    STATE.selection_samples = 0
    set_phase(
        "wait_selection_floor_{}".format(
            STATE.current_floor_record["floor_number"]
        )
    )


def sample_selection():
    reflected(STATE.interaction_component, "refresh_interactions")()
    best = reflected(
        STATE.interaction_component, "get_current_best_interactable_actor"
    )()
    door_path = object_path(STATE.door)
    overlaps = overlapping_actor_paths(STATE.interaction_component)
    if object_path(best) == door_path and door_path in overlaps:
        STATE.selection_samples += 1
    else:
        STATE.selection_samples = 0


def handle_selected_door():
    floor = STATE.current_floor_record["floor_number"]
    serial = STATE.current_floor_record["generation_serial"]
    STATE.checks["floor_{}_door_selected".format(floor)] = True
    if floor == MAX_TEST_FLOOR:
        STATE.checks["configured_max_floor_door_enabled_and_selectable"] = True
        STATE.checks["floor_1_to_configured_max_via_real_doors"] = (
            STATE.interactions == MAX_TEST_FLOOR - 1
        )
        set_phase("request_debug_jump_25")
        return
    best = reflected(
        STATE.interaction_component, "get_current_best_interactable_actor"
    )()
    if object_path(best) != object_path(STATE.door):
        raise RuntimeError("Generated floor door lost ACF selection before Interact")
    reflected(STATE.interaction_component, "interact")('')
    STATE.checks["floor_{}_door_interacted".format(floor)] = True
    STATE.operations.append(
        {
            "operation": "advance_via_real_acf_door",
            "dispatched": True,
            "acceptance_gate": "matching destination runtime sample",
            "from_floor_record": STATE.current_floor_record,
            "expected_destination": {
                "floor_number": floor + 1,
                "generation_serial": serial + 1,
            },
            "arguments": [],
        }
    )
    STATE.interactions += 1
    begin_wait("floor_{}".format(floor + 1), floor + 1, serial + 1, "Advance")


def finalize_success():
    samples = list(STATE.floor_samples)
    required_disabled_labels = {
        "fixed_new_run",
        "replay",
        "reroll",
        "same_seed_restart",
        "debug_25",
        "debug_50",
        "debug_100",
        *("floor_{}".format(floor) for floor in range(2, MAX_TEST_FLOOR + 1)),
    }
    STATE.checks["one_runtime_dungeon_door_and_pcg_per_sample"] = all(
        sample["runtime"]["runtime_pcg_component_count"] == 1
        and bool(sample["runtime"]["dungeon"])
        and bool(sample["runtime"]["door"])
        for sample in samples
    )
    STATE.checks["runtime_pcg_completed"] = all(
        not any(sample["runtime"]["pcg_generating_states"]) for sample in samples
    )
    STATE.checks["runtime_generation_state_ready"] = all(
        "ready" in sample["snapshot"]["generation_state"].lower()
        for sample in samples
    )
    STATE.checks["navigation_present"] = all(
        sample["runtime"]["nav_bounds_count"] > 0
        and sample["runtime"]["recast_navmesh_count"] > 0
        for sample in samples
    )
    STATE.checks["realized_manifests_valid"] = all(
        sample["floor_record"]["manifest"]["is_valid"]
        and sample["floor_record"]["manifest"]["intent_hash"]
        == sample["floor_record"]["intent_hash"]
        for sample in samples
    )
    STATE.checks["manifest_hashes_present"] = all(
        all(
            sample["floor_record"]["manifest"][field]
            for field in (
                "anchor_topology_hash",
                "population_hash",
                "resource_hash",
                "manifest_hash",
            )
        )
        for sample in samples
    )
    policy_caps = STATE.v3_policy["population_caps"]
    hard_caps = {
        "enemy_count": 25,
        "food_count": 8,
        "chest_count": 3,
        "loot_count": 4,
        "special_event_count": 4,
        "spawned_actor_count": 36,
    }
    STATE.checks["population_caps_respected"] = all(
        all(
            0 <= sample["floor_record"]["manifest"][field]
            <= min(policy_caps[field], hard_caps[field])
            for field in hard_caps
        )
        for sample in samples
    )
    STATE.checks["population_actor_residue_absent"] = all(
        sample["runtime"]["anchor_count_after_ready"] == 0
        and sample["runtime"]["population_actor_count"]
        == sample["floor_record"]["manifest"]["spawned_actor_count"]
        for sample in samples
    )
    STATE.checks["start_point_contract_valid"] = all(
        sample["runtime"]["start_point_count"] == 1
        and (
            (
                sample["runtime"]["start_point_repair_applied"]
                and len(sample["runtime"]["start_point_repair_hashes"]) == 1
                and bool(sample["runtime"]["start_point_repair_hashes"][0])
            )
            or (
                not sample["runtime"]["start_point_repair_applied"]
                and not sample["runtime"]["start_point_repair_hashes"]
            )
        )
        for sample in samples
    )
    start_repair_samples = [
        sample["label"]
        for sample in samples
        if sample["runtime"]["start_point_repair_applied"]
    ]
    if REQUIRE_START_POINT_REPAIR:
        required_repair_labels = {
            "fixed_new_run",
            "replay",
            "same_seed_restart",
        }
        repaired_by_label = {
            sample["label"]: sample
            for sample in samples
            if sample["runtime"]["start_point_repair_applied"]
        }
        repair_records = {
            json.dumps(
                {
                    "transform": repaired_by_label[label]["runtime"][
                        "start_point_transform"
                    ],
                    "hashes": repaired_by_label[label]["runtime"][
                        "start_point_repair_hashes"
                    ],
                },
                sort_keys=True,
            )
            for label in required_repair_labels
            if label in repaired_by_label
        }
        STATE.checks["required_start_point_repair_exercised"] = (
            required_repair_labels.issubset(repaired_by_label)
            and len(repair_records) == 1
        )

    ready_durations = sorted(
        float(sample["runtime"]["floor_ready_elapsed_seconds"])
        for sample in samples
    )
    p95_index = max(0, min(len(ready_durations) - 1, int(len(ready_durations) * 0.95 + 0.999999) - 1))
    ready_p95 = ready_durations[p95_index]
    STATE.checks["floor_ready_p95_under_30_seconds"] = ready_p95 < 30.0

    memory_values = [
        int(sample["runtime"]["working_set_bytes"]) for sample in samples
    ]
    post_warmup_memory = memory_values[min(3, len(memory_values) - 1) :]
    memory_decreases = sum(
        1
        for previous, current in zip(post_warmup_memory, post_warmup_memory[1:])
        if current < previous
    )
    memory_growth = max(post_warmup_memory) - min(post_warmup_memory)
    memory_growth_limit = 2 * 1024 * 1024 * 1024
    STATE.checks["working_set_bounded_and_non_monotonic"] = (
        memory_growth <= memory_growth_limit and memory_decreases > 0
    )
    STATE.soak_metrics = {
        "generation_count": len(samples),
        "configured_max_floor": MAX_TEST_FLOOR,
        "real_acf_door_interactions": STATE.interactions,
        "start_point_repair_samples": start_repair_samples,
        "floor_ready_seconds": {
            "minimum": min(ready_durations),
            "median": ready_durations[len(ready_durations) // 2],
            "p95_nearest_rank": ready_p95,
            "maximum": max(ready_durations),
        },
        "working_set_bytes": {
            "minimum_post_warmup": min(post_warmup_memory),
            "maximum_post_warmup": max(post_warmup_memory),
            "final": memory_values[-1],
            "range_post_warmup": memory_growth,
            "allowed_range": memory_growth_limit,
            "decrease_count_post_warmup": memory_decreases,
        },
        "world_actor_counts": [
            {
                "label": sample["label"],
                "count": sample["runtime"]["world_actor_count"],
                "population_count": sample["runtime"]["population_actor_count"],
                "manifest_spawned_count": sample["floor_record"]["manifest"][
                    "spawned_actor_count"
                ],
                "remaining_anchor_count": sample["runtime"][
                    "anchor_count_after_ready"
                ],
            }
            for sample in samples
        ],
    }
    STATE.checks["door_labels_increment_floor"] = all(
        sample["runtime"]["door_label"]
        == "Floor {} → {}".format(
            sample["floor_record"]["floor_number"],
            sample["floor_record"]["floor_number"] + 1,
        )
        for sample in samples
    )
    STATE.checks["door_disabled_before_readiness"] = all(
        STATE.door_disabled_observed.get(label, False)
        for label in required_disabled_labels
    )
    STATE.checks["floor_record_snapshot_consistent"] = all(
        snapshot_matches_floor_record(
            sample["snapshot"], sample["floor_record"]
        )
        for sample in samples
    )
    STATE.checks["positive_pcg_seed_range"] = all(
        1 <= sample["floor_record"]["pcg_seed"] <= 2147483647
        for sample in samples
    )
    seeds_by_context = {}
    for sample in samples:
        context_key = (
            sample["floor_record"]["run_seed"],
            sample["floor_record"]["generation_serial"],
        )
        seeds_by_context.setdefault(
            context_key, sample["floor_record"]["pcg_seed"]
        )
    STATE.checks["generated_context_pcg_seeds_unique"] = (
        len(set(seeds_by_context.values())) == len(seeds_by_context)
        and len(seeds_by_context) == MAX_TEST_FLOOR + 4
    )
    policy_hashes = {
        sample["floor_record"]["policy_hash"] for sample in samples
    }
    STATE.checks["policy_hash_stable"] = len(policy_hashes) == 1 and all(
        bool(value) for value in policy_hashes
    )
    STATE.checks["run_seed_stable"] = all(
        sample["floor_record"]["run_seed"] == RUN_SEED for sample in samples
    )
    STATE.checks["queued_director_intent_absent"] = all(
        not sample["snapshot"]["has_queued_director_intent"]
        for sample in samples
    )
    samples_by_label = {sample["label"]: sample for sample in samples}
    STATE.checks["same_seed_restart_world_actor_count_exact"] = (
        samples_by_label["same_seed_restart"]["runtime"]["world_actor_count"]
        == samples_by_label["fixed_new_run"]["runtime"]["world_actor_count"]
    )
    STATE.soak_metrics["same_seed_restart_world_actor_delta"] = (
        samples_by_label["same_seed_restart"]["runtime"]["world_actor_count"]
        - samples_by_label["fixed_new_run"]["runtime"]["world_actor_count"]
    )
    baseline_fingerprint = samples_by_label["fixed_new_run"]["runtime"][
        "layout_fingerprint"
    ]["sha256"]
    STATE.checks["replay_layout_fingerprint_exact"] = (
        samples_by_label["replay"]["runtime"]["layout_fingerprint"]["sha256"]
        == baseline_fingerprint
    )
    STATE.checks["reroll_layout_fingerprint_changed"] = (
        samples_by_label["reroll"]["runtime"]["layout_fingerprint"]["sha256"]
        != baseline_fingerprint
    )
    STATE.checks["same_seed_restart_layout_fingerprint_exact"] = (
        samples_by_label["same_seed_restart"]["runtime"]["layout_fingerprint"][
            "sha256"
        ]
        == baseline_fingerprint
    )
    generated_labels = ["fixed_new_run", "reroll"] + [
        "floor_{}".format(floor) for floor in range(2, MAX_TEST_FLOOR + 1)
    ] + ["debug_25", "debug_50", "debug_100"]
    generated_fingerprints = {
        samples_by_label[label]["runtime"]["layout_fingerprint"]["sha256"]
        for label in generated_labels
    }
    STATE.checks["generated_layout_fingerprints_distinct"] = (
        len(generated_fingerprints) == len(generated_labels)
    )
    refresh_asset_delta()
    failed = sorted(name for name, value in STATE.checks.items() if not value)
    if failed:
        raise RuntimeError("Dungeon Director V3 checks remained false: " + ", ".join(failed))
    set_phase("complete")
    finish(True)


def tick(_delta_seconds):
    try:
        if STATE.finished:
            return
        if time.monotonic() - STATE.started_at > GLOBAL_TIMEOUT_SECONDS:
            fail("Global timeout in phase {}".format(STATE.phase))
            return

        if STATE.phase == "load_map":
            if RUN_SEED <= 0:
                fail("CODEX_CALYSTO_DIRECTOR_V3_RUN_SEED must be a positive Int64")
                return
            if MAX_TEST_FLOOR < 10 or MAX_TEST_FLOOR > 100:
                fail(
                    "CODEX_CALYSTO_DIRECTOR_V3_MAX_TEST_FLOOR must be between 10 and 100"
                )
                return
            STATE.v3_policy = validate_v3_policy_read_only()
            STATE.checks["v3_policy_read_only"] = True
            STATE.checks["v3_policy_class_valid"] = (
                STATE.v3_policy["class"] == V3_POLICY_CLASS_PATH
            )
            STATE.checks["v3_policy_versions_valid"] = (
                STATE.v3_policy["schema_version"] == 3
                and STATE.v3_policy["generator_version"] == 3
            )
            STATE.door_class = unreal.load_class(None, DOOR_CLASS_PATH)
            STATE.start_point_class = unreal.load_class(None, START_POINT_CLASS_PATH)
            STATE.interaction_component_class = unreal.load_class(
                None, INTERACTION_COMPONENT_CLASS_PATH
            )
            STATE.subsystem_class = unreal.load_class(None, SUBSYSTEM_CLASS_PATH)
            STATE.anchor_class = unreal.load_class(None, ANCHOR_CLASS_PATH)
            STATE.nav_bounds_class = unreal.load_class(None, NAV_BOUNDS_CLASS_PATH)
            STATE.recast_navmesh_class = unreal.load_class(
                None, RECAST_NAVMESH_CLASS_PATH
            )
            if not all(
                (
                    STATE.door_class,
                    STATE.start_point_class,
                    STATE.interaction_component_class,
                    STATE.subsystem_class,
                    STATE.anchor_class,
                    STATE.nav_bounds_class,
                    STATE.recast_navmesh_class,
                )
            ):
                fail("A required Calysto/ACF class failed to load")
                return
            set_phase("loading_map_in_progress")
            if LEVEL_EDITOR.load_level(CONTROL_MAP) is False:
                fail("HUB control map failed to load")
                return
            set_phase("wait_editor_map")
            return

        if STATE.phase == "wait_editor_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if canonical_world_path(editor_world).lower() != CONTROL_WORLD_PATH.lower():
                if phase_age() > PHASE_TIMEOUT_SECONDS:
                    fail("HUB editor world did not become ready")
                return
            if phase_age() < 5.0:
                return
            STATE.dungeon_class = unreal.load_class(None, DUNGEON_CLASS_PATH)
            if not STATE.dungeon_class:
                if phase_age() > PHASE_TIMEOUT_SECONDS:
                    fail("BP_MassiveDungeon runtime class did not load")
                return
            STATE.checks["map_loaded"] = True
            set_phase("begin_pie_in_progress")
            LEVEL_EDITOR.editor_request_begin_play()
            set_phase("wait_control_world")
            return

        if STATE.phase == "wait_control_world":
            world = game_world()
            if (
                LEVEL_EDITOR.is_in_play_in_editor()
                and world
                and canonical_world_path(world).lower() == CONTROL_WORLD_PATH.lower()
            ):
                subsystem = find_game_instance_subsystem(world, STATE.subsystem_class)
                if subsystem and world_time(world) >= 1.0:
                    STATE.subsystem = subsystem
                    STATE.current_floor_record = None
                    STATE.checks["pie_started"] = True
                    if REQUIRE_START_POINT_REPAIR:
                        arm_start_point_suppression(world, "fixed_new_run")
                    require_operation(
                        "request_start_new_run_with_seed",
                        "new_run_with_seed",
                        RUN_SEED,
                    )
                    begin_wait("fixed_new_run", 1, 1, "NewRun")
                    return
            if phase_age() > PHASE_TIMEOUT_SECONDS:
                fail("HUB PIE world did not expose Dungeon Director V3")
            return

        if STATE.phase.startswith("wait_runtime_"):
            world = game_world()
            if (
                LEVEL_EDITOR.is_in_play_in_editor()
                and world
                and canonical_world_path(world).lower() == DUNGEON_WORLD_PATH.lower()
            ):
                observe_pre_ready_door(world)
                if world_time(world) >= FLOOR_READY_TIME_SECONDS:
                    sample = resolve_expected_runtime(world)
                    if sample:
                        handle_runtime_sample(sample)
                        return
            if phase_age() > PHASE_TIMEOUT_SECONDS:
                fail(
                    "{} did not reach Floor {} / GenerationSerial {} runtime readiness".format(
                        STATE.wait_label,
                        STATE.expected_floor,
                        STATE.expected_serial,
                    )
                )
            return

        if STATE.phase == "request_replay":
            if REQUIRE_START_POINT_REPAIR:
                arm_start_point_suppression(game_world(), "replay")
            require_operation("request_replay_current_floor", "replay_current_floor")
            STATE.checks["replay_accepted"] = True
            begin_wait("replay", 1, 1, "Replay")
            return

        if STATE.phase == "request_reroll":
            require_operation("request_reroll_current_floor", "reroll_current_floor")
            STATE.checks["reroll_accepted"] = True
            begin_wait("reroll", 1, 2, "Reroll")
            return

        if STATE.phase.startswith("wait_selection_floor_"):
            sample_selection()
            if STATE.selection_samples >= SELECTION_SAMPLES_REQUIRED:
                handle_selected_door()
                return
            if phase_age() > PHASE_TIMEOUT_SECONDS:
                fail(
                    "ACF did not select the generated door on Floor {}".format(
                        STATE.current_floor_record["floor_number"]
                    )
                )
            return

        if STATE.phase == "request_debug_jump_25":
            require_operation("request_travel_to_floor", "debug_jump", 25)
            begin_wait("debug_25", 25, MAX_TEST_FLOOR + 2, "DebugJump")
            return

        if STATE.phase == "request_debug_jump_50":
            require_operation("request_travel_to_floor", "debug_jump", 50)
            begin_wait("debug_50", 50, MAX_TEST_FLOOR + 3, "DebugJump")
            return

        if STATE.phase == "request_debug_jump_100":
            require_operation("request_travel_to_floor", "debug_jump", 100)
            begin_wait("debug_100", 100, MAX_TEST_FLOOR + 4, "DebugJump")
            return

        if STATE.phase == "request_same_seed_restart":
            if REQUIRE_START_POINT_REPAIR:
                arm_start_point_suppression(game_world(), "same_seed_restart")
            require_operation(
                "request_start_new_run_with_seed",
                "same_seed_new_run_restart",
                RUN_SEED,
            )
            begin_wait("same_seed_restart", 1, 1, "NewRun")
            return
    except Exception as exc:
        finish(False, "{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_dungeon_master_pie58", None)
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
builtins._codex_calysto_dungeon_master_pie58 = STATE
unreal.log("[CalystoDungeonMasterPIE58] Dungeon Director V3 validator registered")
