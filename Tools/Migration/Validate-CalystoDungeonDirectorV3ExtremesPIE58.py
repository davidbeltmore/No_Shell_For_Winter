"""Materialize one exact Dungeon Director V3 Development acceptance scenario.

The scenario policy is a native transient clone armed through a Development-only
console command.  The authored Primary Data Asset and protected Calysto assets
remain read-only.  One editor process exercises exactly one scenario so no run
ecology or transient policy state can leak between cases.
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
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V3_EXTREME_OUTPUT"])
SCENARIO = os.environ["CODEX_CALYSTO_V3_EXTREME_SCENARIO"]
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V3_EXTREME_SEED", "202608143030"))

EXPECTED = {
    "Zero": (0, 0, 0),
    "EnemyCap25": (25, 0, 0),
    "ResourceMin": (0, 0, 0),
    "ResourceMax": (0, 8, 3),
}
if SCENARIO not in EXPECTED:
    raise RuntimeError("Unknown V3 extreme scenario: " + SCENARIO)

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
POPULATION_TAG = "EF.Calysto.Population.V3"
CATEGORY_TAGS = {
    "enemy": "EF.Calysto.Enemy",
    "food": "EF.Calysto.Food",
    "chest": "EF.Calysto.Chest",
    "loot": "EF.Calysto.Loot",
    "special": "EF.Calysto.SpecialEvent",
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
    "Content/FullSample/Player.uasset",
    "Content/DazToUnreal/Female/Female.uasset",
    "Content/DazToUnreal/Multiple/Multiple.uasset",
    "Content/DazToUnreal/Male/Male.uasset",
)

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
GLOBAL_TIMEOUT = 240.0
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
            result[object_name] = bool(asset.get_outermost().is_dirty()) if asset else None
        except Exception:
            result[object_name] = None
    return result


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
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_states()
        self.initial_protected = protected_hashes()
        self.authored_policy_hash = ""
        self.result = {}


STATE = State()


def set_phase(value):
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log("[CalystoV3ExtremesPIE58] phase={} scenario={}".format(value, SCENARIO))


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
        "schema_version": 3,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "success": bool(success),
        "status": "PASS" if success else "FAIL",
        "scenario": SCENARIO,
        "run_seed": RUN_SEED,
        "forced_dungeon_edge": 30,
        "phase": STATE.phase,
        "error": error,
        "authored_policy_hash": STATE.authored_policy_hash,
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
        "policy_sha256_before": STATE.initial_protected.get(
            "Content/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy.uasset",
            {},
        ).get("sha256"),
        "policy_sha256_after": policy_sha_after,
    }
    # The policy is outside PROTECTED_FILES but is monitored explicitly here.
    document["policy_sha256_before"] = getattr(STATE, "policy_sha_before", None)
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("[CalystoV3ExtremesPIE58] result=" + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v3_extremes_pie58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message):
    finish(False, message)


def validate_ready(world, subsystem):
    snapshot = reflected(subsystem, "get_snapshot")()
    generation_state = str(prop(snapshot, "GenerationState")).lower()
    if "ready" not in generation_state:
        return False

    intent = reflected(subsystem, "get_resolved_floor_intent")()
    manifest = reflected(subsystem, "get_realized_floor_manifest")()
    expected_enemy, expected_food, expected_chest = EXPECTED[SCENARIO]
    counts = {
        "enemy": int(prop(manifest, "EnemyCount")),
        "food": int(prop(manifest, "FoodCount")),
        "chest": int(prop(manifest, "ChestCount")),
        "loot": int(prop(manifest, "LootCount")),
        "special": int(prop(manifest, "SpecialEventCount")),
    }
    tag_counts = {name: len(actors_with_tag(world, tag)) for name, tag in CATEGORY_TAGS.items()}
    population_actors = actors_with_tag(world, POPULATION_TAG)
    doors = actors_of_class(world, STATE.door_class)
    dungeons = actors_of_class(world, STATE.dungeon_class)
    anchors = actors_of_class(world, STATE.anchor_class)
    size = prop(intent, "DungeonSize")
    policy_hash = str(prop(intent, "PolicyHash"))
    manifest_hash = str(prop(manifest, "ManifestHash"))
    spawned_actor_count = int(prop(manifest, "SpawnedActorCount"))
    expected_total = expected_enemy + expected_food + expected_chest
    door_enabled = False
    if len(doors) == 1:
        try:
            door_enabled = bool(prop(doors[0], "bIsEnabled"))
        except Exception:
            door_enabled = bool(prop(doors[0], "IsEnabled"))

    checks = {
        "manifest_valid": bool(prop(manifest, "bIsValid")),
        "intent_valid": bool(prop(intent, "bIsValid")),
        "scenario_policy_is_transient": bool(policy_hash and policy_hash != STATE.authored_policy_hash),
        "dungeon_size_is_certified_30": int(prop(size, "X")) == 30 and int(prop(size, "Y")) == 30 and int(prop(size, "Z")) == 1,
        "enemy_count_exact": counts["enemy"] == expected_enemy,
        "food_count_exact": counts["food"] == expected_food,
        "chest_count_exact": counts["chest"] == expected_chest,
        "unrelated_categories_zero": counts["loot"] == 0 and counts["special"] == 0,
        "manifest_actor_count_exact": spawned_actor_count == expected_total,
        "live_population_actor_count_exact": len(population_actors) == expected_total,
        "live_category_tags_exact": tag_counts == counts,
        "anchors_destroyed": len(anchors) == 0,
        "exactly_one_dungeon": len(dungeons) == 1,
        "exactly_one_enabled_door": len(doors) == 1 and door_enabled,
        "manifest_hash_present": len(manifest_hash) == 64,
        "policy_hash_present": len(policy_hash) == 64,
    }
    STATE.result = {
        "counts": counts,
        "tag_counts": tag_counts,
        "population_actor_count": len(population_actors),
        "spawned_actor_count": spawned_actor_count,
        "candidate_anchor_count": int(prop(manifest, "CandidateAnchorCount")),
        "dungeon_count": len(dungeons),
        "door_count": len(doors),
        "door_enabled": door_enabled,
        "anchor_count_after_materialization": len(anchors),
        "dungeon_size": [int(prop(size, "X")), int(prop(size, "Y")), int(prop(size, "Z"))],
        "policy_hash": policy_hash,
        "intent_hash": str(prop(intent, "IntentHash")),
        "manifest_hash": manifest_hash,
        "checks": checks,
    }
    failed = sorted(name for name, passed in checks.items() if not passed)
    if failed:
        raise RuntimeError("Extreme scenario checks failed: " + ", ".join(failed))
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
            if RUN_SEED <= 0 or not POLICY_FILE.is_file():
                fail("A positive seed and the authored V3 policy are required")
                return
            STATE.policy_sha_before = sha256(POLICY_FILE)
            policy = unreal.load_asset(POLICY_OBJECT)
            STATE.authored_policy_hash = str(reflected(policy, "get_policy_hash")())
            if len(STATE.authored_policy_hash) != 64:
                fail("The authored V3 policy did not expose a valid canonical hash")
                return
            STATE.subsystem_class = unreal.load_class(None, SUBSYSTEM_CLASS)
            STATE.door_class = unreal.load_class(None, DOOR_CLASS)
            STATE.dungeon_class = unreal.load_class(None, DUNGEON_CLASS)
            STATE.anchor_class = unreal.load_class(None, ANCHOR_CLASS)
            if not all((STATE.subsystem_class, STATE.door_class, STATE.dungeon_class, STATE.anchor_class)):
                fail("A required V3 class failed to load")
                return
            set_phase("loading_map")
            if LEVEL_EDITOR.load_level(CONTROL_MAP) is False:
                fail("HUB control map failed to load")
                return
            set_phase("wait_editor_map")
            return

        if STATE.phase == "wait_editor_map":
            editor_world = UNREAL_EDITOR.get_editor_world()
            if canonical_world(editor_world).lower() != CONTROL_WORLD.lower():
                if time.monotonic() - STATE.phase_started > PHASE_TIMEOUT:
                    fail("HUB editor world did not become ready")
                return
            if time.monotonic() - STATE.phase_started < 3.0:
                return
            LEVEL_EDITOR.editor_request_begin_play()
            set_phase("wait_control_pie")
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
            if not reflected(subsystem, "request_start_new_run_with_seed")(RUN_SEED):
                fail("RequestStartNewRunWithSeed rejected the armed scenario")
                return
            set_phase("wait_dungeon_ready")
            return

        if STATE.phase == "wait_dungeon_ready":
            world = game_world()
            if world and canonical_world(world).lower() == DUNGEON_WORLD.lower():
                subsystem = find_subsystem(world, STATE.subsystem_class)
                if subsystem and float(unreal.GameplayStatics.get_time_seconds(world)) >= 3.0:
                    if validate_ready(world, subsystem):
                        finish(True)
                        return
            if time.monotonic() - STATE.phase_started > PHASE_TIMEOUT:
                fail("The extreme scenario did not reach runtime readiness")
            return
    except Exception as exc:
        fail("{}\n{}".format(exc, traceback.format_exc()))


existing = getattr(builtins, "_codex_calysto_v3_extremes_pie58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v3_extremes_pie58 = STATE
unreal.log("[CalystoV3ExtremesPIE58] validator registered scenario=" + SCENARIO)
