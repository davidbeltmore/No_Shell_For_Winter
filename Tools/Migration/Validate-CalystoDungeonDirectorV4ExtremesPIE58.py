"""Validate exact Dungeon Director V4 population fixtures in real UE 5.8 PIE.

Each editor process owns one Development-only transient policy clone.  The
authored V4 Primary Data Asset and every protected Calysto package remain
read-only while the normal map travel, PCG, navigation, population, companion
roster, and door readiness pipeline runs unchanged.
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
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V4_EXTREME_OUTPUT"]).resolve()
SCENARIO = os.environ["CODEX_CALYSTO_V4_EXTREME_SCENARIO"]
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V4_EXTREME_SEED", "202608210404"))

EXPECTED_COUNTS = {
    "Zero": {
        "enemy": 0,
        "npc": 0,
        "food": 0,
        "chest": 0,
        "loose_loot": 0,
        "clothing": 0,
        "special_event": 0,
    },
    "EnemyCap25": {
        "enemy": 25,
        "npc": 0,
        "food": 0,
        "chest": 0,
        "loose_loot": 0,
        "clothing": 0,
        "special_event": 0,
    },
    "ResourceMin": {
        "enemy": 0,
        "npc": 0,
        "food": 0,
        "chest": 0,
        "loose_loot": 0,
        "clothing": 0,
        "special_event": 0,
    },
    "ResourceMax": {
        "enemy": 0,
        "npc": 0,
        "food": 30,
        "chest": 10,
        "loose_loot": 0,
        "clothing": 0,
        "special_event": 0,
    },
    "NPCTotal4": {
        "enemy": 0,
        "npc": 4,
        "food": 0,
        "chest": 0,
        "loose_loot": 0,
        "clothing": 0,
        "special_event": 0,
    },
    "SpecialEvents6": {
        "enemy": 0,
        "npc": 0,
        "food": 0,
        "chest": 0,
        "loose_loot": 0,
        "clothing": 0,
        "special_event": 6,
    },
}
EXPECTED_NPC_VARIANTS = {
    "NPCGeneralistFemale": (
        "NPC.Companion.Generalist.Female",
        "/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C",
    ),
    "NPCGeneralistMale": (
        "NPC.Companion.Generalist.Male",
        "/Game/_Game/Characters/Male/ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C",
    ),
    "NPCMeleeFemale": (
        "NPC.Companion.Melee.Female",
        "/Game/_Game/Characters/Female/ACFMeleeCompanionBPFemale.ACFMeleeCompanionBPFemale_C",
    ),
    "NPCMeleeMale": (
        "NPC.Companion.Melee.Male",
        "/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C",
    ),
    "NPCRangedFemale": (
        "NPC.Companion.Ranged.Female",
        "/Game/_Game/Characters/Female/ACFRangedCompanionBPFemale.ACFRangedCompanionBPFemale_C",
    ),
    "NPCRangedMale": (
        "NPC.Companion.Ranged.Male",
        "/Game/_Game/Characters/Male/ACFRangedCompanionBPMale.ACFRangedCompanionBPMale_C",
    ),
}
EXPECTED_COMPANION_CLASSES_BY_CATALOG = {
    catalog: actor_class
    for catalog, actor_class in EXPECTED_NPC_VARIANTS.values()
}
for npc_scenario in EXPECTED_NPC_VARIANTS:
    EXPECTED_COUNTS[npc_scenario] = {
        "enemy": 0,
        "npc": 1,
        "food": 0,
        "chest": 0,
        "loose_loot": 0,
        "clothing": 0,
        "special_event": 0,
    }
if SCENARIO not in EXPECTED_COUNTS:
    raise RuntimeError("Unknown V4 extreme scenario: " + SCENARIO)
if RUN_SEED <= 0:
    raise RuntimeError("Dungeon Director V4 extreme PIE requires a positive seed")

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = (
    CONTENT_DIR
    / "_Game"
    / "Data"
    / "CalystoDungeon"
    / "V4"
    / "DA_CalystoDungeonDirectorPolicy.uasset"
)
SUBSYSTEM_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS = "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
ANCHOR_CLASS = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
RECRUITMENT_CLASS = (
    "/Script/EFProjectSystemsGameplay.ProjectRecruitableCompanionComponent"
)
SPECIAL_EVENT_PROBE_CLASS = (
    "/Script/EFProceduralPCGRuntime.EFCalystoSpecialEventProbe"
)
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

MONITORED_OBJECTS = (
    POLICY_OBJECT,
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
)
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
)

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
GLOBAL_TIMEOUT = 300.0
PHASE_TIMEOUT = 210.0


def object_path(value):
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return ""


def canonical_world(world):
    return re.sub(r"uedpie_\d+_", "", object_path(world), flags=re.IGNORECASE)


def game_world():
    return EDITOR_LEVEL_LIBRARY.get_game_world()


def prop(owner, name):
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()
    for candidate in (name, snake, name[0].lower() + name[1:]):
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
    raise RuntimeError("Missing reflected property {}".format(name))


def reflected(owner, name):
    method = getattr(owner, name, None)
    if not callable(method):
        raise RuntimeError(
            "Missing reflected method {} on {}".format(name, object_path(owner))
        )
    return method


def is_invalid_guid(value):
    try:
        return all(int(prop(value, field)) == 0 for field in ("A", "B", "C", "D"))
    except Exception:
        groups = re.findall(r"(?i)(?<![0-9a-f])[0-9a-f]{8}(?![0-9a-f])", str(value))
        if len(groups) >= 4:
            return all(int(group, 16) == 0 for group in groups[-4:])
        compact = re.search(r"(?i)(?<![0-9a-f])[0-9a-f]{32}(?![0-9a-f])", str(value))
        return bool(compact) and int(compact.group(0), 16) == 0


def actors_of_class(world, actor_class):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def actors_with_tag(world, tag):
    return list(unreal.GameplayStatics.get_all_actors_with_tag(world, tag))


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


def dirty_states():
    result = {}
    for object_name in MONITORED_OBJECTS:
        asset = unreal.load_asset(object_name)
        try:
            result[object_name] = (
                bool(asset.get_outermost().is_dirty()) if asset else None
            )
        except Exception:
            result[object_name] = None
    return result


def native_policy_document(policy):
    policy_class = unreal.load_class(None, POLICY_CLASS)
    if not policy or not policy_class or policy.get_class() != policy_class:
        raise RuntimeError("The authored V4 policy or its exact native class is missing")
    validation = reflected(policy, "validate_policy")()
    if isinstance(validation, tuple):
        valid = bool(validation[0]) if validation else False
        error = str(validation[1]) if len(validation) > 1 else ""
    elif isinstance(validation, bool):
        valid = validation
        error = ""
    else:
        raise RuntimeError("ValidatePolicy returned an unsupported Python value")
    if not valid:
        raise RuntimeError("Native V4 policy validation failed: " + error)
    document = {
        "class": object_path(policy.get_class()),
        "schema_version": int(prop(policy, "SchemaVersion")),
        "generator_version": int(prop(policy, "GeneratorVersion")),
        "policy_id": str(prop(policy, "PolicyId")),
        "policy_hash": str(reflected(policy, "get_policy_hash")()).upper(),
        "native_validation": "PASS",
    }
    if (
        document["schema_version"] != 4
        or document["generator_version"] != 4
        or document["policy_id"] != "CalystoDungeonDirectorV4"
        or not is_sha256(document["policy_hash"])
    ):
        raise RuntimeError("The authored V4 policy identity/hash is invalid")
    return document


class State:
    def __init__(self):
        self.phase = "load_map"
        self.started = time.monotonic()
        self.phase_started = self.started
        self.callback = None
        self.finished = False
        self.subsystem_class = None
        self.door_class = None
        self.dungeon_class = None
        self.anchor_class = None
        self.recruitment_class = None
        self.saw_dungeon_world = False
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_states()
        self.initial_protected = protected_hashes()
        self.policy_sha_before = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
        self.policy = {}
        self.result = {}


STATE = State()


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("[CalystoV4ExtremesPIE58] phase={} scenario={}".format(value, SCENARIO))


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
    asset_mutations = sorted(
        set(asset_saves)
        | {
            name
            for name, dirty in final_dirty.items()
            if dirty and not STATE.initial_dirty.get(name, False)
        }
    )
    protected_mismatches = sorted(
        name
        for name, before in STATE.initial_protected.items()
        if before != final_protected.get(name)
    )
    policy_sha_after = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
    document = {
        "schema_version": 4,
        "generator_version": 4,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "success": bool(success),
        "status": "PASS" if success else "FAIL",
        "scenario": SCENARIO,
        "run_seed": RUN_SEED,
        "forced_dungeon_edge": 30,
        "phase": STATE.phase,
        "error": error,
        "policy": STATE.policy,
        "realized": STATE.result,
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
    OUTPUT_FILE.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log("[CalystoV4ExtremesPIE58] result=" + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v4_extremes_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message):
    finish(False, message)


def validate_ready(world, subsystem):
    snapshot = reflected(subsystem, "get_snapshot")()
    if "ready" not in str(prop(snapshot, "State")).lower():
        return False

    intent = reflected(subsystem, "get_resolved_floor_intent")()
    manifest = reflected(subsystem, "get_realized_floor_manifest")()
    expected = EXPECTED_COUNTS[SCENARIO]
    counts = {
        "enemy": int(prop(manifest, "EnemyCount")),
        "npc": int(prop(manifest, "NPCCount")),
        "food": int(prop(manifest, "FoodCount")),
        "chest": int(prop(manifest, "ChestCount")),
        "loose_loot": int(prop(manifest, "LooseLootCount")),
        "clothing": int(prop(manifest, "ClothingCount")),
        "special_event": int(prop(manifest, "SpecialEventCount")),
    }
    tag_counts = {
        name: len(actors_with_tag(world, tag)) for name, tag in CATEGORY_TAGS.items()
    }
    population_actors = actors_with_tag(world, POPULATION_TAG)
    doors = actors_of_class(world, STATE.door_class)
    dungeons = actors_of_class(world, STATE.dungeon_class)
    anchors = actors_of_class(world, STATE.anchor_class)
    instances = list(prop(manifest, "Instances"))
    directives = list(prop(intent, "SpawnDirectives"))
    stable_ids = [str(prop(instance, "StableInstanceId")) for instance in instances]
    size = prop(intent, "DungeonSize")
    policy_hash = str(prop(intent, "PolicyHash"))
    intent_hash = str(prop(intent, "IntentHash"))
    manifest_hash = str(prop(manifest, "ManifestHash"))
    companion_hash = str(prop(manifest, "CompanionSnapshotHash"))
    expected_total = sum(expected.values())
    door_enabled = False
    if len(doors) == 1:
        for field in ("bIsEnabled", "IsEnabled"):
            try:
                door_enabled = bool(prop(doors[0], field))
                break
            except Exception:
                pass

    scenario_name = str(prop(snapshot, "DevelopmentPopulationScenario"))
    intent_scenario_name = str(prop(intent, "DevelopmentPopulationScenario"))
    hash_fields = {
        "policy": policy_hash,
        "ecology": str(prop(intent, "EcologyHash")),
        "intent": intent_hash,
        "anchor_topology": str(prop(manifest, "AnchorTopologyHash")),
        "population": str(prop(manifest, "PopulationHash")),
        "resource": str(prop(manifest, "ResourceHash")),
        "companion": companion_hash,
        "manifest": manifest_hash,
    }
    checks = {
        "policy_valid": bool(prop(snapshot, "bPolicyValid"))
        and not str(prop(snapshot, "PolicyError")),
        "run_ready": bool(prop(snapshot, "bHasActiveRun")),
        "door_ready": bool(prop(snapshot, "bDoorReady")),
        "companion_roster_ready": bool(prop(snapshot, "bCompanionReady")),
        "manifest_valid": bool(prop(manifest, "bIsValid")),
        "intent_valid": bool(prop(intent, "bIsValid")),
        "generator_version_4": int(prop(intent, "GeneratorVersion")) == 4,
        "floor_identity": int(prop(intent, "RunSeed")) == RUN_SEED
        and int(prop(intent, "FloorNumber")) == 1
        and int(prop(intent, "GenerationSerial")) == 1,
        "scenario_policy_is_transient": bool(policy_hash)
        and policy_hash != STATE.policy.get("policy_hash", ""),
        "scenario_identity_preserved": SCENARIO.lower() in scenario_name.lower()
        and SCENARIO.lower() in intent_scenario_name.lower(),
        "forced_edge_preserved": int(prop(snapshot, "DevelopmentForcedDungeonEdge")) == 30
        and int(prop(intent, "DevelopmentForcedDungeonEdge")) == 30,
        "dungeon_size_is_30": int(prop(size, "X")) == 30
        and int(prop(size, "Y")) == 30
        and int(prop(size, "Z")) == 1,
        "all_category_counts_exact": counts == expected,
        "manifest_actor_count_exact": int(prop(manifest, "SpawnedActorCount"))
        == expected_total,
        "manifest_instance_count_exact": len(instances) == expected_total,
        "stable_instance_ids_unique": len(stable_ids) == len(set(stable_ids))
        and all(value and value.lower() != "none" for value in stable_ids),
        "live_population_actor_count_exact": len(population_actors) == expected_total,
        "live_category_tags_exact": tag_counts == counts,
        "candidate_anchors_were_available": int(prop(manifest, "CandidateAnchorCount")) > 0,
        "anchors_destroyed": len(anchors) == 0,
        "exactly_one_dungeon": len(dungeons) == 1,
        "exactly_one_enabled_door": len(doors) == 1 and door_enabled,
        "intent_manifest_identity": intent_hash == str(prop(manifest, "IntentHash")),
        "companion_hash_matches": companion_hash
        == str(prop(intent, "CompanionSnapshotHash"))
        == str(prop(snapshot, "CompanionSnapshotHash")),
        "all_hashes_present": all(is_sha256(value) for value in hash_fields.values()),
    }
    npc_evidence = {}
    if SCENARIO in EXPECTED_NPC_VARIANTS:
        expected_catalog, expected_class = EXPECTED_NPC_VARIANTS[SCENARIO]
        npc_instances = [
            instance
            for instance in instances
            if "npc" in str(prop(instance, "Category")).lower()
        ]
        npc_directives = [
            directive
            for directive in directives
            if "npc" in str(prop(directive, "Category")).lower()
        ]
        live_npcs = actors_with_tag(world, CATEGORY_TAGS["npc"])
        live_class = object_path(live_npcs[0].get_class()) if len(live_npcs) == 1 else ""
        controller = None
        recruitment_hook = None
        if len(live_npcs) == 1:
            get_controller = getattr(live_npcs[0], "get_controller", None)
            if callable(get_controller):
                controller = get_controller()
            get_component = getattr(live_npcs[0], "get_component_by_class", None)
            if callable(get_component):
                recruitment_hook = get_component(STATE.recruitment_class)
        realized_catalog = (
            str(prop(npc_instances[0], "CatalogId")) if len(npc_instances) == 1 else ""
        )
        directive_catalog = (
            str(prop(npc_directives[0], "CatalogId"))
            if len(npc_directives) == 1
            else ""
        )
        directive_level = (
            int(prop(npc_directives[0], "LogicalLevel"))
            if len(npc_directives) == 1
            else 0
        )
        directive_physical_level = (
            int(prop(npc_directives[0], "PhysicalACFLevel"))
            if len(npc_directives) == 1
            else 0
        )
        checks.update(
            {
                "npc_exact_catalog": realized_catalog == expected_catalog
                and directive_catalog == expected_catalog,
                "npc_exact_actor_class": live_class == expected_class,
                "npc_controller_ready": controller is not None,
                "npc_recruitment_hook_ready": recruitment_hook is not None,
                "npc_logical_level_contract": directive_level >= 1
                and directive_physical_level == min(directive_level, 100),
                "npc_lifecycle_recruitable": len(npc_directives) == 1
                and "recruit" in str(prop(npc_directives[0], "Lifecycle")).lower(),
            }
        )
        npc_evidence = {
            "catalog_id": realized_catalog,
            "actor_class": live_class,
            "controller_class": object_path(controller.get_class()) if controller else "",
            "recruitment_component": object_path(recruitment_hook),
            "logical_level": directive_level,
            "physical_acf_level": directive_physical_level,
        }
    elif SCENARIO == "NPCTotal4":
        npc_instances = [
            instance
            for instance in instances
            if "npc" in str(prop(instance, "Category")).lower()
        ]
        npc_directives = [
            directive
            for directive in directives
            if "npc" in str(prop(directive, "Category")).lower()
        ]
        live_npcs = actors_with_tag(world, CATEGORY_TAGS["npc"])
        live_classes = [object_path(actor.get_class()) for actor in live_npcs]
        controllers = []
        recruitment_hooks = []
        for actor in live_npcs:
            get_controller = getattr(actor, "get_controller", None)
            controllers.append(get_controller() if callable(get_controller) else None)
            get_component = getattr(actor, "get_component_by_class", None)
            recruitment_hooks.append(
                get_component(STATE.recruitment_class)
                if callable(get_component)
                else None
            )
        directive_catalogs = [
            str(prop(directive, "CatalogId")) for directive in npc_directives
        ]
        instance_catalogs = [
            str(prop(instance, "CatalogId")) for instance in npc_instances
        ]
        invalid_roster_ids = []
        for directive in npc_directives:
            invalid_roster_ids.append(
                is_invalid_guid(prop(directive, "StableCompanionId"))
            )
        checks.update(
            {
                "npc_total4_exact_local_directives": len(npc_directives) == 4
                and len(npc_instances) == 4
                and all(invalid_roster_ids),
                "npc_total4_catalogs_authorized": all(
                    catalog in EXPECTED_COMPANION_CLASSES_BY_CATALOG
                    for catalog in directive_catalogs + instance_catalogs
                ),
                "npc_total4_actor_classes_authorized": len(live_classes) == 4
                and all(
                    actor_class in EXPECTED_COMPANION_CLASSES_BY_CATALOG.values()
                    for actor_class in live_classes
                ),
                "npc_total4_controllers_ready": len(controllers) == 4
                and all(controller is not None for controller in controllers),
                "npc_total4_recruitment_hooks_ready": len(recruitment_hooks) == 4
                and all(hook is not None for hook in recruitment_hooks),
                "npc_total4_lifecycle_recruitable": len(npc_directives) == 4
                and all(
                    "recruit" in str(prop(directive, "Lifecycle")).lower()
                    for directive in npc_directives
                ),
                "npc_total4_level_contract": len(npc_directives) == 4
                and all(
                    int(prop(directive, "LogicalLevel")) >= 1
                    and int(prop(directive, "PhysicalACFLevel"))
                    == min(int(prop(directive, "LogicalLevel")), 100)
                    for directive in npc_directives
                ),
            }
        )
        npc_evidence = {
            "catalog_ids": directive_catalogs,
            "actor_classes": live_classes,
            "controller_classes": [
                object_path(controller.get_class()) if controller else ""
                for controller in controllers
            ],
            "recruitment_components": [object_path(hook) for hook in recruitment_hooks],
            "logical_levels": [
                int(prop(directive, "LogicalLevel")) for directive in npc_directives
            ],
            "physical_acf_levels": [
                int(prop(directive, "PhysicalACFLevel"))
                for directive in npc_directives
            ],
        }
    special_event_evidence = {}
    if SCENARIO == "SpecialEvents6":
        event_instances = [
            instance
            for instance in instances
            if "special" in str(prop(instance, "Category")).lower()
        ]
        event_directives = [
            directive
            for directive in directives
            if "special" in str(prop(directive, "Category")).lower()
        ]
        live_events = actors_with_tag(world, CATEGORY_TAGS["special_event"])
        probe_tagged = actors_with_tag(
            world, "EF.Calysto.Automation.SpecialEventProbe"
        )
        live_classes = [object_path(actor.get_class()) for actor in live_events]
        checks.update(
            {
                "special_events6_exact_probe_instances": len(event_instances) == 6
                and len(event_directives) == 6
                and len(live_events) == 6
                and len(probe_tagged) == 6,
                "special_events6_project_owned_class": all(
                    actor_class == SPECIAL_EVENT_PROBE_CLASS
                    for actor_class in live_classes
                ),
                "special_events6_exact_catalog": all(
                    str(prop(value, "CatalogId"))
                    == "Event.Automation.SpecialEventProbe"
                    for value in event_instances + event_directives
                ),
                "special_events6_common_floor_local": all(
                    "common" in str(prop(directive, "Tier")).lower()
                    and "floor" in str(prop(directive, "Lifecycle")).lower()
                    for directive in event_directives
                ),
            }
        )
        special_event_evidence = {
            "actor_classes": live_classes,
            "probe_tagged_count": len(probe_tagged),
            "catalog_ids": [
                str(prop(directive, "CatalogId"))
                for directive in event_directives
            ],
        }
    STATE.result = {
        "counts": counts,
        "tag_counts": tag_counts,
        "population_actor_count": len(population_actors),
        "spawned_actor_count": int(prop(manifest, "SpawnedActorCount")),
        "candidate_anchor_count": int(prop(manifest, "CandidateAnchorCount")),
        "dungeon_count": len(dungeons),
        "door_count": len(doors),
        "door_enabled": door_enabled,
        "anchor_count_after_materialization": len(anchors),
        "dungeon_size": [int(prop(size, "X")), int(prop(size, "Y")), int(prop(size, "Z"))],
        "style": str(prop(intent, "Style")),
        "theme": str(prop(intent, "Theme")),
        "npc_evidence": npc_evidence,
        "special_event_evidence": special_event_evidence,
        "hashes": hash_fields,
        "checks": checks,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("V4 extreme scenario checks failed: " + ", ".join(failed))
    return True


def tick(delta_seconds):
    del delta_seconds
    if STATE.finished:
        return
    try:
        if time.monotonic() - STATE.started > GLOBAL_TIMEOUT:
            fail("Global timeout in phase " + STATE.phase)
            return

        if STATE.phase == "load_map":
            if not POLICY_FILE.is_file():
                fail("The authored V4 policy package is missing")
                return
            policy = unreal.load_asset(POLICY_OBJECT)
            STATE.policy = native_policy_document(policy)
            STATE.subsystem_class = unreal.load_class(None, SUBSYSTEM_CLASS)
            STATE.door_class = unreal.load_class(None, DOOR_CLASS)
            STATE.dungeon_class = unreal.load_class(None, DUNGEON_CLASS)
            STATE.anchor_class = unreal.load_class(None, ANCHOR_CLASS)
            STATE.recruitment_class = unreal.load_class(None, RECRUITMENT_CLASS)
            if not all(
                (
                    STATE.subsystem_class,
                    STATE.door_class,
                    STATE.dungeon_class,
                    STATE.anchor_class,
                    STATE.recruitment_class,
                )
            ):
                fail("A required V4 runtime class failed to load")
                return
            # load_level pumps Slate while it performs the synchronous editor-map
            # transition.  Advance the state first so a nested post-tick cannot
            # recursively request the same map until the Python stack overflows.
            set_phase("wait_editor_map")
            if LEVEL_EDITOR.load_level(CONTROL_MAP) is False:
                fail("HUB control map failed to load")
                return
            return

        if STATE.phase == "wait_editor_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if canonical_world(editor_world).lower() != CONTROL_WORLD.lower():
                if time.monotonic() - STATE.phase_started > PHASE_TIMEOUT:
                    fail("HUB editor world did not become ready")
                return
            if time.monotonic() - STATE.phase_started < 3.0:
                return
            set_phase("wait_control_pie")
            LEVEL_EDITOR.editor_request_begin_play()
            return

        if STATE.phase == "wait_control_pie":
            world = game_world()
            if not world or canonical_world(world).lower() != CONTROL_WORLD.lower():
                if time.monotonic() - STATE.phase_started > PHASE_TIMEOUT:
                    fail("HUB PIE world did not become ready")
                return
            subsystem = find_subsystem(world, STATE.subsystem_class)
            if not subsystem or float(unreal.GameplayStatics.get_time_seconds(world)) < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(
                world, "EF.Calysto.Automation.SetPopulationScenario " + SCENARIO
            )
            unreal.SystemLibrary.execute_console_command(
                world, "EF.Calysto.Automation.ForceDungeonEdge 30"
            )
            # OpenLevel may also pump callbacks.  Mark the destination-wait phase
            # before invoking the Director so this request remains one-shot.
            set_phase("wait_dungeon_ready")
            if not reflected(subsystem, "request_start_new_run_with_seed")(RUN_SEED):
                fail("RequestStartNewRunWithSeed rejected the armed V4 scenario")
                return
            return

        if STATE.phase == "wait_dungeon_ready":
            world = game_world()
            if world and canonical_world(world).lower() == DUNGEON_WORLD.lower():
                STATE.saw_dungeon_world = True
                subsystem = find_subsystem(world, STATE.subsystem_class)
                if subsystem and float(unreal.GameplayStatics.get_time_seconds(world)) >= 3.0:
                    if validate_ready(world, subsystem):
                        finish(True)
                        return
            elif (
                STATE.saw_dungeon_world
                and world
                and canonical_world(world).lower() == CONTROL_WORLD.lower()
            ):
                subsystem = find_subsystem(world, STATE.subsystem_class)
                if subsystem:
                    snapshot = reflected(subsystem, "get_snapshot")()
                    try:
                        failure_code = str(prop(snapshot, "LastFailureCode"))
                        failure_message = str(prop(snapshot, "LastFailureMessage"))
                    except Exception:
                        failure_code = "DIRECTOR_RETURNED_TO_HUB"
                        failure_message = "The Director returned to HUB before FloorReady."
                    fail("{}: {}".format(failure_code, failure_message))
                    return
            if time.monotonic() - STATE.phase_started > PHASE_TIMEOUT:
                fail("The V4 extreme scenario did not reach runtime readiness")
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v4_extremes_pie58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v4_extremes_pie58 = STATE
unreal.log("[CalystoV4ExtremesPIE58] validator registered scenario=" + SCENARIO)
