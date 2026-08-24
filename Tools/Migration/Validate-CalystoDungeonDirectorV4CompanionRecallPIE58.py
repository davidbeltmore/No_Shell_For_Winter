"""Real PIE gate for Dungeon Director V4 companion recall lifecycle.

The validator exercises production paths only:

* ACF AddExistingCharacterToGroup observes both recruitments.
* AACFCharacter.KillCharacter emits the canonical death edges.
* Advance commits the first PendingDead record.
* AProjectCalystoChestV4.GatherItem moves the exact live ACF inventory entry.
* UACFInventoryComponent.UseConsumableOnTarget opens the real transaction/menu.
* The reflected selection delegate supplies the real transaction GUID.
* CancelPendingRevival and ConfirmPendingRevival operate on that frozen GUID.

No roster or inventory array is edited through reflection.  If Unreal Python
cannot bind the BlueprintAssignable selection delegate, the validator records
that precise capability gap, verifies cancellation through New Run, and emits
PASS_WITH_GAPS instead of pretending the transaction was completed.
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
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V4_COMPANION_RECALL_OUTPUT"]).resolve()
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V4_COMPANION_RECALL_SEED", "202608210505"))

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = CONTENT_DIR / "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
ROSTER_CLASS = "/Script/EFProjectSystemsGameplay.ProjectRunCompanionSubsystem"
GROUP_CLASS = "/Script/AIFramework.ACFCompanionGroupAIComponent"
EQUIPMENT_CLASS = "/Script/InventorySystem.ACFEquipmentComponent"
CHEST_CLASS = "/Script/EFProjectSystemsGameplay.ProjectCalystoChestV4"
MENU_CLASS = "/Script/EFProjectSystemsGameplay.ProjectCompanionRevivalMenuWidget"
DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS = "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
ANCHOR_CLASS = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
RECRUITMENT_CLASS = "/Script/EFProjectSystemsGameplay.ProjectRecruitableCompanionComponent"
DEATH_PROXY_CLASS = "/Script/EFProjectSystemsGameplay.ProjectCompanionDeathProxyComponent"
RECALL_CLASS = "/Game/_Game/Items/Companions/BP_Item_WintersRecall.BP_Item_WintersRecall_C"
FEMALE_CLASS = "/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C"
MALE_CLASS = "/Game/_Game/Characters/Male/ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C"
FEMALE_CATALOG = "NPC.Companion.Generalist.Female"
MALE_CATALOG = "NPC.Companion.Generalist.Male"
RECALL_CATALOG = "Item.CompanionRevival.WintersRecall"
NPC_TAG = "EF.Calysto.V4.Category.NPC"
POPULATION_TAG = "EF.Calysto.Population.V4"
SCENARIO = "CompanionRecallLifecycle"
FORCED_EDGE = 26

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
GLOBAL_TIMEOUT = 900.0
PHASE_TIMEOUT = 180.0


def object_path(value):
    try:
        return value.get_path_name() if value else ""
    except Exception:
        return ""


def reference_text(value):
    if value is None:
        return ""
    for method_name in ("to_soft_object_path", "get_asset_path_name"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    return object_path(value) or str(value)


def canonical_world(world):
    return re.sub(r"uedpie_\d+_", "", object_path(world), flags=re.IGNORECASE)


def game_world():
    return EDITOR_LEVEL_LIBRARY.get_game_world()


def prop(owner, name):
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()
    candidates = (name, snake, name[0].lower() + name[1:])
    for candidate in candidates:
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
        raise RuntimeError("Missing reflected method {} on {}".format(name, object_path(owner)))
    return method


def actors_of_class(world, actor_class):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def actors_with_tag(world, tag):
    return list(unreal.GameplayStatics.get_all_actors_with_tag(world, tag))


def objects_of_class(object_class):
    result = []
    if not object_class:
        return result
    try:
        for candidate in unreal.ObjectIterator(object_class):
            if candidate and candidate.get_class() == object_class:
                result.append(candidate)
    except Exception:
        pass
    return result


def active_menu_objects():
    result = []
    for candidate in objects_of_class(STATE.menu_class):
        path = object_path(candidate)
        if "Default__" in path:
            continue
        is_in_viewport = getattr(candidate, "is_in_viewport", None)
        try:
            if callable(is_in_viewport) and bool(is_in_viewport()):
                result.append(candidate)
        except Exception:
            pass
    return result


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


def get_component(actor, component_class):
    if not actor or not component_class:
        return None
    method = getattr(actor, "get_component_by_class", None)
    if callable(method):
        try:
            return method(component_class)
        except Exception:
            pass
    return None


def player_pawn(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    if not pawn:
        raise RuntimeError("The local player pawn is unavailable")
    return pawn


def player_group(world):
    pawn = player_pawn(world)
    controller = reflected(pawn, "get_controller")()
    group = get_component(controller, STATE.group_class)
    if not group:
        raise RuntimeError("The typed ACF companion group is unavailable")
    return group


def player_equipment(world):
    pawn = player_pawn(world)
    equipment = get_component(pawn, STATE.equipment_class)
    if not equipment:
        controller = reflected(pawn, "get_controller")()
        equipment = get_component(controller, STATE.equipment_class)
    if not equipment:
        raise RuntimeError("The typed ACF equipment inventory is unavailable")
    return equipment


def group_size(group):
    return int(reflected(group, "get_group_size")())


def group_actors(group):
    raw = reflected(group, "get_group_agents")()
    if isinstance(raw, tuple) and len(raw) == 1:
        raw = raw[0]
    agents = list(raw or [])
    result = []
    for agent in agents:
        try:
            actor = prop(agent, "AICharacter")
        except Exception:
            actor = None
        if actor:
            result.append(actor)
    return result


def state_contains(value, token):
    return token.lower() in str(value).lower().replace("_", "")


def guid_text(value):
    try:
        return "".join("{:08X}".format(int(prop(value, field)) & 0xFFFFFFFF) for field in ("A", "B", "C", "D"))
    except Exception:
        pass
    labeled = re.findall(r"(?i)[ABCD]\s*=\s*([0-9a-f]{1,8})", str(value))
    if len(labeled) == 4:
        return "".join(part.zfill(8) for part in labeled).upper()
    compact = re.fullmatch(r"(?i)[({]?([0-9a-f]{32})[)}]?", str(value).strip())
    return compact.group(1).upper() if compact else ""


def hash_document(intent, manifest):
    return {
        "intent": str(prop(intent, "IntentHash")).upper(),
        "manifest": str(prop(manifest, "ManifestHash")).upper(),
        "topology": str(prop(manifest, "AnchorTopologyHash")).upper(),
        "population": str(prop(manifest, "PopulationHash")).upper(),
        "resource": str(prop(manifest, "ResourceHash")).upper(),
        "companion": str(prop(manifest, "CompanionSnapshotHash")).upper(),
    }


def roster_document(roster):
    snapshot = reflected(roster, "get_run_roster_snapshot")()
    result_entries = []
    for entry in list(prop(snapshot, "Entries")):
        definition = prop(entry, "Definition")
        result_entries.append(
            {
                "stable_id": guid_text(prop(definition, "StableCompanionId")),
                "content_id": str(prop(definition, "ContentId")),
                "character_class": reference_text(prop(definition, "CharacterClass")),
                "archetype": str(prop(definition, "Archetype")),
                "gender": str(prop(definition, "Gender")),
                "grade": str(prop(definition, "DifficultyGrade")),
                "level": int(prop(definition, "ResolvedLevel")),
                "lifecycle": str(prop(definition, "Lifecycle")),
                "state": str(prop(entry, "State")),
                "death_floor": int(prop(entry, "DeathFloor")),
                "death_serial": int(prop(entry, "DeathGenerationSerial")),
            }
        )
    result_entries.sort(key=lambda row: row["stable_id"])
    return {
        "run_epoch": int(prop(snapshot, "RunEpoch")),
        "floor": int(prop(snapshot, "FloorNumber")),
        "serial": int(prop(snapshot, "GenerationSerial")),
        "snapshot_hash": str(prop(snapshot, "SnapshotHash")).upper(),
        "entries": result_entries,
        "active_party": sorted(guid_text(value) for value in list(prop(snapshot, "ActiveParty"))),
    }


def raw_roster_entry(roster, stable_id_text):
    snapshot = reflected(roster, "get_run_roster_snapshot")()
    for entry in list(prop(snapshot, "Entries")):
        definition = prop(entry, "Definition")
        if guid_text(prop(definition, "StableCompanionId")) == stable_id_text:
            return entry, definition, prop(definition, "StableCompanionId")
    return None, None, None


def candidate_document(roster):
    result = []
    for candidate in list(reflected(roster, "get_revival_candidates")()):
        result.append(
            {
                "stable_id": guid_text(prop(candidate, "StableCompanionId")),
                "archetype": str(prop(candidate, "Archetype")),
                "gender": str(prop(candidate, "Gender")),
                "grade": str(prop(candidate, "DifficultyGrade")),
                "level": int(prop(candidate, "ResolvedLevel")),
                "pending_advance": bool(prop(candidate, "bDeathPendingAdvance")),
            }
        )
    result.sort(key=lambda row: row["stable_id"])
    return result


def inventory_item_document(entry):
    item = prop(entry, "Item")
    return {
        "guid": guid_text(prop(entry, "ItemGuid")),
        "count": int(prop(entry, "Count")),
        "item_class": reference_text(prop(entry, "ItemClass")),
        "live_item": object_path(item),
        "live_item_class": object_path(item.get_class()) if item else "",
    }


def recall_entries(inventory):
    result = []
    for entry in list(reflected(inventory, "get_inventory")()):
        document = inventory_item_document(entry)
        if RECALL_CLASS.lower() in (document["item_class"] + " " + document["live_item_class"]).lower():
            result.append((entry, document))
    return result


def manifest_instances(manifest):
    result = []
    for instance in list(prop(manifest, "Instances")):
        verified = []
        for content in list(prop(instance, "VerifiedChestContents")):
            verified.append(
                {
                    "container_id": str(prop(content, "ContainerInstanceId")),
                    "attempt_id": str(prop(content, "StableAttemptId")),
                    "catalog_id": str(prop(content, "ContentCatalogId")),
                    "content_class": reference_text(prop(content, "ContentClass")),
                    "tier": str(prop(content, "Tier")),
                    "cooldown": int(prop(content, "CooldownFloors")),
                }
            )
        result.append(
            {
                "stable_instance_id": str(prop(instance, "StableInstanceId")),
                "stable_companion_id": guid_text(prop(instance, "StableCompanionId")),
                "catalog_id": str(prop(instance, "CatalogId")),
                "category": str(prop(instance, "Category")),
                "actor_class": reference_text(prop(instance, "ActorClass")),
                "tier": str(prop(instance, "Tier")),
                "verified_content_ids": [str(value) for value in list(prop(instance, "VerifiedChestContentIds"))],
                "verified_contents": verified,
            }
        )
    return result


def native_policy_document(policy):
    policy_class = unreal.load_class(None, POLICY_CLASS)
    if not policy or not policy_class or policy.get_class() != policy_class:
        raise RuntimeError("The authored V4 policy or exact native class is missing")
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
    if document["schema_version"] != 4 or document["generator_version"] != 4 or not is_sha256(document["policy_hash"]):
        raise RuntimeError("The authored V4 policy identity/hash is invalid")
    return document


class State:
    def __init__(self):
        self.phase = "load_map"
        self.started = time.monotonic()
        self.phase_started = self.started
        self.callback = None
        self.finished = False
        self.director_class = None
        self.roster_class = None
        self.group_class = None
        self.equipment_class = None
        self.chest_class = None
        self.menu_class = None
        self.door_class = None
        self.dungeon_class = None
        self.anchor_class = None
        self.recruitment_class = None
        self.death_proxy_class = None
        self.recall_class = None
        self.female_class = None
        self.male_class = None
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_states()
        self.initial_protected = protected_hashes()
        self.policy_sha_before = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
        self.policy = {}
        self.evidence = {}
        self.capability_gaps = []
        self.selection_events = []
        self.selection_callback = None
        self.initial_hashes = {}
        self.initial_epoch = 0
        self.female_id = ""
        self.male_id = ""
        self.male_guid = None
        self.male_actor_path = ""
        self.chest_recall_guid = ""
        self.recall_guid = ""
        self.recall_entry = None
        self.inventory_before_cancel = None
        self.roster_before_cancel = None
        self.full_transaction_pass = False
        self.last_world_time = 0.0
        self.saw_generation_transition = False


STATE = State()


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("CALYSTO_V4_COMPANION_RECALL phase={}".format(value))


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
        | {name for name, dirty in final_dirty.items() if dirty and not STATE.initial_dirty.get(name, False)}
    )
    protected_mismatches = sorted(
        name for name, before in STATE.initial_protected.items() if before != final_protected.get(name)
    )
    policy_sha_after = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
    if success and STATE.capability_gaps:
        status = "PASS_WITH_GAPS"
    else:
        status = "PASS" if success else "FAIL"
    document = {
        "schema_version": 4,
        "generator_version": 4,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "success": bool(success),
        "status": status,
        "strict_acceptance": bool(success and STATE.full_transaction_pass and not STATE.capability_gaps),
        "full_transaction_pass": bool(STATE.full_transaction_pass),
        "capability_gaps": list(STATE.capability_gaps),
        "phase": STATE.phase,
        "error": error,
        "run_seed": RUN_SEED,
        "scenario": SCENARIO,
        "forced_dungeon_edge": FORCED_EDGE,
        "policy": STATE.policy,
        "recall_lifecycle": STATE.evidence,
        "asset_saves": asset_saves,
        "asset_mutations": asset_mutations,
        "dirty_before": STATE.initial_dirty,
        "dirty_after": final_dirty,
        "protected_assets": {"before": STATE.initial_protected, "after": final_protected, "mismatches": protected_mismatches},
        "policy_sha256_before": STATE.policy_sha_before,
        "policy_sha256_after": policy_sha_after,
    }
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CALYSTO_V4_COMPANION_RECALL_RESULT " + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v4_companion_recall_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message):
    finish(False, message)


def runtime_objects(world):
    return find_subsystem(world, STATE.director_class), find_subsystem(world, STATE.roster_class)


def director_is_ready(director):
    snapshot = reflected(director, "get_snapshot")()
    return state_contains(prop(snapshot, "State"), "ready") and bool(prop(snapshot, "bDoorReady"))


def mark_transition(world, director):
    if not world or canonical_world(world).lower() != DUNGEON_WORLD.lower():
        STATE.saw_generation_transition = True
        return
    try:
        snapshot = reflected(director, "get_snapshot")() if director else None
        now = float(unreal.GameplayStatics.get_time_seconds(world))
        if not snapshot or not state_contains(prop(snapshot, "State"), "ready") or now + 0.25 < STATE.last_world_time:
            STATE.saw_generation_transition = True
    except Exception:
        STATE.saw_generation_transition = True


def begin_travel(world, next_phase, request, failure_text):
    STATE.last_world_time = float(unreal.GameplayStatics.get_time_seconds(world))
    STATE.saw_generation_transition = False
    set_phase(next_phase)
    if not request():
        raise RuntimeError(failure_text)


def assert_checks(label, checks):
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("{} failed: {}".format(label, ", ".join(failed)))


def audit_floor(world, director, roster, expected_floor, expected_serial, expected_catalog, expected_chests):
    snapshot = reflected(director, "get_snapshot")()
    intent = reflected(director, "get_resolved_floor_intent")()
    manifest = reflected(director, "get_realized_floor_manifest")()
    roster_doc = roster_document(roster)
    group = player_group(world)
    live_npcs = actors_with_tag(world, NPC_TAG)
    chests = actors_of_class(world, STATE.chest_class)
    doors = actors_of_class(world, STATE.door_class)
    dungeons = actors_of_class(world, STATE.dungeon_class)
    anchors = actors_of_class(world, STATE.anchor_class)
    population = actors_with_tag(world, POPULATION_TAG)
    hashes = hash_document(intent, manifest)
    size = prop(intent, "DungeonSize")
    instances = manifest_instances(manifest)
    npc_instances = [row for row in instances if state_contains(row["category"], "npc")]
    chest_instances = [row for row in instances if state_contains(row["category"], "chest")]
    door_enabled = False
    if len(doors) == 1:
        for field in ("bIsEnabled", "IsEnabled"):
            try:
                door_enabled = bool(prop(doors[0], field))
                break
            except Exception:
                pass
    expected_actor_class = FEMALE_CLASS if expected_catalog == FEMALE_CATALOG else MALE_CLASS
    checks = {
        "director_ready": director_is_ready(director),
        "intent_valid": bool(prop(intent, "bIsValid")),
        "manifest_valid": bool(prop(manifest, "bIsValid")),
        "identity": int(prop(intent, "RunSeed")) == RUN_SEED and int(prop(intent, "FloorNumber")) == expected_floor and int(prop(intent, "GenerationSerial")) == expected_serial,
        "manifest_identity": int(prop(manifest, "RunSeed")) == RUN_SEED and int(prop(manifest, "FloorNumber")) == expected_floor and int(prop(manifest, "GenerationSerial")) == expected_serial,
        "snapshot_identity": int(prop(snapshot, "FloorNumber")) == expected_floor and int(prop(snapshot, "GenerationSerial")) == expected_serial,
        "forced_edge": int(prop(size, "X")) == FORCED_EDGE and int(prop(size, "Y")) == FORCED_EDGE and int(prop(size, "Z")) == 1,
        "scenario": SCENARIO.lower() in str(prop(intent, "DevelopmentPopulationScenario")).lower(),
        "one_expected_npc": int(prop(manifest, "NPCCount")) == 1 and len(live_npcs) == 1 and len(npc_instances) == 1,
        "npc_catalog": len(npc_instances) == 1 and npc_instances[0]["catalog_id"] == expected_catalog,
        "npc_class": len(live_npcs) == 1 and live_npcs[0].get_class() == (STATE.female_class if expected_catalog == FEMALE_CATALOG else STATE.male_class) and expected_actor_class.lower() in npc_instances[0]["actor_class"].lower(),
        "chest_count": int(prop(manifest, "ChestCount")) == expected_chests and len(chests) == expected_chests and len(chest_instances) == expected_chests,
        "other_categories_zero": all(int(prop(manifest, field)) == 0 for field in ("EnemyCount", "FoodCount", "LooseLootCount", "ClothingCount", "SpecialEventCount")),
        "spawned_actor_count": int(prop(manifest, "SpawnedActorCount")) == 1 + expected_chests and len(population) == 1 + expected_chests,
        "roster_ready": bool(prop(snapshot, "bCompanionReady")) and bool(reflected(roster, "is_companion_roster_ready")()),
        "door": len(doors) == 1 and door_enabled,
        "dungeon": len(dungeons) == 1,
        "anchors_destroyed": len(anchors) == 0,
        "hashes": all(is_sha256(value) for value in hashes.values()),
        "hash_identity": hashes["intent"] == str(prop(manifest, "IntentHash")).upper() and hashes["companion"] == str(prop(intent, "CompanionSnapshotHash")).upper() == str(prop(snapshot, "CompanionSnapshotHash")).upper(),
    }
    if expected_chests == 0:
        checks["no_chest_directive"] = len(list(prop(intent, "ChestContentDirectives"))) == 0
    else:
        directives = list(prop(intent, "ChestContentDirectives"))
        contents = chest_instances[0]["verified_contents"] if len(chest_instances) == 1 else []
        checks.update(
            {
                "one_recall_directive": len(directives) == 1 and str(prop(directives[0], "ContentCatalogId")) == RECALL_CATALOG,
                "recall_epic": len(directives) == 1 and state_contains(prop(directives[0], "Tier"), "epic") and int(prop(directives[0], "CooldownFloors")) == 8,
                "recall_class": len(directives) == 1 and RECALL_CLASS.lower() in reference_text(prop(directives[0], "ContentClass")).lower(),
                "verified_recall": len(contents) == 1 and contents[0]["catalog_id"] == RECALL_CATALOG and state_contains(contents[0]["tier"], "epic") and contents[0]["cooldown"] == 8 and RECALL_CLASS.lower() in contents[0]["content_class"].lower(),
            }
        )
    assert_checks("Floor {} audit".format(expected_floor), checks)
    return {
        "snapshot": {"floor": expected_floor, "serial": expected_serial, "companion_ready": bool(prop(snapshot, "bCompanionReady"))},
        "roster": roster_doc,
        "group_size": group_size(group),
        "hashes": hashes,
        "instances": instances,
        "checks": checks,
    }


def bind_selection_delegate(roster):
    errors = []
    try:
        delegate = prop(roster, "OnRevivalSelectionRequested")
    except Exception as exc:
        return False, "OnRevivalSelectionRequested is not reflected: {}".format(exc)
    STATE.selection_callback = on_revival_selection
    for method_name in ("add_callable_unique", "add_callable"):
        method = getattr(delegate, method_name, None)
        if not callable(method):
            continue
        try:
            method(STATE.selection_callback)
            return True, method_name
        except Exception as exc:
            errors.append("{}: {}".format(method_name, exc))
    return False, "Blueprint delegate exposes no usable Python callable binder ({})".format("; ".join(errors) or "no methods")


def on_revival_selection(transaction_id, candidates):
    raw_candidates = list(candidates or [])
    document = []
    for candidate in raw_candidates:
        document.append(
            {
                "stable_id": guid_text(prop(candidate, "StableCompanionId")),
                "pending_advance": bool(prop(candidate, "bDeathPendingAdvance")),
            }
        )
    document.sort(key=lambda row: row["stable_id"])
    STATE.selection_events.append(
        {"transaction": transaction_id, "transaction_id": guid_text(transaction_id), "candidates": document}
    )
    unreal.log("CALYSTO_V4_COMPANION_RECALL_SELECTION transaction={} candidates={}".format(guid_text(transaction_id), len(document)))


def start_recall_use(world, inventory, entry):
    pawn = player_pawn(world)
    can_use = bool(reflected(inventory, "can_use_consumable")(entry, pawn))
    if not can_use:
        raise RuntimeError("ACF CanUseConsumable rejected the exact Winter's Recall entry")
    reflected(inventory, "use_consumable_on_target")(entry, pawn)


def start_new_run_cleanup(world, director):
    begin_travel(
        world,
        "wait_new_run_cleanup",
        lambda: reflected(director, "request_start_new_run_with_seed")(RUN_SEED),
        "Same-seed New Run cleanup was rejected",
    )


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
                raise RuntimeError("The recall gate requires a positive seed and the authored V4 policy")
            STATE.policy = native_policy_document(unreal.load_asset(POLICY_OBJECT))
            STATE.director_class = unreal.load_class(None, DIRECTOR_CLASS)
            STATE.roster_class = unreal.load_class(None, ROSTER_CLASS)
            STATE.group_class = unreal.load_class(None, GROUP_CLASS)
            STATE.equipment_class = unreal.load_class(None, EQUIPMENT_CLASS)
            STATE.chest_class = unreal.load_class(None, CHEST_CLASS)
            STATE.menu_class = unreal.load_class(None, MENU_CLASS)
            STATE.door_class = unreal.load_class(None, DOOR_CLASS)
            STATE.dungeon_class = unreal.load_class(None, DUNGEON_CLASS)
            STATE.anchor_class = unreal.load_class(None, ANCHOR_CLASS)
            STATE.recruitment_class = unreal.load_class(None, RECRUITMENT_CLASS)
            STATE.death_proxy_class = unreal.load_class(None, DEATH_PROXY_CLASS)
            STATE.recall_class = unreal.load_class(None, RECALL_CLASS)
            STATE.female_class = unreal.load_class(None, FEMALE_CLASS)
            STATE.male_class = unreal.load_class(None, MALE_CLASS)
            required = (
                STATE.director_class, STATE.roster_class, STATE.group_class, STATE.equipment_class,
                STATE.chest_class, STATE.menu_class, STATE.door_class, STATE.dungeon_class,
                STATE.anchor_class, STATE.recruitment_class, STATE.death_proxy_class,
                STATE.recall_class, STATE.female_class, STATE.male_class,
            )
            if not all(required):
                raise RuntimeError("A required recall runtime class failed to load")
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
            director, roster = runtime_objects(world)
            if not director or not roster or float(unreal.GameplayStatics.get_time_seconds(world)) < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(world, "EF.Calysto.Automation.SetPopulationScenario " + SCENARIO)
            unreal.SystemLibrary.execute_console_command(world, "EF.Calysto.Automation.ForceDungeonEdge {}".format(FORCED_EDGE))
            set_phase("wait_floor1_ready")
            if not reflected(director, "request_start_new_run_with_seed")(RUN_SEED):
                raise RuntimeError("Initial New Run request was rejected")
            return

        director, roster = runtime_objects(world) if world else (None, None)
        if STATE.phase in ("wait_floor1_ready", "wait_floor2_ready", "wait_new_run_cleanup"):
            mark_transition(world, director)
        if not world or canonical_world(world).lower() != DUNGEON_WORLD.lower() or not director or not roster:
            return

        if STATE.phase == "wait_floor1_ready":
            if not director_is_ready(director) or float(unreal.GameplayStatics.get_time_seconds(world)) < 3.0:
                return
            initial = audit_floor(world, director, roster, 1, 1, FEMALE_CATALOG, 0)
            checks = {
                "roster_empty": len(initial["roster"]["entries"]) == 0 and not initial["roster"]["active_party"],
                "group_empty": group_size(player_group(world)) == 0,
                "no_candidates": len(candidate_document(roster)) == 0,
                "no_recall_owned": len(recall_entries(player_equipment(world))) == 0,
            }
            assert_checks("Floor 1 pre-recruitment", checks)
            initial["pre_recruitment_checks"] = checks
            STATE.evidence["floor1_initial"] = initial
            STATE.initial_hashes = initial["hashes"]
            STATE.initial_epoch = int(reflected(roster, "get_run_epoch")())
            local_female = actors_with_tag(world, NPC_TAG)[0]
            set_phase("wait_female_recruited")
            if not reflected(player_group(world), "add_existing_character_to_group")(local_female):
                raise RuntimeError("ACF rejected Female AddExistingCharacterToGroup")
            return

        if STATE.phase == "wait_female_recruited":
            doc = roster_document(roster)
            if len(doc["entries"]) != 1 or len(doc["active_party"]) != 1:
                return
            female = doc["entries"][0]
            checks = {
                "female_catalog": female["content_id"] == FEMALE_CATALOG,
                "female_class": FEMALE_CLASS.lower() in female["character_class"].lower(),
                "female_alive": state_contains(female["state"], "alive"),
                "female_guid": len(female["stable_id"]) == 32,
                "group_one": group_size(player_group(world)) == 1,
            }
            assert_checks("Female recruitment", checks)
            STATE.female_id = female["stable_id"]
            STATE.evidence["female_recruited"] = {"roster": doc, "checks": checks}
            set_phase("wait_female_pending_dead")
            reflected(actors_with_tag(world, NPC_TAG)[0], "kill_character")()
            return

        if STATE.phase == "wait_female_pending_dead":
            doc = roster_document(roster)
            if len(doc["entries"]) != 1 or not state_contains(doc["entries"][0]["state"], "pendingdead"):
                return
            candidates = candidate_document(roster)
            checks = {
                "same_female": doc["entries"][0]["stable_id"] == STATE.female_id,
                "pending_floor1": doc["entries"][0]["death_floor"] == 1 and doc["entries"][0]["death_serial"] == 1,
                "pending_candidate": len(candidates) == 1 and candidates[0]["stable_id"] == STATE.female_id and candidates[0]["pending_advance"],
                "not_confirmed": not bool(reflected(roster, "has_confirmed_dead_companion")()),
                "group_empty": group_size(player_group(world)) == 0,
            }
            assert_checks("Female PendingDead", checks)
            STATE.evidence["female_pending_dead"] = {"roster": doc, "candidates": candidates, "checks": checks}
            begin_travel(world, "wait_floor2_ready", lambda: reflected(director, "request_advance_floor")(), "Advance to Floor 2 was rejected")
            return

        if STATE.phase == "wait_floor2_ready":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            floor2 = audit_floor(world, director, roster, 2, 2, MALE_CATALOG, 1)
            doc = floor2["roster"]
            candidates = candidate_document(roster)
            chests = actors_of_class(world, STATE.chest_class)
            chest_items = list(reflected(chests[0], "get_items")()) if len(chests) == 1 else []
            chest_docs = [inventory_item_document(entry) for entry in chest_items]
            equipment = player_equipment(world)
            checks = {
                "female_confirmed_dead": len(doc["entries"]) == 1 and doc["entries"][0]["stable_id"] == STATE.female_id and state_contains(doc["entries"][0]["state"], "dead"),
                "female_death_metadata": len(doc["entries"]) == 1 and doc["entries"][0]["death_floor"] == 1 and doc["entries"][0]["death_serial"] == 1,
                "confirmed_candidate": len(candidates) == 1 and candidates[0]["stable_id"] == STATE.female_id and not candidates[0]["pending_advance"],
                "confirmed_dead_api": bool(reflected(roster, "has_confirmed_dead_companion")()),
                "group_empty": group_size(player_group(world)) == 0,
                "one_storage_entry": len(chest_docs) == 1,
                "storage_recall": len(chest_docs) == 1 and RECALL_CLASS.lower() in (chest_docs[0]["item_class"] + chest_docs[0]["live_item_class"]).lower(),
                "storage_count_one": len(chest_docs) == 1 and chest_docs[0]["count"] == 1 and len(chest_docs[0]["guid"]) == 32,
                "not_owned_before_gather": len(recall_entries(equipment)) == 0,
            }
            assert_checks("Floor 2 confirmed death/chest", checks)
            floor2["confirmed_death_and_chest"] = {"candidates": candidates, "storage": chest_docs, "checks": checks}
            STATE.evidence["floor2_ready"] = floor2
            STATE.chest_recall_guid = chest_docs[0]["guid"]
            set_phase("wait_recall_gathered")
            reflected(chests[0], "gather_item")(equipment)
            return

        if STATE.phase == "wait_recall_gathered":
            equipment = player_equipment(world)
            owned = recall_entries(equipment)
            if len(owned) != 1:
                return
            entry, document = owned[0]
            checks = {
                "exact_count": document["count"] == 1,
                "exact_guid": len(document["guid"]) == 32,
                "guid_preserved_from_chest": document["guid"] == STATE.chest_recall_guid,
                "live_item": bool(document["live_item"]) and document["live_item_class"] == RECALL_CLASS,
                "storage_drained": all(len(list(reflected(chest, "get_items")())) == 0 for chest in actors_of_class(world, STATE.chest_class)),
            }
            assert_checks("Real ACF chest GatherItem", checks)
            STATE.recall_guid = document["guid"]
            STATE.recall_entry = entry
            STATE.evidence["recall_gathered"] = {"inventory_entry": document, "checks": checks, "path": "AProjectCalystoChestV4.GatherItem -> UACFInventoryComponent.MoveItemsFromInventory"}
            male = actors_with_tag(world, NPC_TAG)[0]
            STATE.male_actor_path = object_path(male)
            set_phase("wait_male_recruited")
            if not reflected(player_group(world), "add_existing_character_to_group")(male):
                raise RuntimeError("ACF rejected Male AddExistingCharacterToGroup")
            return

        if STATE.phase == "wait_male_recruited":
            doc = roster_document(roster)
            if len(doc["entries"]) != 2 or len(doc["active_party"]) != 1:
                return
            male = next((entry for entry in doc["entries"] if entry["content_id"] == MALE_CATALOG), None)
            female = next((entry for entry in doc["entries"] if entry["stable_id"] == STATE.female_id), None)
            checks = {
                "female_stays_dead": female is not None and state_contains(female["state"], "dead"),
                "male_alive": male is not None and state_contains(male["state"], "alive"),
                "male_class": male is not None and MALE_CLASS.lower() in male["character_class"].lower(),
                "male_guid": male is not None and len(male["stable_id"]) == 32 and male["stable_id"] != STATE.female_id,
                "male_active": male is not None and doc["active_party"] == [male["stable_id"]],
                "group_one": group_size(player_group(world)) == 1,
            }
            assert_checks("Male recruitment", checks)
            STATE.male_id = male["stable_id"]
            _, _, STATE.male_guid = raw_roster_entry(roster, STATE.male_id)
            STATE.evidence["male_recruited"] = {"roster": doc, "checks": checks}
            set_phase("wait_male_pending_dead")
            reflected(actors_with_tag(world, NPC_TAG)[0], "kill_character")()
            return

        if STATE.phase == "wait_male_pending_dead":
            doc = roster_document(roster)
            male = next((entry for entry in doc["entries"] if entry["stable_id"] == STATE.male_id), None)
            female = next((entry for entry in doc["entries"] if entry["stable_id"] == STATE.female_id), None)
            if male is None or not state_contains(male["state"], "pendingdead"):
                return
            candidates = candidate_document(roster)
            by_id = {row["stable_id"]: row for row in candidates}
            checks = {
                "female_dead": female is not None and state_contains(female["state"], "dead"),
                "male_pending": male["death_floor"] == 2 and male["death_serial"] == 2,
                "two_candidates": len(candidates) == 2 and set(by_id) == {STATE.female_id, STATE.male_id},
                "female_confirmed": STATE.female_id in by_id and not by_id[STATE.female_id]["pending_advance"],
                "male_pending_candidate": STATE.male_id in by_id and by_id[STATE.male_id]["pending_advance"],
                "group_empty": group_size(player_group(world)) == 0,
                "recall_preserved": len(recall_entries(player_equipment(world))) == 1 and recall_entries(player_equipment(world))[0][1]["guid"] == STATE.recall_guid,
            }
            assert_checks("Male PendingDead", checks)
            STATE.evidence["male_pending_dead"] = {"roster": doc, "candidates": candidates, "checks": checks}
            bound, detail = bind_selection_delegate(roster)
            STATE.evidence["python_selection_delegate"] = {"bound": bound, "detail": detail}
            equipment = player_equipment(world)
            entry, _ = recall_entries(equipment)[0]
            STATE.inventory_before_cancel = inventory_item_document(entry)
            STATE.roster_before_cancel = doc
            set_phase("wait_cancel_selection")
            start_recall_use(world, equipment, entry)
            if not bound:
                STATE.capability_gaps.append(detail)
            return

        if STATE.phase == "wait_cancel_selection":
            if not STATE.selection_events:
                if time.monotonic() - STATE.phase_started < 5.0:
                    return
                menu_count = len(active_menu_objects())
                owned = recall_entries(player_equipment(world))
                gap = "Unreal Python did not receive OnRevivalSelectionRequested; the real menu/transaction opened but its generated GUID cannot be read without reflection mutation."
                if gap not in STATE.capability_gaps:
                    STATE.capability_gaps.append(gap)
                checks = {
                    "menu_object_exists": menu_count >= 1,
                    "item_not_consumed": len(owned) == 1 and owned[0][1]["guid"] == STATE.recall_guid and owned[0][1]["count"] == 1,
                    "roster_unchanged": roster_document(roster) == STATE.roster_before_cancel,
                }
                assert_checks("Maximum reflection-gap coverage", checks)
                STATE.evidence["reflection_gap_open_transaction"] = {"checks": checks, "menu_objects": menu_count}
                start_new_run_cleanup(world, director)
                return
            event = STATE.selection_events[0]
            by_id = {row["stable_id"]: row for row in event["candidates"]}
            checks = {
                "transaction_guid": len(event["transaction_id"]) == 32,
                "two_frozen_candidates": set(by_id) == {STATE.female_id, STATE.male_id},
                "female_confirmed": not by_id[STATE.female_id]["pending_advance"],
                "male_pending": by_id[STATE.male_id]["pending_advance"],
                "item_unchanged": recall_entries(player_equipment(world))[0][1] == STATE.inventory_before_cancel,
            }
            assert_checks("First real revival selection", checks)
            reflected(roster, "cancel_pending_revival")(event["transaction"])
            STATE.evidence["cancel_selection"] = {"transaction_id": event["transaction_id"], "candidates": event["candidates"], "checks_before_cancel": checks}
            set_phase("wait_cancelled")
            return

        if STATE.phase == "wait_cancelled":
            if time.monotonic() - STATE.phase_started < 0.5:
                return
            owned = recall_entries(player_equipment(world))
            checks = {
                "exact_item_not_consumed": len(owned) == 1 and owned[0][1] == STATE.inventory_before_cancel,
                "roster_unchanged": roster_document(roster) == STATE.roster_before_cancel,
                "group_empty": group_size(player_group(world)) == 0,
            }
            assert_checks("Cancel transaction", checks)
            STATE.evidence["cancel_selection"]["checks_after_cancel"] = checks
            set_phase("wait_confirm_selection")
            start_recall_use(world, player_equipment(world), owned[0][0])
            return

        if STATE.phase == "wait_confirm_selection":
            if len(STATE.selection_events) < 2:
                return
            event = STATE.selection_events[1]
            checks = {
                "new_transaction": event["transaction_id"] != STATE.selection_events[0]["transaction_id"] and len(event["transaction_id"]) == 32,
                "same_candidate_set": {row["stable_id"] for row in event["candidates"]} == {STATE.female_id, STATE.male_id},
                "item_still_exact": recall_entries(player_equipment(world))[0][1] == STATE.inventory_before_cancel,
            }
            assert_checks("Second real revival selection", checks)
            set_phase("wait_male_revived")
            accepted = bool(reflected(roster, "confirm_pending_revival")(event["transaction"], STATE.male_guid))
            STATE.evidence["confirm_selection"] = {"transaction_id": event["transaction_id"], "target_stable_id": STATE.male_id, "checks_before_confirm": checks, "accepted": accepted}
            if not accepted:
                raise RuntimeError("ConfirmPendingRevival rejected the real PendingDead Male token; no synthetic fallback is permitted")
            return

        if STATE.phase == "wait_male_revived":
            doc = roster_document(roster)
            male = next((entry for entry in doc["entries"] if entry["stable_id"] == STATE.male_id), None)
            female = next((entry for entry in doc["entries"] if entry["stable_id"] == STATE.female_id), None)
            if male is None or not state_contains(male["state"], "alive"):
                return
            agents = group_actors(player_group(world))
            candidates = candidate_document(roster)
            owned = recall_entries(player_equipment(world))
            fresh_actor = agents[0] if len(agents) == 1 else None
            checks = {
                "female_stays_dead": female is not None and state_contains(female["state"], "dead") and STATE.female_id not in doc["active_party"],
                "male_same_stable_id": male["stable_id"] == STATE.male_id and doc["active_party"] == [STATE.male_id],
                "fresh_actor": fresh_actor is not None and object_path(fresh_actor) != STATE.male_actor_path,
                "fresh_actor_class": fresh_actor is not None and fresh_actor.get_class() == STATE.male_class,
                "group_one": group_size(player_group(world)) == 1 and len(agents) == 1,
                "exact_unit_removed": len(owned) == 0,
                "female_only_candidate": len(candidates) == 1 and candidates[0]["stable_id"] == STATE.female_id and not candidates[0]["pending_advance"],
            }
            assert_checks("Confirmed Male revival", checks)
            STATE.full_transaction_pass = True
            STATE.evidence["confirmed_revival"] = {
                "roster": doc,
                "candidates": candidates,
                "old_actor": STATE.male_actor_path,
                "fresh_actor": object_path(fresh_actor),
                "removed_inventory_guid": STATE.recall_guid,
                "checks": checks,
            }
            start_new_run_cleanup(world, director)
            return

        if STATE.phase == "wait_new_run_cleanup":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            reproduced = audit_floor(world, director, roster, 1, 1, FEMALE_CATALOG, 0)
            owned = recall_entries(player_equipment(world))
            candidates = candidate_document(roster)
            new_epoch = int(reflected(roster, "get_run_epoch")())
            checks = {
                "new_epoch": new_epoch > STATE.initial_epoch,
                "roster_empty": len(reproduced["roster"]["entries"]) == 0 and not reproduced["roster"]["active_party"],
                "graveyard_empty": not bool(reflected(roster, "has_confirmed_dead_companion")()) and len(candidates) == 0,
                "group_empty": group_size(player_group(world)) == 0,
                "recall_removed": len(owned) == 0,
                "same_seed_floor1": reproduced["hashes"] == STATE.initial_hashes,
                "female_local_again": len(actors_with_tag(world, NPC_TAG)) == 1 and actors_with_tag(world, NPC_TAG)[0].get_class() == STATE.female_class,
                "no_chest": len(actors_of_class(world, STATE.chest_class)) == 0,
            }
            assert_checks("New Run roster/graveyard/Recall cleanup", checks)
            reproduced["previous_epoch"] = STATE.initial_epoch
            reproduced["new_epoch"] = new_epoch
            reproduced["candidates"] = candidates
            reproduced["checks"] = checks
            STATE.evidence["new_run_cleanup"] = reproduced
            finish(True)
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v4_companion_recall_pie58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v4_companion_recall_pie58 = STATE
unreal.log("CALYSTO_V4_COMPANION_RECALL validator_registered=true")
