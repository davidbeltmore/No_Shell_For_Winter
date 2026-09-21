"""Read-only UE 5.8 PIE traversal smoke for the definitive Calysto V6 entrance flow.

The harness starts PIE in HUB, selects and interacts with the real DoorToLevel
through the player's ACF interaction component, proves that DungeonGeneration
does not bounce back to HUB, then requests one fixed-seed V6 run and validates
the immutable floor, room Theme, material-precedence, and realized manifests.
No Content package is saved.
"""

from __future__ import annotations

import builtins
import datetime
import hashlib
import json
import os
import re
import time
import traceback
from pathlib import Path
from typing import Any

import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
PLUGIN_CONTENT_ROOT = (PROJECT_ROOT / "Plugins/EFProcedural/Content").resolve()
OUTPUT_FILE = Path(os.environ["CODEX_CALYSTO_V6_PIE_OUTPUT"]).resolve()
RUN_SEED = int(os.environ.get("CODEX_CALYSTO_V6_PIE_SEED", "202609040606"))
BLUEPRINT_SUMMARY_FILE = Path(
    os.environ.get("CODEX_CALYSTO_V6_BLUEPRINT_SUMMARY", "")
).resolve()

HUB_MAP = "/Game/_Game/Hub/HUB"
HUB_WORLD = "/Game/_Game/Hub/HUB.HUB"
DUNGEON_WORLD = "/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration"
POLICY_PACKAGE = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy"
POLICY_OBJECT = POLICY_PACKAGE + ".DA_CalystoDungeonDirectorPolicy"
POLICY_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"
POLICY_FILE = CONTENT_ROOT / "_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.uasset"
COOK_CLOSURE_RECEIPT = (
    PROJECT_ROOT / "Saved/Migration/CalystoDungeonDirectorV6/ValidateCookClosureV6.json"
)
DIRECTOR_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem"
ENTRANCE_CLASS = "/Game/Procedural/DoorToLevel.DoorToLevel_C"
INTERACTION_CLASS = "/Script/AscentCombatFramework.ACFInteractionComponent"
PROTECTED_ROOM_MASK = 1 | 2 | 4 | 8
EXPECTED_THEME_MATERIALS = {
    "NoTheme": "/Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles",
    "Forge": "/EFProcedural/Calysto/Internal/Materials/Architecture/MI_Template_BaseOrange.MI_Template_BaseOrange",
    "Shrine": "/Game/FullSample/DemoRoom/Materials/MI_Display_Blue.MI_Display_Blue",
}
_LEGACY_POLICY_ROOT = "/Game/_Game/Data/CalystoDungeon"
_LEGACY_DATA_ASSET = "DA_CalystoDungeonDirectorPolicy"
_LEGACY_TABLE = "DT_CalystoDungeonDirectorPolicy"
LEGACY_OBJECTS = tuple(
    f"{_LEGACY_POLICY_ROOT}/V{version}/{_LEGACY_DATA_ASSET}.{_LEGACY_DATA_ASSET}"
    for version in range(3, 6)
) + (
    f"{_LEGACY_POLICY_ROOT}/V{max(range(3, 6))}/{_LEGACY_TABLE}.{_LEGACY_TABLE}",
) + tuple(
    f"{package}V{2 + 2}.{package.rsplit('/', 1)[-1]}V{2 + 2}_C"
    for package in (
        "/Game/_Game/Items/Chests/BP_CalystoLockedChest",
        "/Game/_Game/Items/Chests/BP_CalystoLockPickChest",
        "/Game/_Game/Items/Clothing/BP_CalystoArmorPickup",
    )
)
PROTECTED_FILES = (
    "Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset",
    "Content/Calysto/Dungeon/Blueprint/Utility/BP_EndPoint.uasset",
    "Content/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine.uasset",
    "Content/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner.uasset",
    "Content/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster.uasset",
    "Content/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape.uasset",
    "Content/Procedural/Maps/DungeonGeneration.umap",
    "Content/Procedural/DoorToLevel.uasset",
    "Content/FullSample/Player.uasset",
    "Content/DazToUnreal/Female/Female.uasset",
    "Content/DazToUnreal/Male/Male.uasset",
    "Content/DazToUnreal/Multiple/Multiple.uasset",
    "Plugins/EFCharacterCreationDazBridge/EFCharacterCreationDazBridge.uplugin",
)
PACKAGE_SUFFIXES = (".uasset", ".uexp", ".ubulk", ".m.ubulk", ".uptnl")

LEVEL_EDITOR = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UNREAL_EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
GLOBAL_TIMEOUT = 600.0
PHASE_TIMEOUT = 180.0
READY_WORLD_TIME = 2.0
STABILITY_HOLD_SECONDS = 3.0
SELECTION_SAMPLES_REQUIRED = 3


def object_path(value: Any) -> str:
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def property_candidates(name: str) -> list[str]:
    first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    snake = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower()
    values = [name, snake, name[0].lower() + name[1:]]
    if len(name) > 1 and name[0] == "b" and name[1].isupper():
        stripped = name[1:]
        first = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", stripped)
        values.append(re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", first).lower())
    return list(dict.fromkeys(values))


def prop(owner: Any, name: str) -> Any:
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
    raise RuntimeError(f"Missing reflected property {name} on {object_path(owner)}")


def reflected(owner: Any, name: str, *args: Any) -> Any:
    method = getattr(owner, name, None)
    if not callable(method):
        raise RuntimeError(f"Missing reflected method {name} on {object_path(owner)}")
    return method(*args)


def normalized(value: Any) -> str:
    raw = getattr(value, "name", value)
    return re.sub(r"[^a-z0-9]", "", str(raw).split(".")[-1].casefold())


def canonical_world(world: Any) -> str:
    return re.sub(r"uedpie_\d+_", "", object_path(world), flags=re.IGNORECASE)


def game_world() -> Any:
    return EDITOR_LEVEL_LIBRARY.get_game_world()


def world_time(world: Any) -> float:
    return float(unreal.GameplayStatics.get_time_seconds(world)) if world else 0.0


def is_sha256(value: Any) -> bool:
    return bool(re.fullmatch(r"[0-9A-Fa-f]{64}", str(value)))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def package_base(package: str) -> Path:
    if package.startswith("/Game/"):
        return CONTENT_ROOT / package[len("/Game/") :]
    if package.startswith("/EFProcedural/"):
        return PLUGIN_CONTENT_ROOT / package[len("/EFProcedural/") :]
    raise RuntimeError(f"Unsupported V6 package mount: {package}")


def package_file(package: str) -> Path:
    return Path(str(package_base(package)) + ".uasset")


def validate_guarded_asset_hashes(closure: dict[str, Any]) -> None:
    expected = closure.get("guarded_asset_hashes", {})
    if not isinstance(expected, dict) or not expected:
        raise RuntimeError("V6 cook closure has no guarded asset inventory")
    drift: dict[str, dict[str, str]] = {}
    for package, expected_hash in expected.items():
        disk = package_file(str(package))
        actual = sha256(disk) if disk.is_file() else "MISSING"
        if actual != str(expected_hash).upper():
            drift[str(package)] = {
                "expected": str(expected_hash).upper(), "actual": actual
            }
    if drift:
        raise RuntimeError(f"V6 guarded assets drifted after cook closure: {drift}")


def validate_blueprint_package_evidence(
    report_contract: dict[str, Any], expected_count: int
) -> None:
    expected_list = [str(value) for value in report_contract.get("expected_blueprints", [])]
    compiled_list = [
        str(row.get("package", ""))
        for row in report_contract.get("compiled_blueprints", [])
    ]
    snapshots = report_contract.get("package_files_after", {})
    expected_set = set(expected_list)
    if (
        len(expected_list) != expected_count
        or len(expected_set) != expected_count
        or set(compiled_list) != expected_set
        or len(compiled_list) != expected_count
        or set(snapshots) != expected_set
        or "/Game/Procedural/DoorToLevel" not in expected_set
    ):
        raise RuntimeError("Blueprint compile evidence package cohort is not exact")
    for package in sorted(expected_set):
        recorded = snapshots[package]
        if set(recorded) != set(PACKAGE_SUFFIXES):
            raise RuntimeError(f"Blueprint snapshot suffix contract drifted: {package}")
        base = package_base(package)
        for suffix in PACKAGE_SUFFIXES:
            disk = Path(str(base) + suffix)
            expected_snapshot = recorded[suffix]
            expected_hash = (
                str(expected_snapshot.get("sha256", "")).upper()
                if isinstance(expected_snapshot, dict) else None
            )
            actual_hash = sha256(disk) if disk.is_file() else None
            if actual_hash != expected_hash:
                raise RuntimeError(
                    "Blueprint package changed after compile PASS: "
                    f"{package}{suffix} expected={expected_hash} actual={actual_hash}"
                )


def content_snapshot() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    for path in CONTENT_ROOT.rglob("*"):
        if path.is_file() and path.suffix.casefold() in {".uasset", ".umap", ".uexp", ".ubulk", ".uptnl"}:
            stat = path.stat()
            result[path.relative_to(CONTENT_ROOT).as_posix()] = (
                int(stat.st_size), int(stat.st_mtime_ns), sha256(path)
            )
    return result


def protected_hashes() -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for relative in PROTECTED_FILES:
        path = PROJECT_ROOT / relative
        result[relative] = {
            "exists": path.is_file(),
            "length": path.stat().st_size if path.is_file() else None,
            "sha256": sha256(path) if path.is_file() else None,
        }
    return result


def dirty_packages() -> list[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({object_path(value) for value in values})


def find_subsystem(world: Any, subsystem_class: Any) -> Any:
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
    prefix = object_path(game_instance)
    for candidate in unreal.ObjectIterator(unreal.Object):
        try:
            if candidate.get_class() == subsystem_class and object_path(candidate).startswith(prefix):
                return candidate
        except Exception:
            pass
    return None


def actors_of_class(world: Any, actor_class: Any) -> list[Any]:
    if not world or not actor_class:
        return []
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def components_of_class(actor: Any, component_class: Any) -> list[Any]:
    if not actor or not component_class:
        return []
    return list(actor.get_components_by_class(component_class))


def soft_path(value: Any) -> str:
    for method_name in ("to_soft_object_path", "get_asset_path_name"):
        method = getattr(value, method_name, None)
        if callable(method):
            try:
                return str(method())
            except Exception:
                pass
    raw = object_path(value)
    match = re.search(r"/(?:Game|EFProcedural)/[A-Za-z0-9_./]+", raw)
    return match.group(0) if match else raw


def material_paths(material_set: Any) -> dict[str, str]:
    return {
        surface: soft_path(prop(material_set, property_name))
        for surface, property_name in (
            ("floor", "FloorMaterial"),
            ("wall", "WallMaterial"),
            ("roof", "RoofMaterial"),
        )
    }


def policy_document() -> dict[str, Any]:
    policy_class = unreal.load_class(None, POLICY_CLASS)
    policy = unreal.load_asset(POLICY_PACKAGE)
    if not policy_class or not policy or policy.get_class() != policy_class:
        raise RuntimeError("Exact definitive V6 policy class/object is unavailable")
    validation = reflected(policy, "validate_policy")
    if isinstance(validation, bool) and not validation:
        raise RuntimeError("Native V6 policy validation failed")
    hashes = {
        "gameplay": str(reflected(policy, "get_gameplay_hash")).upper(),
        "authoring": str(reflected(policy, "get_authoring_hash")).upper(),
        "materials": str(reflected(policy, "get_material_hash")).upper(),
        "decals": str(reflected(policy, "get_decal_hash")).upper(),
    }
    if not all(is_sha256(value) for value in hashes.values()):
        raise RuntimeError(f"V6 policy returned invalid hashes: {hashes}")
    closure_raw = COOK_CLOSURE_RECEIPT.read_bytes()
    closure_hash = hashlib.sha256(closure_raw).hexdigest().upper()
    closure = json.loads(closure_raw.decode("utf-8"))
    if closure.get("status") != "PASS" or closure.get("policy", {}).get("object") != POLICY_OBJECT:
        raise RuntimeError("V6 cook-closure receipt is not current")
    if hashes != closure["policy"]["hashes"]:
        raise RuntimeError("Resident V6 policy hashes differ from cook-closure receipt")
    validate_guarded_asset_hashes(closure)
    allowed_summary_root = (
        PROJECT_ROOT / "Saved/Migration/CalystoDungeonDirectorV6"
    ).resolve()
    try:
        BLUEPRINT_SUMMARY_FILE.relative_to(allowed_summary_root)
    except ValueError:
        raise RuntimeError(
            f"Blueprint compile summary escapes V6 evidence root: {BLUEPRINT_SUMMARY_FILE}"
        )
    if (
        BLUEPRINT_SUMMARY_FILE.name != "StrictSummary.json"
        or BLUEPRINT_SUMMARY_FILE.parent.parent != allowed_summary_root
        or not BLUEPRINT_SUMMARY_FILE.parent.name.startswith("BlueprintCompile_")
        or not BLUEPRINT_SUMMARY_FILE.is_file()
    ):
        raise RuntimeError("Exact V6 Blueprint compile StrictSummary.json is required")
    blueprint_summary_raw = BLUEPRINT_SUMMARY_FILE.read_bytes()
    blueprint_summary_hash = hashlib.sha256(blueprint_summary_raw).hexdigest().upper()
    blueprint_summary = json.loads(blueprint_summary_raw.decode("utf-8"))
    report_contract = blueprint_summary.get("report_contract", {})
    expected_count = int(blueprint_summary.get("expected_blueprint_count", 0))
    if (
        blueprint_summary.get("status") != "PASS"
        or blueprint_summary.get("policy_hashes") != hashes
        or blueprint_summary.get("cook_closure_receipt_sha256")
        != closure_hash
        or int(blueprint_summary.get("compiled_blueprint_count", 0))
        != expected_count
        or expected_count < 30
        or not isinstance(report_contract, dict)
        or report_contract.get("status")
        != "UE58_CALYSTO_V6_BLUEPRINT_COMPILE_PASS"
        or int(report_contract.get("expected_blueprint_count", -1)) != expected_count
        or int(report_contract.get("compiled_blueprint_count", -1)) != expected_count
        or len(report_contract.get("expected_blueprints", [])) != expected_count
        or len(report_contract.get("compiled_blueprints", [])) != expected_count
        or report_contract.get("forbidden_compiled")
        or report_contract.get("on_disk_package_changes")
        or report_contract.get("protected_file_changes")
        or report_contract.get("dirty_not_in_cohort")
        or report_contract.get("saved_assets")
        or report_contract.get("failures")
        or int(report_contract.get("save_api_calls", -1)) != 0
    ):
        raise RuntimeError("Blueprint compile summary is stale or incomplete")
    validate_blueprint_package_evidence(report_contract, expected_count)
    return {
        "object": object_path(policy),
        "class": object_path(policy.get_class()),
        "hashes": hashes,
        "package_sha256": sha256(POLICY_FILE),
        "blueprint_compile_summary": str(BLUEPRINT_SUMMARY_FILE),
        "blueprint_compile_summary_sha256": blueprint_summary_hash,
    }


def legacy_objects_loaded() -> list[str]:
    loaded: list[str] = []
    for path in LEGACY_OBJECTS:
        try:
            if unreal.find_object(None, path) is not None:
                loaded.append(path)
        except Exception:
            pass
    return sorted(loaded)


class State:
    def __init__(self) -> None:
        self.phase = "load_map"
        self.started = time.monotonic()
        self.phase_started = self.started
        self.callback: Any = None
        self.finished = False
        self.classes: dict[str, Any] = {}
        self.policy: dict[str, Any] = {}
        self.samples: list[dict[str, Any]] = []
        self.operations: list[dict[str, Any]] = []
        self.entry_selected_samples = 0
        self.entry_positioned = False
        self.entry_interacted = False
        self.saw_dungeon_world = False
        self.seeded_run_requested = False
        self.stability_started = 0.0
        self.initial_content = content_snapshot()
        self.initial_dirty = dirty_packages()
        self.initial_protected = protected_hashes()


STATE = State()


def set_phase(value: str) -> None:
    STATE.phase = value
    STATE.phase_started = time.monotonic()
    unreal.log(f"CALYSTO_V6_PIE_SMOKE phase={value}")


def finish(success: bool, error: str = "") -> None:
    if STATE.finished:
        return
    STATE.finished = True
    after_content = content_snapshot()
    after_dirty = dirty_packages()
    after_protected = protected_hashes()
    changed_content = sorted(
        path
        for path in set(STATE.initial_content) | set(after_content)
        if STATE.initial_content.get(path) != after_content.get(path)
    )
    protected_mismatches = sorted(
        path
        for path in STATE.initial_protected
        if STATE.initial_protected[path] != after_protected.get(path)
    )
    newly_dirty = sorted(set(after_dirty) - set(STATE.initial_dirty))
    loaded_legacy = legacy_objects_loaded()
    final_checks = {
        "exact_two_ready_samples": [sample["label"] for sample in STATE.samples]
        == ["door_entry_probe", "seeded_floor_1"],
        "real_door_to_level_selected": STATE.entry_selected_samples >= SELECTION_SAMPLES_REQUIRED,
        "real_door_to_level_interacted": STATE.entry_interacted,
        "dungeon_world_observed": STATE.saw_dungeon_world,
        "seeded_run_requested": STATE.seeded_run_requested,
        "remained_in_dungeon_after_readiness": STATE.stability_started > 0.0,
        "content_not_saved": not changed_content,
        "no_new_dirty_packages": not newly_dirty,
        "protected_hashes_stable": not protected_mismatches,
        "legacy_calysto_objects_not_loaded": not loaded_legacy,
    }
    if success and not all(final_checks.values()):
        success = False
        error = "Final V6 PIE checks failed: " + ", ".join(
            key for key, value in final_checks.items() if not value
        )
    document = {
        "receipt_schema_version": 1,
        "schema_version": 6,
        "generator_version": 6,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "success": bool(success),
        "status": "PASS" if success else "FAIL",
        "phase": STATE.phase,
        "error": error,
        "run_seed": RUN_SEED,
        "policy": STATE.policy,
        "samples": STATE.samples,
        "operations": STATE.operations,
        "entry_selection_samples": STATE.entry_selected_samples,
        "final_checks": final_checks,
        "asset_saves": changed_content,
        "newly_dirty_packages": newly_dirty,
        "protected_assets": {
            "before": STATE.initial_protected,
            "after": after_protected,
            "mismatches": protected_mismatches,
        },
        "legacy_objects_loaded": loaded_legacy,
    }
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_FILE.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    unreal.log("CALYSTO_V6_PIE_SMOKE_RESULT=" + json.dumps(document, sort_keys=True))
    if STATE.callback is not None:
        try:
            unreal.unregister_slate_post_tick_callback(STATE.callback)
        except Exception:
            pass
        STATE.callback = None
    builtins._codex_calysto_v6_pie_smoke58 = None
    try:
        if LEVEL_EDITOR.is_in_play_in_editor():
            EDITOR_LEVEL_LIBRARY.editor_end_play()
    finally:
        unreal.SystemLibrary.quit_editor()


def fail(message: str) -> None:
    finish(False, message)


def director_for_world(world: Any) -> Any:
    return find_subsystem(world, STATE.classes.get("director"))


def state_ready(snapshot: Any) -> bool:
    return "ready" in normalized(prop(snapshot, "State")) and all(
        bool(prop(snapshot, name))
        for name in (
            "bPolicyValid",
            "bPCGComplete",
            "bNavigationPathReady",
            "bRoomManifestReady",
            "bPopulationReady",
            "bVisualsReady",
            "bDoorEnabled",
        )
    )


def audit_ready_floor(world: Any, director: Any, label: str, seeded: bool) -> dict[str, Any] | None:
    snapshot = reflected(director, "get_snapshot")
    if not state_ready(snapshot):
        return None
    if int(prop(snapshot, "SchemaVersion")) != 6 or int(prop(snapshot, "GeneratorVersion")) != 6:
        raise RuntimeError("Snapshot is not the definitive V6 schema/generator")
    if seeded and int(prop(snapshot, "RunSeed")) != RUN_SEED:
        return None
    if int(prop(snapshot, "FloorNumber")) != 1:
        raise RuntimeError("First V6 PIE smoke floor is not Floor 1")

    intent = reflected(director, "get_resolved_floor_intent")
    plan = reflected(director, "get_resolved_floor_plan_v6")
    rooms = reflected(director, "get_room_manifest_v6")
    realized = reflected(director, "get_realized_floor_manifest")
    style_id = str(prop(snapshot, "StyleId"))
    room_rows = list(prop(rooms, "Rooms"))
    if not bool(prop(intent, "bIsValid")) or not bool(prop(realized, "bIsValid")):
        raise RuntimeError("V6 intent or realized manifest is invalid")
    if not room_rows or int(prop(rooms, "EligibleRoomCount")) < int(prop(rooms, "ThemedRoomCount")):
        raise RuntimeError("V6 room manifest is empty or has invalid Theme counts")

    theme_counts = {"NoTheme": 0, "Forge": 0, "Shrine": 0}
    material_failures: list[str] = []
    protected_theme_failures: list[int] = []
    stable_ids: list[int] = []
    for room in room_rows:
        stable_id = int(prop(room, "StableRoomId"))
        stable_ids.append(stable_id)
        room_style = str(prop(room, "StyleId"))
        theme_id = str(prop(room, "ThemeId"))
        themed = bool(prop(room, "bIsThemed"))
        flags = int(prop(room, "RoomFlags"))
        if room_style != style_id:
            raise RuntimeError(f"Room {stable_id} has a second Style: {room_style}")
        expected_theme = theme_id if themed else "NoTheme"
        if expected_theme not in theme_counts or (not themed and theme_id != "NoTheme"):
            raise RuntimeError(f"Room {stable_id} has invalid Theme identity {theme_id}")
        theme_counts[expected_theme] += 1
        if flags & PROTECTED_ROOM_MASK and themed:
            protected_theme_failures.append(stable_id)
        actual_materials = material_paths(prop(room, "EffectiveMaterials"))
        expected_material = EXPECTED_THEME_MATERIALS[expected_theme]
        if any(value != expected_material for value in actual_materials.values()):
            material_failures.append(
                f"room={stable_id} theme={expected_theme} actual={actual_materials}"
            )
        if not is_sha256(prop(room, "CatalogHash")) or not is_sha256(prop(room, "DecalHash")):
            raise RuntimeError(f"Room {stable_id} has invalid catalog/decal hash")

    hashes = {
        "policy": str(prop(snapshot, "FloorIntentHash")),
        "snapshot": str(prop(snapshot, "SnapshotHash")),
        "intent": str(prop(intent, "IntentHash")),
        "floor_plan": str(prop(plan, "FloorPlanHash")),
        "room_manifest": str(prop(rooms, "ManifestHash")),
        "population": str(prop(snapshot, "PopulationManifestHash")),
        "realized": str(prop(realized, "ManifestHash")),
    }
    if not all(is_sha256(value) for value in hashes.values()):
        raise RuntimeError(f"V6 runtime returned non-canonical hashes: {hashes}")
    if str(prop(plan, "PolicyHash")) != STATE.policy["hashes"]["gameplay"]:
        raise RuntimeError("V6 floor plan does not use the authored gameplay hash")
    if protected_theme_failures or material_failures or len(stable_ids) != len(set(stable_ids)):
        raise RuntimeError(
            "V6 room contract failed: "
            f"protected_themes={protected_theme_failures} materials={material_failures}"
        )
    return {
        "label": label,
        "world": canonical_world(world),
        "run_seed": int(prop(snapshot, "RunSeed")),
        "floor": int(prop(snapshot, "FloorNumber")),
        "serial": int(prop(snapshot, "GenerationSerial")),
        "style_id": style_id,
        "room_count": len(room_rows),
        "eligible_room_count": int(prop(rooms, "EligibleRoomCount")),
        "themed_room_count": int(prop(rooms, "ThemedRoomCount")),
        "theme_counts": theme_counts,
        "style_materials": material_paths(prop(plan, "StyleMaterials")),
        "hashes": hashes,
        "readiness": {
            name: bool(prop(snapshot, name))
            for name in (
                "bPCGComplete",
                "bNavigationPathReady",
                "bRoomManifestReady",
                "bPopulationReady",
                "bVisualsReady",
                "bDoorEnabled",
            )
        },
    }


def try_interact_with_entry(world: Any) -> bool:
    doors = actors_of_class(world, STATE.classes["entrance"])
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    interactions = components_of_class(player, STATE.classes["interaction"])
    if len(doors) != 1 or not player or len(interactions) != 1:
        STATE.entry_selected_samples = 0
        return False
    door = doors[0]
    interaction = interactions[0]
    if not STATE.entry_positioned:
        location = door.get_actor_location()
        player.set_actor_location(
            unreal.Vector(location.x - 80.0, location.y - 80.0, location.z),
            False,
            True,
        )
        reflected(interaction, "enable_detection", False)
        reflected(interaction, "enable_detection", True)
        STATE.entry_positioned = True
    reflected(interaction, "refresh_interactions")
    best = reflected(interaction, "get_current_best_interactable_actor")
    try:
        overlaps = {object_path(actor) for actor in interaction.get_overlapping_actors()}
    except Exception:
        overlaps = set()
    door_path = object_path(door)
    if object_path(best) == door_path and door_path in overlaps:
        STATE.entry_selected_samples += 1
    else:
        STATE.entry_selected_samples = 0
        return False
    if STATE.entry_selected_samples < SELECTION_SAMPLES_REQUIRED:
        return False
    STATE.operations.append(
        {
            "operation": "interact",
            "source": "real_acf_interaction_component",
            "actor": door_path,
            "actor_class": object_path(door.get_class()),
            "source_world": canonical_world(world),
            "selection_samples": STATE.entry_selected_samples,
        }
    )
    reflected(interaction, "interact", "CalystoV6PIESmoke58")
    STATE.entry_interacted = True
    return True


def tick(delta_seconds: float) -> None:
    del delta_seconds
    if STATE.finished:
        return
    try:
        now = time.monotonic()
        if now - STATE.started > GLOBAL_TIMEOUT:
            fail("Global timeout in phase " + STATE.phase)
            return
        if now - STATE.phase_started > PHASE_TIMEOUT:
            fail("Phase timeout in " + STATE.phase)
            return

        if STATE.phase == "load_map":
            if PROJECT_ROOT != EXPECTED_ROOT or RUN_SEED <= 0:
                raise RuntimeError("Wrong project or invalid V6 PIE seed")
            if not POLICY_FILE.is_file() or not COOK_CLOSURE_RECEIPT.is_file():
                raise RuntimeError("Definitive V6 policy/cook-closure receipt is missing")
            if STATE.initial_dirty:
                raise RuntimeError(f"Save or revert dirty packages before V6 PIE: {STATE.initial_dirty}")
            if any(not row["exists"] for row in STATE.initial_protected.values()):
                raise RuntimeError("A protected Calysto invariant file is missing")
            STATE.policy = policy_document()
            STATE.classes = {
                "director": unreal.load_class(None, DIRECTOR_CLASS),
                "entrance": unreal.load_class(None, ENTRANCE_CLASS),
                "interaction": unreal.load_class(None, INTERACTION_CLASS),
            }
            if not all(STATE.classes.values()):
                raise RuntimeError("A required V6/ACF PIE class failed to load")
            set_phase("wait_editor_hub")
            if LEVEL_EDITOR.load_level(HUB_MAP) is False:
                raise RuntimeError("HUB control map failed to load")
            return

        if STATE.phase == "wait_editor_hub":
            if canonical_world(UNREAL_EDITOR.get_editor_world()).casefold() != HUB_WORLD.casefold():
                return
            if now - STATE.phase_started < 2.0:
                return
            set_phase("wait_hub_pie")
            LEVEL_EDITOR.editor_request_begin_play()
            return

        world = game_world()
        current_world = canonical_world(world).casefold() if world else ""
        if STATE.phase == "wait_hub_pie":
            if not world or current_world != HUB_WORLD.casefold() or world_time(world) < 1.0:
                return
            director = director_for_world(world)
            if not director:
                return
            snapshot = reflected(director, "get_snapshot")
            if str(prop(snapshot, "PolicyError")):
                raise RuntimeError("V6 policy error in HUB: " + str(prop(snapshot, "PolicyError")))
            if not bool(prop(snapshot, "bPolicyValid")):
                return
            if bool(prop(snapshot, "bHasActiveRun")) or bool(reflected(director, "is_travel_request_pending")):
                raise RuntimeError("DoorToLevel smoke requires an idle V6 Director in HUB")
            if try_interact_with_entry(world):
                set_phase("wait_entry_dungeon")
            return

        if STATE.phase == "wait_entry_dungeon":
            if not world:
                return
            if current_world == HUB_WORLD.casefold():
                if STATE.saw_dungeon_world:
                    raise RuntimeError(
                        "DoorToLevel regression: DungeonGeneration returned immediately to HUB"
                    )
                return
            if current_world != DUNGEON_WORLD.casefold():
                raise RuntimeError(f"DoorToLevel opened unexpected world: {canonical_world(world)}")
            STATE.saw_dungeon_world = True
            director = director_for_world(world)
            if not director or world_time(world) < READY_WORLD_TIME:
                return
            sample = audit_ready_floor(world, director, "door_entry_probe", seeded=False)
            if sample is None:
                return
            STATE.samples.append(sample)
            accepted = bool(reflected(director, "request_start_new_run_with_seed", RUN_SEED))
            STATE.operations.append(
                {"operation": "request_start_new_run_with_seed", "seed": RUN_SEED, "accepted": accepted}
            )
            if not accepted:
                raise RuntimeError("V6 Director rejected the fixed-seed run after DoorToLevel entry")
            STATE.seeded_run_requested = True
            set_phase("wait_seeded_floor")
            return

        if STATE.phase == "wait_seeded_floor":
            if not world:
                return
            if current_world == HUB_WORLD.casefold():
                raise RuntimeError("V6 returned to HUB while preparing the seeded dungeon floor")
            if current_world != DUNGEON_WORLD.casefold():
                return
            director = director_for_world(world)
            if not director or world_time(world) < READY_WORLD_TIME:
                return
            sample = audit_ready_floor(world, director, "seeded_floor_1", seeded=True)
            if sample is None:
                return
            STATE.samples.append(sample)
            STATE.stability_started = now
            set_phase("hold_dungeon_stability")
            return

        if STATE.phase == "hold_dungeon_stability":
            if not world or current_world == HUB_WORLD.casefold():
                raise RuntimeError("DoorToLevel regression: dungeon returned immediately to HUB")
            if current_world != DUNGEON_WORLD.casefold():
                raise RuntimeError(f"Unexpected world during V6 stability hold: {canonical_world(world)}")
            if now - STATE.stability_started >= STABILITY_HOLD_SECONDS:
                finish(True)
            return
    except Exception as exc:
        fail(f"{exc}\n{traceback.format_exc()}")


existing = getattr(builtins, "_codex_calysto_v6_pie_smoke58", None)
if existing is not None:
    try:
        if existing.callback is not None:
            unreal.unregister_slate_post_tick_callback(existing.callback)
    except Exception:
        pass

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
STATE.callback = unreal.register_slate_post_tick_callback(tick)
builtins._codex_calysto_v6_pie_smoke58 = STATE
unreal.log(f"CALYSTO_V6_PIE_SMOKE validator_registered=true seed={RUN_SEED}")
