"""Strict UE 5.8 PIE traversal gate for Dungeon Director V4.

The gate keeps one GameInstance alive while it proves fixed-seed New Run,
exact Replay, distinct Reroll, real ACF-door traversal through Floor 10, and
Development jumps at the required depth boundaries.  It never saves Content
or edits the authored V4 policy/Calysto packages.
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
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V4_TRAVERSAL_OUTPUT"]).resolve()
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V4_TRAVERSAL_SEED", "202608210404"))

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = CONTENT_DIR / "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
PCG_SUBSYSTEM_CLASS = "/Script/EFProceduralPCGRuntime.EFProceduralPCGSubsystem"
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
JUMP_FLOORS = (25, 50, 100, 101, 125, 500, 1000)
EXPECTED_SAMPLE_COUNT = 3 + 9 + len(JUMP_FLOORS)
EXPECTED_DOOR_INTERACTIONS = 9
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
    """Retain a native identity without holding an old PIE UWorld alive."""
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
        "replay": ("replay",),
        "reroll": ("reroll",),
        "advance": ("advance",),
        "debugjump": ("debugjump",),
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
    """Accept localized number grouping while preserving the exact floor edge."""
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
        self.initial_sample = None
        self.replay_sample = None
        self.reroll_sample = None
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
    unreal.log("CALYSTO_V4_TRAVERSAL phase={}".format(value))


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
    asset_mutations = sorted(set(asset_saves) | {name for name, dirty in final_dirty.items() if dirty})
    protected_mismatches = sorted(
        name for name, before in STATE.initial_protected.items() if before != final_protected.get(name)
    )
    policy_sha_after = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
    final_checks = {
        "sample_count_19": len(STATE.samples) == EXPECTED_SAMPLE_COUNT,
        "real_acf_door_interactions_9": len(STATE.door_interactions) == EXPECTED_DOOR_INTERACTIONS,
        "door_floors_1_through_9": [row["from_floor"] for row in STATE.door_interactions] == list(range(1, 10)),
        "door_disabled_before_every_ready": len(STATE.samples) == EXPECTED_SAMPLE_COUNT
        and all(STATE.door_disabled_observed.get(sample["label"], False) for sample in STATE.samples),
        "policy_hash_constant": bool(STATE.samples)
        and all(sample["hashes"]["policy"] == STATE.policy.get("policy_hash") for sample in STATE.samples),
        "run_epoch_constant": bool(STATE.samples)
        and STATE.initial_run_epoch > 0
        and all(sample["run_epoch"] == STATE.initial_run_epoch for sample in STATE.samples),
        "content_not_saved": not asset_saves,
        "packages_not_dirty": not asset_mutations,
        "protected_hashes_stable": not protected_mismatches,
        "policy_bytes_stable": STATE.policy_sha_before is not None and STATE.policy_sha_before == policy_sha_after,
    }
    if len(STATE.samples) == EXPECTED_SAMPLE_COUNT:
        non_replay_seeds = [sample["pcg_seed"] for sample in STATE.samples if sample["label"] != "replay_floor_1"]
        final_checks["pcg_seeds_unique_except_replay"] = len(non_replay_seeds) == len(set(non_replay_seeds))
        final_checks["normal_history_not_synthetic"] = all(
            not sample["development_synthetic_history"] for sample in STATE.samples if not sample["is_development_jump"]
        )
        final_checks["jump_history_synthetic"] = all(
            sample["development_synthetic_history"] for sample in STATE.samples if sample["is_development_jump"]
        )
    else:
        final_checks["pcg_seeds_unique_except_replay"] = False
        final_checks["normal_history_not_synthetic"] = False
        final_checks["jump_history_synthetic"] = False

    failed_final = sorted(name for name, passed in final_checks.items() if not passed)
    if success and failed_final:
        success = False
        error = "Final traversal checks failed: " + ", ".join(failed_final)
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
        "expected_generation_count": EXPECTED_SAMPLE_COUNT,
        "expected_door_interactions": EXPECTED_DOOR_INTERACTIONS,
        "samples": STATE.samples,
        "operations": STATE.operations,
        "door_interactions": STATE.door_interactions,
        "door_disabled_observed": STATE.door_disabled_observed,
        "door_disabled_actor": STATE.door_disabled_actor,
        "final_checks": final_checks,
        "asset_saves": asset_saves,
        "asset_mutations": asset_mutations,
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
    unreal.log("CALYSTO_V4_TRAVERSAL_RESULT " + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v4_traversal_pie58 = None
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


def request_director_operation(world, director, label, floor, serial, kind, method_name, *args):
    operation = {
        "operation": method_name,
        "source": "director_api",
        "from_floor": STATE.current_sample["floor"] if STATE.current_sample else 0,
        "expected_floor": floor,
        "expected_serial": serial,
        "arguments": list(args),
        "accepted": None,
    }
    STATE.operations.append(operation)
    begin_wait(label, floor, serial, kind, world)
    accepted = bool(reflected(director, method_name)(*args))
    operation["accepted"] = accepted
    if not accepted:
        raise RuntimeError("Director rejected operation {} for {}".format(method_name, label))


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
    pcg_subsystem = find_subsystem(world, STATE.classes.get("pcg_subsystem"))
    if pcg_subsystem and bool(
        reflected(pcg_subsystem, "was_current_floor_door_disabled_before_readiness")()
    ):
        STATE.door_disabled_observed[STATE.wait_label] = True
        if len(doors) == 1:
            STATE.door_disabled_actor[STATE.wait_label] = object_path(doors[0])
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
    is_jump = STATE.expected_kind.lower() == "debugjump"
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
    category_checks = {}
    structural_category_results = {}
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
    live_category_counts = {key: len(actors_with_tag(world, tag)) for key, tag in CATEGORY_TAGS.items()}
    doors = actors_of_class(world, STATE.classes["door"])
    dungeons = actors_of_class(world, STATE.classes["dungeon"])
    starts = actors_of_class(world, STATE.classes["start"])
    anchors = actors_of_class(world, STATE.classes["anchor"])
    nav_bounds = actors_of_class(world, STATE.classes["nav_bounds"])
    recast = actors_of_class(world, STATE.classes["recast"])
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
    director_level = int(prop(intent, "DirectorLevel"))
    winter_level = int(prop(intent, "LogicalWinterLevel"))
    level_contract = director_level == min(floor, 100) and winter_level == (floor if floor > 100 else 0)
    enemy_level_contract = True
    for directive in directives:
        if category_key(prop(directive, "Category")) in ("enemy", "npc"):
            logical = int(prop(directive, "LogicalLevel"))
            physical = int(prop(directive, "PhysicalACFLevel"))
            enemy_level_contract = enemy_level_contract and logical >= 1 and physical == min(logical, 100)

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
        "synthetic_history_contract": bool(prop(ecology, "bDevelopmentSyntheticHistory")) == is_jump,
        "style_valid": any(token in normalized(style) for token in ("standard", "compact", "branching")),
        "theme_valid": any(token in normalized(theme) for token in ("default", "forge", "shrine")),
        "certified_size": 26 <= int(prop(size, "X")) <= 30 and int(prop(size, "X")) == int(prop(size, "Y")) and int(prop(size, "Z")) == 1,
        "shape_ranges": 0.20 <= float(prop(intent, "CandidateAnchorDensity")) <= 0.50 and 0.30 <= float(prop(intent, "SidePathChance")) <= 0.70,
        "level_contract": level_contract and enemy_level_contract,
        "category_set": set(category_results) == set(CATEGORY_CAPS)
        and set(structural_category_results) == {"decoration", "lighting"}
        and all(category_checks.values())
        and all(structural_category_checks.values()),
        "hard_caps": all(0 <= counts[key] <= CATEGORY_CAPS[key] for key in CATEGORY_CAPS) and total <= INITIAL_ACTOR_CAP,
        "manifest_actor_count": int(prop(manifest, "SpawnedActorCount")) == total == len(instances) == len(directives),
        "stable_ids": len(instance_ids) == len(set(instance_ids)) and len(directive_ids) == len(set(directive_ids)) and all(value and value.lower() != "none" for value in instance_ids + directive_ids),
        "live_population": len(population) == total and live_category_counts == counts,
        "candidate_anchors_available": int(prop(manifest, "CandidateAnchorCount")) > 0,
        "anchors_destroyed": len(anchors) == 0,
        "one_dungeon_door_start": len(dungeons) == 1 and len(doors) == 1 and len(starts) == 1,
        "door_enabled_and_labeled": enabled and door_label_matches(label, floor),
        "real_acf_interaction_surface": player is not None and len(interactions) == 1,
        "navigation_ready": len(nav_bounds) > 0 and len(recast) > 0,
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
        "door_disabled_before_ready": STATE.door_disabled_observed.get(STATE.wait_label, False),
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
        "director_level": director_level,
        "logical_winter_level": winter_level,
        "development_synthetic_history": bool(prop(ecology, "bDevelopmentSyntheticHistory")),
        "is_development_jump": is_jump,
        "door": {
            "actor": object_path(doors[0]) if len(doors) == 1 else "",
            "enabled": enabled,
            "label": label,
            "disabled_before_ready_observed": STATE.door_disabled_observed.get(STATE.wait_label, False),
        },
        "runtime": {
            "population_actor_count": len(population),
            "anchors_after_ready": len(anchors),
            "runtime_pcg_components": len(runtime_pcg),
            "nav_bounds": len(nav_bounds),
            "recast_navmeshes": len(recast),
            "ready_elapsed_seconds": round(time.monotonic() - STATE.wait_started, 3),
        },
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
    if floor < 1 or floor >= 10:
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
    reflected(interaction, "interact")("CalystoV4TraversalPIE58")


def handle_sample(world, director, sample):
    STATE.samples.append(sample)
    STATE.current_sample = sample
    label = sample["label"]
    if label == "new_run_floor_1":
        STATE.initial_sample = sample
        STATE.initial_run_epoch = sample["run_epoch"]
        set_phase("request_replay")
        return
    if label == "replay_floor_1":
        STATE.replay_sample = sample
        exact_fields = (
            "hashes",
            "pcg_seed",
            "style",
            "theme",
            "dungeon_size",
            "counts",
            "category_results",
            "structural_category_results",
        )
        if any(sample[field] != STATE.initial_sample[field] for field in exact_fields):
            raise RuntimeError("Replay did not reproduce the exact Floor 1 intent/manifest/runtime contract")
        set_phase("request_reroll")
        return
    if label == "reroll_floor_1":
        STATE.reroll_sample = sample
        if (
            sample["hashes"]["intent"] == STATE.initial_sample["hashes"]["intent"]
            or sample["hashes"]["manifest"] == STATE.initial_sample["hashes"]["manifest"]
            or sample["pcg_seed"] == STATE.initial_sample["pcg_seed"]
        ):
            raise RuntimeError("Reroll did not change intent, manifest, and PCG seed")
        begin_door_selection(world, sample)
        return
    if label.startswith("advance_floor_"):
        floor = sample["floor"]
        if floor < 2 or floor > 10:
            raise RuntimeError("Unexpected sequential Advance destination")
        if floor < 10:
            begin_door_selection(world, sample)
        else:
            set_phase("request_jump_25")
        return
    if label.startswith("jump_floor_"):
        floor = sample["floor"]
        index = JUMP_FLOORS.index(floor)
        if index + 1 < len(JUMP_FLOORS):
            set_phase("request_jump_{}".format(JUMP_FLOORS[index + 1]))
        else:
            finish(True)
        return
    raise RuntimeError("Unknown traversal sample label: " + label)


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
                raise RuntimeError("Traversal gate requires a positive seed and the authored V4 policy")
            if any(not row["exists"] for row in STATE.initial_protected.values()):
                raise RuntimeError("A protected traversal invariant file is missing")
            bp_before = STATE.initial_protected["Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset"]["sha256"]
            if bp_before != BP_MASSIVE_DUNGEON_BASELINE:
                raise RuntimeError("BP_MassiveDungeon no longer matches the protected baseline")
            STATE.policy = native_policy_document(unreal.load_asset(POLICY_OBJECT))
            STATE.classes = {
                "director": unreal.load_class(None, DIRECTOR_CLASS),
                "pcg_subsystem": unreal.load_class(None, PCG_SUBSYSTEM_CLASS),
                "door": unreal.load_class(None, DOOR_CLASS),
                "dungeon": unreal.load_class(None, DUNGEON_CLASS),
                "start": unreal.load_class(None, START_POINT_CLASS),
                "anchor": unreal.load_class(None, ANCHOR_CLASS),
                "interaction": unreal.load_class(None, INTERACTION_COMPONENT_CLASS),
                "nav_bounds": unreal.load_class(None, NAV_BOUNDS_CLASS),
                "recast": unreal.load_class(None, RECAST_NAVMESH_CLASS),
            }
            if not all(STATE.classes.values()):
                raise RuntimeError("A required V4/Calysto/ACF traversal class failed to load")
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
            request_director_operation(
                world,
                director,
                "new_run_floor_1",
                1,
                1,
                "NewRun",
                "request_start_new_run_with_seed",
                RUN_SEED,
            )
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
                        handle_sample(world, director, sample)
                        return
            elif STATE.saw_dungeon_world and world and canonical_world(world).lower() == CONTROL_WORLD.lower():
                director = director_for_world(world)
                if director:
                    snapshot = reflected(director, "get_snapshot")()
                    raise RuntimeError("{}: {}".format(prop(snapshot, "FailureCode"), prop(snapshot, "FailureMessage")))
            return

        if STATE.phase == "request_replay":
            director = director_for_world(world)
            request_director_operation(world, director, "replay_floor_1", 1, 1, "Replay", "request_replay_current_floor")
            return

        if STATE.phase == "request_reroll":
            director = director_for_world(world)
            request_director_operation(world, director, "reroll_floor_1", 1, 2, "Reroll", "request_reroll_current_floor")
            return

        if STATE.phase.startswith("wait_door_selection_floor_"):
            if not world or canonical_world(world).lower() != DUNGEON_WORLD.lower():
                return
            sample_door_selection(world)
            return

        if STATE.phase.startswith("request_jump_"):
            target = int(STATE.phase.rsplit("_", 1)[1])
            if target not in JUMP_FLOORS:
                raise RuntimeError("Unknown Development jump target")
            director = director_for_world(world)
            expected_serial = 12 + JUMP_FLOORS.index(target)
            request_director_operation(
                world,
                director,
                "jump_floor_{}".format(target),
                target,
                expected_serial,
                "DebugJump",
                "request_travel_to_floor",
                target,
            )
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v4_traversal_pie58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v4_traversal_pie58 = STATE
unreal.log("CALYSTO_V4_TRAVERSAL validator_registered=true seed={}".format(RUN_SEED))
