"""Exercise the real Dungeon Director V4 companion lifecycle in UE 5.8 PIE.

The test deliberately uses ACF's public group API for recruitment and
AACFCharacter.KillCharacter for death.  It never edits the roster directly and
never saves Content.  One GameInstance is kept across all OpenLevel travels.
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
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_OUTPUT"]).resolve()
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_SEED", "202608210404"))

CONTROL_MAP = "/Game/_Game/Hub/HUB"
CONTROL_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_OBJECT = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4"
POLICY_FILE = CONTENT_DIR / "_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
ROSTER_CLASS = "/Script/EFProjectSystemsGameplay.ProjectRunCompanionSubsystem"
GROUP_CLASS = "/Script/AIFramework.ACFCompanionGroupAIComponent"
DOOR_CLASS = "/Script/EFProceduralACFURuntime.EFCalystoFloorDoor"
DUNGEON_CLASS = "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C"
ANCHOR_CLASS = "/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor"
RECRUITMENT_CLASS = "/Script/EFProjectSystemsGameplay.ProjectRecruitableCompanionComponent"
DEATH_PROXY_CLASS = "/Script/EFProjectSystemsGameplay.ProjectCompanionDeathProxyComponent"
NPC_TAG = "EF.Calysto.V4.Category.NPC"
POPULATION_TAG = "EF.Calysto.Population.V4"
EXPECTED_CATALOG = "NPC.Companion.Generalist.Female"
EXPECTED_CLASS = "/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C"
SCENARIO = "NPCGeneralistFemale"
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


def player_group(world):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    controller = reflected(pawn, "get_controller")() if pawn else None
    group = get_component(controller, STATE.group_class)
    if not group:
        raise RuntimeError("The typed ACF companion group is unavailable")
    return group


def group_size(group):
    return int(reflected(group, "get_group_size")())


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
    entries = list(prop(snapshot, "Entries"))
    active = list(prop(snapshot, "ActiveParty"))
    result_entries = []
    for entry in entries:
        definition = prop(entry, "Definition")
        result_entries.append(
            {
                "stable_id": guid_text(prop(definition, "StableCompanionId")),
                "content_id": str(prop(definition, "ContentId")),
                "character_class": str(prop(definition, "CharacterClass")),
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
        "active_party": sorted(guid_text(value) for value in active),
    }


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
        self.classes = {}
        self.director_class = None
        self.roster_class = None
        self.group_class = None
        self.door_class = None
        self.dungeon_class = None
        self.anchor_class = None
        self.recruitment_class = None
        self.death_proxy_class = None
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_states()
        self.initial_protected = protected_hashes()
        self.policy_sha_before = sha256(POLICY_FILE) if POLICY_FILE.is_file() else None
        self.policy = {}
        self.evidence = {}
        self.stable_id = ""
        self.definition_identity = {}
        self.initial_hashes = {}
        self.floor2_hashes = {}
        self.initial_epoch = 0
        self.last_world_time = 0.0
        self.saw_generation_transition = False


STATE = State()


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("CALYSTO_V4_COMPANION_LIFECYCLE phase={}".format(value))


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
        "policy": STATE.policy,
        "lifecycle": STATE.evidence,
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
    unreal.log("CALYSTO_V4_COMPANION_LIFECYCLE_RESULT " + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v4_companion_lifecycle_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message):
    finish(False, message)


def runtime_objects(world):
    director = find_subsystem(world, STATE.director_class)
    roster = find_subsystem(world, STATE.roster_class)
    if not director or not roster:
        return None, None
    return director, roster


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


def audit_floor(world, director, roster, expected_floor, expected_serial, expected_npcs, expected_state, projected):
    snapshot = reflected(director, "get_snapshot")()
    intent = reflected(director, "get_resolved_floor_intent")()
    manifest = reflected(director, "get_realized_floor_manifest")()
    roster_doc = roster_document(roster)
    group = player_group(world)
    live_npcs = actors_with_tag(world, NPC_TAG)
    doors = actors_of_class(world, STATE.door_class)
    dungeons = actors_of_class(world, STATE.dungeon_class)
    anchors = actors_of_class(world, STATE.anchor_class)
    population = actors_with_tag(world, POPULATION_TAG)
    hashes = hash_document(intent, manifest)
    size = prop(intent, "DungeonSize")
    door_enabled = False
    if len(doors) == 1:
        for field in ("bIsEnabled", "IsEnabled"):
            try:
                door_enabled = bool(prop(doors[0], field))
                break
            except Exception:
                pass
    checks = {
        "director_ready": director_is_ready(director),
        "manifest_valid": bool(prop(manifest, "bIsValid")),
        "intent_valid": bool(prop(intent, "bIsValid")),
        "identity": int(prop(intent, "RunSeed")) == RUN_SEED and int(prop(intent, "FloorNumber")) == expected_floor and int(prop(intent, "GenerationSerial")) == expected_serial,
        "snapshot_identity": int(prop(snapshot, "FloorNumber")) == expected_floor and int(prop(snapshot, "GenerationSerial")) == expected_serial,
        "forced_edge": int(prop(size, "X")) == FORCED_EDGE and int(prop(size, "Y")) == FORCED_EDGE and int(prop(size, "Z")) == 1,
        "scenario": SCENARIO.lower() in str(prop(intent, "DevelopmentPopulationScenario")).lower(),
        "npc_count": int(prop(manifest, "NPCCount")) == expected_npcs and len(live_npcs) == expected_npcs,
        "roster_ready": bool(prop(snapshot, "bCompanionReady")) and bool(reflected(roster, "is_companion_roster_ready")()),
        "group_size": group_size(group) == (1 if projected else 0),
        "door": len(doors) == 1 and door_enabled,
        "dungeon": len(dungeons) == 1,
        "anchors_destroyed": len(anchors) == 0,
        "population_count": len(population) == int(prop(manifest, "SpawnedActorCount")),
        "hashes": all(is_sha256(value) for value in hashes.values()),
        "hash_identity": hashes["intent"] == str(prop(manifest, "IntentHash")).upper() and hashes["companion"] == str(prop(intent, "CompanionSnapshotHash")).upper() == str(prop(snapshot, "CompanionSnapshotHash")).upper(),
    }
    if expected_state == "empty":
        checks.update({"roster_empty": len(roster_doc["entries"]) == 0 and len(roster_doc["active_party"]) == 0, "not_confirmed_dead": not bool(reflected(roster, "has_confirmed_dead_companion")()), "no_revival_candidates": len(list(reflected(roster, "get_revival_candidates")())) == 0})
    else:
        entry = roster_doc["entries"][0] if len(roster_doc["entries"]) == 1 else {}
        checks.update(
            {
                "one_stable_entry": len(roster_doc["entries"]) == 1 and entry.get("stable_id") == STATE.stable_id,
                "definition_preserved": {key: entry.get(key) for key in STATE.definition_identity} == STATE.definition_identity,
                "state": state_contains(entry.get("state", ""), expected_state),
                "active_party": roster_doc["active_party"] == ([STATE.stable_id] if expected_state == "alive" else []),
            }
        )
    if expected_npcs == 1:
        actor = live_npcs[0]
        hook = get_component(actor, STATE.recruitment_class)
        proxy = get_component(actor, STATE.death_proxy_class)
        checks.update(
            {
                "npc_class": object_path(actor.get_class()) == EXPECTED_CLASS,
                "npc_catalog": any(str(prop(instance, "CatalogId")) == EXPECTED_CATALOG for instance in list(prop(manifest, "Instances")) if state_contains(prop(instance, "Category"), "npc")),
                "local_or_projected_hook": (hook is None) if projected else (hook is not None),
                "local_or_projected_proxy": (proxy is not None) if projected else (proxy is None),
                "group_contains_actor": bool(reflected(group, "is_already_in_group")(actor)) == projected,
            }
        )
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("Floor audit failed: " + ", ".join(failed))
    return {"floor": expected_floor, "serial": expected_serial, "hashes": hashes, "roster": roster_doc, "checks": checks}


def pending_death_ready(roster, floor, serial):
    doc = roster_document(roster)
    if len(doc["entries"]) != 1 or not state_contains(doc["entries"][0]["state"], "pendingdead"):
        return None
    entry = doc["entries"][0]
    group = player_group(game_world())
    candidates = list(reflected(roster, "get_revival_candidates")())
    checks = {
        "same_id": entry["stable_id"] == STATE.stable_id,
        "death_identity": entry["death_floor"] == floor and entry["death_serial"] == serial,
        "not_active": doc["active_party"] == [],
        "group_empty": group_size(group) == 0,
        "not_confirmed": not bool(reflected(roster, "has_confirmed_dead_companion")()),
        "one_pending_candidate": len(candidates) == 1 and bool(prop(candidates[0], "bDeathPendingAdvance")),
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("Pending-death audit failed: " + ", ".join(failed))
    return {"floor": floor, "serial": serial, "roster": doc, "checks": checks}


def request_kill(world, roster, wait_phase):
    live_npcs = actors_with_tag(world, NPC_TAG)
    if len(live_npcs) != 1:
        raise RuntimeError("Kill phase requires exactly one live projected NPC")
    if group_size(player_group(world)) != 1:
        raise RuntimeError("Kill phase requires the projected NPC in the real ACF group")
    set_phase(wait_phase)
    reflected(live_npcs[0], "kill_character")()


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
                raise RuntimeError("The lifecycle gate requires a positive seed and the authored V4 policy")
            STATE.policy = native_policy_document(unreal.load_asset(POLICY_OBJECT))
            STATE.director_class = unreal.load_class(None, DIRECTOR_CLASS)
            STATE.roster_class = unreal.load_class(None, ROSTER_CLASS)
            STATE.group_class = unreal.load_class(None, GROUP_CLASS)
            STATE.door_class = unreal.load_class(None, DOOR_CLASS)
            STATE.dungeon_class = unreal.load_class(None, DUNGEON_CLASS)
            STATE.anchor_class = unreal.load_class(None, ANCHOR_CLASS)
            STATE.recruitment_class = unreal.load_class(None, RECRUITMENT_CLASS)
            STATE.death_proxy_class = unreal.load_class(None, DEATH_PROXY_CLASS)
            if not all((STATE.director_class, STATE.roster_class, STATE.group_class, STATE.door_class, STATE.dungeon_class, STATE.anchor_class, STATE.recruitment_class, STATE.death_proxy_class)):
                raise RuntimeError("A required lifecycle runtime class failed to load")
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
            set_phase("wait_initial_ready")
            if not reflected(director, "request_start_new_run_with_seed")(RUN_SEED):
                raise RuntimeError("Initial New Run request was rejected")
            return

        director, roster = runtime_objects(world) if world else (None, None)
        if STATE.phase.startswith("wait_") and STATE.phase not in ("wait_initial_ready", "wait_recruited", "wait_pending_a", "wait_pending_b", "wait_pending_c", "wait_control_pie", "wait_editor_map"):
            mark_transition(world, director)
        if not world or canonical_world(world).lower() != DUNGEON_WORLD.lower() or not director or not roster:
            return

        if STATE.phase == "wait_initial_ready":
            if not director_is_ready(director) or float(unreal.GameplayStatics.get_time_seconds(world)) < 3.0:
                return
            initial = audit_floor(world, director, roster, 1, 1, 1, "empty", False)
            STATE.evidence["initial"] = initial
            STATE.initial_hashes = initial["hashes"]
            STATE.initial_epoch = int(reflected(roster, "get_run_epoch")())
            local_npc = actors_with_tag(world, NPC_TAG)[0]
            group = player_group(world)
            set_phase("wait_recruited")
            if not reflected(group, "add_existing_character_to_group")(local_npc):
                raise RuntimeError("ACF AddExistingCharacterToGroup rejected the local companion")
            return

        if STATE.phase == "wait_recruited":
            doc = roster_document(roster)
            if len(doc["entries"]) != 1 or len(doc["active_party"]) != 1:
                return
            entry = doc["entries"][0]
            if not state_contains(entry["state"], "alive"):
                raise RuntimeError("The ACF recruitment observer did not create an Alive roster entry")
            STATE.stable_id = entry["stable_id"]
            STATE.definition_identity = {key: entry[key] for key in ("stable_id", "content_id", "archetype", "gender", "grade", "lifecycle")}
            checks = {
                "stable_id": len(STATE.stable_id) == 32,
                "catalog": entry["content_id"] == EXPECTED_CATALOG,
                "recruitable": state_contains(entry["lifecycle"], "recruit"),
                "group": group_size(player_group(world)) == 1,
                "not_confirmed_dead": not bool(reflected(roster, "has_confirmed_dead_companion")()),
            }
            failed = sorted(name for name, passed in checks.items() if not passed)
            if failed:
                raise RuntimeError("Recruitment audit failed: " + ", ".join(failed))
            STATE.evidence["recruited"] = {"roster": doc, "checks": checks}
            begin_travel(world, "wait_floor2_advance", lambda: reflected(director, "request_advance_floor")(), "Advance to Floor 2 was rejected")
            return

        if STATE.phase == "wait_floor2_advance":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            floor2 = audit_floor(world, director, roster, 2, 2, 1, "alive", True)
            STATE.evidence["floor2_advance"] = floor2
            STATE.floor2_hashes = floor2["hashes"]
            request_kill(world, roster, "wait_pending_a")
            return

        if STATE.phase == "wait_pending_a":
            pending = pending_death_ready(roster, 2, 2)
            if not pending:
                return
            STATE.evidence["pending_a"] = pending
            begin_travel(world, "wait_floor2_replay", lambda: reflected(director, "request_replay_current_floor")(), "Replay was rejected")
            return

        if STATE.phase == "wait_floor2_replay":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            replay = audit_floor(world, director, roster, 2, 2, 1, "alive", True)
            if replay["hashes"] != STATE.floor2_hashes:
                raise RuntimeError("Replay did not reproduce the exact Floor 2 intent and manifest hashes")
            STATE.evidence["floor2_replay"] = replay
            request_kill(world, roster, "wait_pending_b")
            return

        if STATE.phase == "wait_pending_b":
            pending = pending_death_ready(roster, 2, 2)
            if not pending:
                return
            STATE.evidence["pending_b"] = pending
            begin_travel(world, "wait_floor2_reroll", lambda: reflected(director, "request_reroll_current_floor")(), "Reroll was rejected")
            return

        if STATE.phase == "wait_floor2_reroll":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            reroll = audit_floor(world, director, roster, 2, 3, 1, "alive", True)
            if reroll["hashes"]["intent"] == STATE.floor2_hashes["intent"] or reroll["hashes"]["manifest"] == STATE.floor2_hashes["manifest"]:
                raise RuntimeError("Reroll did not change both intent and manifest hashes")
            STATE.evidence["floor2_reroll"] = reroll
            request_kill(world, roster, "wait_pending_c")
            return

        if STATE.phase == "wait_pending_c":
            pending = pending_death_ready(roster, 2, 3)
            if not pending:
                return
            STATE.evidence["pending_c"] = pending
            begin_travel(world, "wait_floor3_dead", lambda: reflected(director, "request_advance_floor")(), "Advance with PendingDead companion was rejected")
            return

        if STATE.phase == "wait_floor3_dead":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            dead = audit_floor(world, director, roster, 3, 4, 0, "dead", False)
            candidates = list(reflected(roster, "get_revival_candidates")())
            checks = {
                "confirmed_dead": bool(reflected(roster, "has_confirmed_dead_companion")()),
                "one_dead_candidate": len(candidates) == 1 and not bool(prop(candidates[0], "bDeathPendingAdvance")),
                "death_metadata_preserved": dead["roster"]["entries"][0]["death_floor"] == 2 and dead["roster"]["entries"][0]["death_serial"] == 3,
            }
            failed = sorted(name for name, passed in checks.items() if not passed)
            if failed:
                raise RuntimeError("Confirmed-death audit failed: " + ", ".join(failed))
            dead["confirmed_checks"] = checks
            STATE.evidence["floor3_dead"] = dead
            begin_travel(world, "wait_new_run_reproduction", lambda: reflected(director, "request_start_new_run_with_seed")(RUN_SEED), "Same-seed New Run was rejected")
            return

        if STATE.phase == "wait_new_run_reproduction":
            if not STATE.saw_generation_transition or not director_is_ready(director):
                return
            reproduced = audit_floor(world, director, roster, 1, 1, 1, "empty", False)
            new_epoch = int(reflected(roster, "get_run_epoch")())
            checks = {
                "new_epoch": new_epoch > STATE.initial_epoch,
                "same_seed_exact_reproduction": reproduced["hashes"] == STATE.initial_hashes,
                "group_empty": group_size(player_group(world)) == 0,
                "graveyard_empty": not bool(reflected(roster, "has_confirmed_dead_companion")()) and len(list(reflected(roster, "get_revival_candidates")())) == 0,
            }
            failed = sorted(name for name, passed in checks.items() if not passed)
            if failed:
                raise RuntimeError("New Run reproduction audit failed: " + ", ".join(failed))
            reproduced["new_run_checks"] = checks
            reproduced["previous_epoch"] = STATE.initial_epoch
            reproduced["new_epoch"] = new_epoch
            STATE.evidence["new_run_reproduction"] = reproduced
            finish(True)
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v4_companion_lifecycle_pie58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v4_companion_lifecycle_pie58 = STATE
unreal.log("CALYSTO_V4_COMPANION_LIFECYCLE validator_registered=true")
