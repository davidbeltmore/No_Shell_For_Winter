"""Validate the Dungeon Director V4 typed ACF inventory travel contract in PIE.

The validator keeps one GameInstance across real OpenLevel travels and delegates
inventory semantics to UProjectV4InventoryTravelFixtureSubsystem.  It never
saves Content and never edits a CDO or authored asset.
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
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V4_INVENTORY_TRAVEL_OUTPUT"]).resolve()
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V4_INVENTORY_TRAVEL_SEED", "202608210505"))

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = CONTENT_DIR / "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
FIXTURE_CLASS = "/Script/EFProjectSystemsGameplay.ProjectV4InventoryTravelFixtureSubsystem"
DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS = "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
ANCHOR_CLASS = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
POPULATION_TAG = "EF.Calysto.Population.V4"
SCENARIO = "Zero"
FORCED_EDGE = 26
FIXTURE_CURRENCY_BITS = "449A5000"  # IEEE-754 float bits for exactly 1234.5f.
FIXTURE_MAX_SLOTS = 37
FIXTURE_MAX_WEIGHT = 222

MONITORED_OBJECTS = (
    POLICY_OBJECT,
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
    "/Game/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch",
    "/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge",
    "/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine",
    "/Game/FullSample/Player",
    "/Game/DazToUnreal/Female/Female",
    "/Game/DazToUnreal/Male/Male",
    "/Game/DazToUnreal/Multiple/Multiple",
)

PROTECTED_FILES = (
    "Content/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset",
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
    "Content/Calysto/Dungeon/Blueprint/Lightning/BP_WallTorch.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine.uasset",
    "Content/Calysto/Dungeon/Demo/LevelInstance/PCGDA_Forge.uasset",
    "Content/Procedural/Maps/DungeonGeneration.umap",
    "Content/Procedural/DoorToLevel.uasset",
    "Content/FullSample/Player.uasset",
    "Content/DazToUnreal/Female/Female.uasset",
    "Content/DazToUnreal/Male/Male.uasset",
    "Content/DazToUnreal/Multiple/Multiple.uasset",
    "Content/FullSample/Blueprints/Items/Ammo/ACF_Arrow_BP.uasset",
    "Content/FullSample/Blueprints/Items/Weapons/ACFShieldBP.uasset",
    "Content/_Game/Items/Companions/BP_Item_WintersRecall.uasset",
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
        raise RuntimeError("Missing reflected method {} on {}".format(name, object_path(owner)))
    return method


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


def parse_native_result(label, value):
    text = str(value).strip()
    parts = text.split("|") if text else []
    if not parts or parts[0].strip().upper() != "PASS":
        raise RuntimeError("{} failed: {}".format(label, text or "<empty>"))
    fields = {}
    for part in parts[1:]:
        if "=" not in part:
            continue
        key, item = part.split("=", 1)
        key = key.strip().lower().replace("-", "_")
        if not key or key in fields:
            raise RuntimeError("{} returned a duplicate/empty field: {}".format(label, part))
        fields[key] = item.strip()
    return {"raw": text, "fields": fields}


def require_field(result, name):
    fields = result["fields"]
    if name not in fields or fields[name] == "":
        raise RuntimeError("Native fixture result is missing '{}' in {}".format(name, result["raw"]))
    return fields[name]


def require_int(result, name, expected=None):
    value = int(require_field(result, name), 10)
    if expected is not None and value != expected:
        raise RuntimeError("Native fixture {}={} but expected {}".format(name, value, expected))
    return value


def fixture_result_checks(result, kind, epoch, floor, expect_recall, expected_delegates):
    reported_kind = require_field(result, "kind").lower().replace("_", "")
    if reported_kind != kind.lower().replace("_", ""):
        raise RuntimeError("Native fixture kind '{}' does not match '{}'".format(reported_kind, kind))
    checks = {
        "kind": True,
        "run_epoch": require_int(result, "run_epoch", epoch) == epoch,
        "floor": require_int(result, "floor", floor) == floor,
        "hash": is_sha256(require_field(result, "hash")),
        "inventory_changed": require_int(result, "inventory_changed", expected_delegates["inventory_changed"]) == expected_delegates["inventory_changed"],
        "item_added": require_int(result, "item_added", expected_delegates["item_added"]) == expected_delegates["item_added"],
        "item_removed": require_int(result, "item_removed", expected_delegates["item_removed"]) == expected_delegates["item_removed"],
        "currency_changed": require_int(result, "currency_changed", expected_delegates["currency_changed"]) == expected_delegates["currency_changed"],
        "recall_count": require_int(result, "recall_count", 2 if expect_recall else 0) == (2 if expect_recall else 0),
        "weight_bits": bool(re.fullmatch(r"(?:0x)?[0-9A-Fa-f]{8}", require_field(result, "weight_bits"))),
        "max_slots": require_int(result, "max_slots") > 0,
        "max_weight": require_int(result, "max_weight") > 0,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("Native fixture field audit failed: " + ", ".join(failed))
    result["checks"] = checks
    return result


def hash_document(intent, manifest):
    return {
        "intent": str(prop(intent, "IntentHash")).upper(),
        "manifest": str(prop(manifest, "ManifestHash")).upper(),
        "topology": str(prop(manifest, "AnchorTopologyHash")).upper(),
        "population": str(prop(manifest, "PopulationHash")).upper(),
        "resource": str(prop(manifest, "ResourceHash")).upper(),
        "companion": str(prop(manifest, "CompanionSnapshotHash")).upper(),
    }


class State:
    def __init__(self):
        self.phase = "load_map"
        self.started = time.monotonic()
        self.phase_started = self.started
        self.callback = None
        self.finished = False
        self.director_class = None
        self.fixture_class = None
        self.door_class = None
        self.dungeon_class = None
        self.anchor_class = None
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_states()
        self.initial_protected = protected_hashes()
        self.policy_sha_before = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
        self.policy = {}
        self.evidence = {}
        self.initial_epoch = 0
        self.initial_hashes = {}
        self.frozen_document = ""
        self.frozen_hash = ""
        self.fixture_baseline = {}
        self.last_world_time = 0.0
        self.saw_generation_transition = False


STATE = State()


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("CALYSTO_V4_INVENTORY_TRAVEL phase={}".format(value))


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
    document = {
        "schema_version": 4,
        "generator_version": 4,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "success": bool(success),
        "status": "PASS" if success else "FAIL",
        "phase": STATE.phase,
        "error": error,
        "run_seed": RUN_SEED,
        "scenario": SCENARIO,
        "forced_dungeon_edge": FORCED_EDGE,
        "generation_count": 5,
        "policy": STATE.policy,
        "frozen_fixture_hash": STATE.frozen_hash,
        "frozen_fixture_document": STATE.frozen_document,
        "inventory_travel": STATE.evidence,
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
    unreal.log("CALYSTO_V4_INVENTORY_TRAVEL_RESULT " + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v4_inventory_travel_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message):
    finish(False, message)


def runtime_objects(world):
    return (
        find_subsystem(world, STATE.director_class),
        find_subsystem(world, STATE.fixture_class),
    )


def director_is_ready(director):
    snapshot = reflected(director, "get_snapshot")()
    return state_contains(prop(snapshot, "State"), "ready") and bool(prop(snapshot, "bDoorReady"))


def state_contains(value, token):
    return token.lower() in str(value).lower().replace("_", "")


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


def audit_floor(world, director, expected_floor, expected_serial, expected_epoch):
    snapshot = reflected(director, "get_snapshot")()
    intent = reflected(director, "get_resolved_floor_intent")()
    manifest = reflected(director, "get_realized_floor_manifest")()
    hashes = hash_document(intent, manifest)
    size = prop(intent, "DungeonSize")
    doors = actors_of_class(world, STATE.door_class)
    dungeons = actors_of_class(world, STATE.dungeon_class)
    anchors = actors_of_class(world, STATE.anchor_class)
    population = actors_with_tag(world, POPULATION_TAG)
    door_enabled = False
    if len(doors) == 1:
        for field in ("bIsEnabled", "IsEnabled"):
            try:
                door_enabled = bool(prop(doors[0], field))
                break
            except Exception:
                pass
    current_epoch = int(reflected(director, "get_run_epoch")())
    checks = {
        "director_ready": director_is_ready(director),
        "snapshot_identity": int(prop(snapshot, "FloorNumber")) == expected_floor and int(prop(snapshot, "GenerationSerial")) == expected_serial and int(prop(snapshot, "RunEpoch")) == expected_epoch,
        "director_epoch": current_epoch == expected_epoch,
        "intent_identity": bool(prop(intent, "bIsValid")) and int(prop(intent, "RunSeed")) == RUN_SEED and int(prop(intent, "FloorNumber")) == expected_floor and int(prop(intent, "GenerationSerial")) == expected_serial,
        "manifest_valid": bool(prop(manifest, "bIsValid")),
        "forced_edge": int(prop(size, "X")) == FORCED_EDGE and int(prop(size, "Y")) == FORCED_EDGE and int(prop(size, "Z")) == 1,
        "scenario": SCENARIO.lower() in str(prop(intent, "DevelopmentPopulationScenario")).lower(),
        "zero_population": int(prop(manifest, "SpawnedActorCount")) == 0 and len(population) == 0,
        "companion_ready": bool(prop(snapshot, "bCompanionReady")),
        "door": len(doors) == 1 and door_enabled,
        "dungeon": len(dungeons) == 1,
        "anchors_destroyed": len(anchors) == 0,
        "hashes": all(is_sha256(value) for value in hashes.values()),
        "hash_identity": hashes["intent"] == str(prop(manifest, "IntentHash")).upper() and hashes["companion"] == str(prop(intent, "CompanionSnapshotHash")).upper() == str(prop(snapshot, "CompanionSnapshotHash")).upper(),
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("Floor audit failed: " + ", ".join(failed))
    return {
        "run_epoch": current_epoch,
        "floor": expected_floor,
        "serial": expected_serial,
        "hashes": hashes,
        "checks": checks,
    }


def stable_fixture_getters(fixture):
    document = str(reflected(fixture, "get_frozen_inventory_fixture_document_for_automation")())
    fixture_hash = str(reflected(fixture, "get_frozen_inventory_fixture_hash_for_automation")()).upper()
    if not document or not is_sha256(fixture_hash):
        raise RuntimeError("The armed inventory fixture has no canonical document/hash")
    return document, fixture_hash


def arm_fixture_checks(result, epoch, document, fixture_hash):
    document_match = re.match(
        r"^ProjectV4InventoryFixture\|currency=([0-9A-Fa-f]{8})\|weight=([0-9A-Fa-f]{8})\|maxSlots=(\d+)\|maxWeight=(\d+)\|items=[\s\S]+$",
        document,
    )
    if not document_match:
        raise RuntimeError("The frozen fixture document has an invalid canonical shape")
    checks = {
        "armed": require_int(result, "armed", 1) == 1,
        "run_epoch": require_int(result, "run_epoch", epoch) == epoch,
        "floor": require_int(result, "floor", 1) == 1,
        "hash": require_field(result, "hash").upper() == fixture_hash,
        "items": require_int(result, "items", 4) == 4,
        "arrow": require_int(result, "arrow", 7) == 7,
        "recall": require_int(result, "recall", 2) == 2,
        "block_fragment": "block" in require_field(result, "block_fragment").lower(),
        "currency": document_match.group(1).upper() == FIXTURE_CURRENCY_BITS,
        "max_slots": require_int(result, "max_slots") == int(document_match.group(3)) == FIXTURE_MAX_SLOTS,
        "max_weight": require_int(result, "max_weight") == int(document_match.group(4)) == FIXTURE_MAX_WEIGHT,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("Arm fixture field audit failed: " + ", ".join(failed))
    result["checks"] = checks
    return {
        "weight_bits": document_match.group(2).upper(),
        "max_slots": int(document_match.group(3)),
        "max_weight": int(document_match.group(4)),
    }


def audit_fixture(fixture, kind, epoch, floor, expect_recall, expected_delegates):
    result = parse_native_result(kind + " inventory audit", reflected(fixture, "audit_inventory_travel_fixture_for_automation")())
    fixture_result_checks(result, kind, epoch, floor, expect_recall, expected_delegates)
    if not bool(reflected(fixture, "is_inventory_travel_fixture_armed_for_automation")()):
        raise RuntimeError("The inventory fixture disarmed unexpectedly during " + kind)
    document, fixture_hash = stable_fixture_getters(fixture)
    if document != STATE.frozen_document or fixture_hash != STATE.frozen_hash:
        raise RuntimeError("The frozen fixture baseline changed during " + kind)
    if kind != "NewRun" and require_field(result, "hash").upper() != STATE.frozen_hash:
        raise RuntimeError(kind + " did not retain the exact frozen inventory hash")
    baseline = STATE.fixture_baseline
    if require_int(result, "max_slots") != baseline["max_slots"] or require_int(result, "max_weight") != baseline["max_weight"]:
        raise RuntimeError(kind + " changed inventory capacity")
    if kind != "NewRun" and require_field(result, "weight_bits").upper() != baseline["weight_bits"]:
        raise RuntimeError(kind + " changed the exact inventory weight bits")
    if kind == "NewRun" and require_field(result, "hash").upper() == STATE.frozen_hash:
        raise RuntimeError("NewRun did not produce the expected Recall-free inventory hash")
    return result


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
                raise RuntimeError("The inventory travel gate requires a positive seed and the authored V4 policy")
            STATE.policy = native_policy_document(unreal.load_asset(POLICY_OBJECT))
            STATE.director_class = unreal.load_class(None, DIRECTOR_CLASS)
            STATE.fixture_class = unreal.load_class(None, FIXTURE_CLASS)
            STATE.door_class = unreal.load_class(None, DOOR_CLASS)
            STATE.dungeon_class = unreal.load_class(None, DUNGEON_CLASS)
            STATE.anchor_class = unreal.load_class(None, ANCHOR_CLASS)
            if not all((STATE.director_class, STATE.fixture_class, STATE.door_class, STATE.dungeon_class, STATE.anchor_class)):
                raise RuntimeError("A required inventory travel runtime class failed to load")
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
            director, fixture = runtime_objects(world)
            if not director or not fixture or float(unreal.GameplayStatics.get_time_seconds(world)) < 1.0:
                return
            unreal.SystemLibrary.execute_console_command(world, "EF.Calysto.Automation.SetPopulationScenario " + SCENARIO)
            unreal.SystemLibrary.execute_console_command(world, "EF.Calysto.Automation.ForceDungeonEdge {}".format(FORCED_EDGE))
            set_phase("wait_initial_ready")
            if not reflected(director, "request_start_new_run_with_seed")(RUN_SEED):
                raise RuntimeError("Initial New Run request was rejected")
            return

        director, fixture = runtime_objects(world) if world else (None, None)
        if STATE.phase in ("wait_replay", "wait_reroll", "wait_advance", "wait_new_run"):
            mark_transition(world, director)
        if not world or canonical_world(world).lower() != DUNGEON_WORLD.lower() or not director or not fixture:
            return

        if STATE.phase == "wait_initial_ready":
            if not director_is_ready(director) or float(unreal.GameplayStatics.get_time_seconds(world)) < 3.0:
                return
            epoch = int(reflected(director, "get_run_epoch")())
            if epoch <= 0:
                raise RuntimeError("Initial New Run did not establish a positive RunEpoch")
            floor = audit_floor(world, director, 1, 1, epoch)
            arm = parse_native_result("Arm inventory fixture", reflected(fixture, "arm_inventory_travel_fixture_for_automation")())
            if not bool(reflected(fixture, "is_inventory_travel_fixture_armed_for_automation")()):
                raise RuntimeError("Arm returned PASS but the fixture is not armed")
            STATE.frozen_document, STATE.frozen_hash = stable_fixture_getters(fixture)
            STATE.fixture_baseline = arm_fixture_checks(
                arm, epoch, STATE.frozen_document, STATE.frozen_hash)
            STATE.initial_epoch = epoch
            STATE.initial_hashes = floor["hashes"]
            STATE.evidence["initial_arm"] = {"floor": floor, "fixture": arm}
            begin_travel(world, "wait_replay", lambda: reflected(director, "request_replay_current_floor")(), "Replay was rejected")
            return

        if STATE.phase == "wait_replay":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            floor = audit_floor(world, director, 1, 1, STATE.initial_epoch)
            if floor["hashes"] != STATE.initial_hashes:
                raise RuntimeError("Replay did not reproduce the exact Floor 1 hashes")
            audit = audit_fixture(fixture, "Replay", STATE.initial_epoch, 1, True, {"inventory_changed": 1, "item_added": 0, "item_removed": 0, "currency_changed": 1})
            STATE.evidence["replay"] = {"floor": floor, "fixture": audit}
            begin_travel(world, "wait_reroll", lambda: reflected(director, "request_reroll_current_floor")(), "Reroll was rejected")
            return

        if STATE.phase == "wait_reroll":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            floor = audit_floor(world, director, 1, 2, STATE.initial_epoch)
            if floor["hashes"]["intent"] == STATE.initial_hashes["intent"] or floor["hashes"]["manifest"] == STATE.initial_hashes["manifest"]:
                raise RuntimeError("Reroll did not change both intent and manifest hashes")
            audit = audit_fixture(fixture, "Reroll", STATE.initial_epoch, 1, True, {"inventory_changed": 1, "item_added": 0, "item_removed": 0, "currency_changed": 1})
            STATE.evidence["reroll"] = {"floor": floor, "fixture": audit}
            begin_travel(world, "wait_advance", lambda: reflected(director, "request_advance_floor")(), "Advance was rejected")
            return

        if STATE.phase == "wait_advance":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            floor = audit_floor(world, director, 2, 3, STATE.initial_epoch)
            audit = audit_fixture(fixture, "Advance", STATE.initial_epoch, 2, True, {"inventory_changed": 1, "item_added": 0, "item_removed": 0, "currency_changed": 1})
            STATE.evidence["advance"] = {"floor": floor, "fixture": audit}
            begin_travel(world, "wait_new_run", lambda: reflected(director, "request_start_new_run_with_seed")(RUN_SEED), "Same-seed New Run was rejected")
            return

        if STATE.phase == "wait_new_run":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            new_epoch = STATE.initial_epoch + 1
            floor = audit_floor(world, director, 1, 1, new_epoch)
            if floor["hashes"] != STATE.initial_hashes:
                raise RuntimeError("Same-seed New Run did not reproduce the exact Floor 1 hashes")
            audit = audit_fixture(fixture, "NewRun", new_epoch, 1, False, {"inventory_changed": 3, "item_added": 0, "item_removed": 2, "currency_changed": 1})
            if require_field(audit, "weight_bits").upper() == STATE.fixture_baseline["weight_bits"]:
                raise RuntimeError("NewRun did not remove the two Recall item weights")
            STATE.evidence["new_run"] = {"floor": floor, "fixture": audit, "previous_epoch": STATE.initial_epoch, "new_epoch": new_epoch}
            finish(True)
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v4_inventory_travel_pie58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v4_inventory_travel_pie58 = STATE
unreal.log("CALYSTO_V4_INVENTORY_TRAVEL validator_registered=true")
