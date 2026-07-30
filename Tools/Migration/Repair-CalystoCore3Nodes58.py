"""All-or-nothing UE 5.8 Refresh All Nodes repair for three Calysto BPs.

Required environment:
  CODEX_CALYSTO_CORE3_BACKUP_EVIDENCE
  CODEX_CALYSTO_CORE3_NODE_REPAIR_EVIDENCE

Requires the editor-only UProjectBlueprintMigrationLibrary native wrapper.
No asset is saved unless all three Blueprints compile successfully in memory.
"""

import datetime
import hashlib
import json
import os

import unreal


BACKUP_VALUE = os.environ.get("CODEX_CALYSTO_CORE3_BACKUP_EVIDENCE", "").strip()
EVIDENCE_VALUE = os.environ.get(
    "CODEX_CALYSTO_CORE3_NODE_REPAIR_EVIDENCE", ""
).strip()
PACKAGES = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController",
    "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto",
)
EXPECTED_PARENTS = {
    PACKAGES[0]: "/Script/Engine.Actor",
    PACKAGES[1]: "/Script/Engine.PlayerController",
    PACKAGES[2]: "/Script/Engine.PrimaryDataAsset",
}
SUPPORT_LOAD_ORDER = (
    "/Game/Calysto/Shared/Data/Structure/Enum_SpawnType",
    "/Game/Calysto/Shared/Data/Structure/ST_CalystoScatterBlueprint",
    "/Game/Calysto/Shared/Data/Structure/ST_CalystoScatter",
    "/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter",
    "/Game/Calysto/Shared/Data/Structure/Enum_ScatterMode",
    "/Game/Calysto/Shared/Data/Structure/ST_SmartScatter",
    "/Game/Calysto/Shared/Data/Structure/ST_PlaceOfInterest",
    "/Game/Calysto/Shared/Data/Structure/ST_Vegetation",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/PCG/PCG_DungeonEditor",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
    "/Game/Calysto/Shared/Input/IMC_Default",
)
KNOWN_MISSING_CONTROLLER_DEPENDENCY = (
    "/Game/Calysto/World/Blueprint/Legacy/BP_Biome3_0"
)
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_CALYSTO_CORE3_NODE_REPAIR58_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    try:
        return os.path.commonpath(
            [os.path.realpath(path), os.path.realpath(root)]
        ).lower() == os.path.realpath(root).lower()
    except ValueError:
        return False


def package_file(content_root, package):
    return os.path.realpath(
        os.path.join(
            content_root,
            package[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def snapshot(path):
    if not os.path.isfile(path):
        fail("Required package file is absent: " + path)
    stat = os.stat(path)
    return {
        "file": os.path.realpath(path),
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(path),
    }


def inventory(content_root):
    result = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = os.path.realpath(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/").lower()
            stat = os.stat(path)
            result[relative] = {"length": stat.st_size, "mtime_ns": stat.st_mtime_ns}
    return result


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def status(blueprint):
    raw = str(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in raw.upper() if character.isalnum())
    return raw, normalized


def write_evidence(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
    os.replace(temporary, path)


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not BACKUP_VALUE or not EVIDENCE_VALUE:
    fail("Both documented Core3 node-repair environment variables are required")
if len(PACKAGES) != 3 or len(set(PACKAGES)) != 3:
    fail("Guarded node-repair cohort is not exactly three unique packages")
if any(package.lower().startswith("/game/exportedanimations") for package in PACKAGES):
    fail("ExportedAnimations entered the Core3 node-repair cohort")
if not hasattr(unreal, "ProjectBlueprintMigrationLibrary"):
    fail("ProjectBlueprintMigrationLibrary native editor wrapper is unavailable")
if not hasattr(
    unreal.ProjectBlueprintMigrationLibrary, "refresh_all_blueprint_nodes"
):
    fail("Native RefreshAllBlueprintNodes function is unavailable")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration"))
backup_path = os.path.realpath(BACKUP_VALUE)
evidence_path = os.path.realpath(EVIDENCE_VALUE)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if not is_under(backup_path, saved_root) or not os.path.isfile(backup_path):
    fail("Core3 backup evidence is absent or escapes Saved/Migration")
if not is_under(evidence_path, saved_root) or evidence_path.lower() == backup_path.lower():
    fail("Core3 node-repair evidence path is invalid")
with open(backup_path, "r", encoding="utf-8-sig") as handle:
    backup = json.load(handle)
if backup.get("status") != "EXACT_CALYSTO_CORE3_TARGET_BACKUP_PASS":
    fail("Core3 backup evidence status is not PASS")
if tuple(str(value) for value in backup.get("packages", [])) != PACKAGES:
    fail("Core3 backup evidence package cohort differs")
if os.path.realpath(str(backup.get("target_root", ""))).lower() != project_root.lower():
    fail("Core3 backup evidence belongs to another project")

before_inventory = inventory(content_root)
disk_before = {}
expected_modified = set()
for package in PACKAGES:
    current = snapshot(package_file(content_root, package))
    recorded = backup.get("target_before", {}).get(package, {})
    if (
        current["length"] != int(recorded.get("length", -1))
        or current["sha256"] != str(recorded.get("sha256", "")).upper()
    ):
        fail("Live package bytes differ from exact backup evidence: " + package)
    disk_before[package] = current
    expected_modified.add(
        os.path.relpath(current["file"], content_root).replace(os.sep, "/").lower()
    )

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Calysto"], True)
registry.wait_for_completion()
support_rows = []
for package in SUPPORT_LOAD_ORDER:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    support_rows.append(
        {
            "package": package,
            "loaded": asset is not None,
            "asset_class": asset.get_class().get_name() if asset is not None else "",
        }
    )
    if asset is None:
        fail("Required Core3 support asset did not load: " + package)

controller_dependency_present = os.path.isfile(
    package_file(content_root, KNOWN_MISSING_CONTROLLER_DEPENDENCY)
)
blueprints = {}
parents_before = {}
for package in PACKAGES:
    blueprint = unreal.EditorAssetLibrary.load_asset(package)
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        fail("Core3 asset is not a loadable Blueprint: " + package)
    parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
    parent_path = object_path(parent)
    if parent_path != EXPECTED_PARENTS[package]:
        fail(
            "Core3 parent mismatch before refresh: {} actual={} expected={}".format(
                package, parent_path, EXPECTED_PARENTS[package]
            )
        )
    blueprints[package] = blueprint
    parents_before[package] = parent_path

repair_rows = []
failures = []
for package in PACKAGES:
    blueprint = blueprints[package]
    attempts = []
    refresh_result = unreal.ProjectBlueprintMigrationLibrary.refresh_all_blueprint_nodes(
        blueprint
    )
    if package == PACKAGES[0]:
        unreal.BlueprintEditorLibrary.upgrade_operator_nodes(blueprint)
        refresh_result = (
            unreal.ProjectBlueprintMigrationLibrary.refresh_all_blueprint_nodes(
                blueprint
            )
            and refresh_result
        )
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    raw_status, normalized = status(blueprint)
    attempts.append({"operation": "REFRESH_ALL_NODES", "status": raw_status})
    if "UPTODATE" not in normalized and hasattr(
        unreal.ProjectBlueprintMigrationLibrary, "reconstruct_all_blueprint_nodes"
    ):
        reconstruct_result = (
            unreal.ProjectBlueprintMigrationLibrary.reconstruct_all_blueprint_nodes(
                blueprint
            )
        )
        refresh_result = (
            unreal.ProjectBlueprintMigrationLibrary.refresh_all_blueprint_nodes(
                blueprint
            )
            and refresh_result
        )
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        raw_status, normalized = status(blueprint)
        attempts.append(
            {
                "operation": "RECONSTRUCT_THEN_REFRESH",
                "native_reconstruct_result": bool(reconstruct_result),
                "status": raw_status,
            }
        )
    parent_after = object_path(
        unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
    )
    row = {
        "package": package,
        "native_refresh_result": bool(refresh_result),
        "parent_before": parents_before[package],
        "parent_after": parent_after,
        "compile_status": raw_status,
        "attempts": attempts,
    }
    repair_rows.append(row)
    if not refresh_result:
        failures.append({"package": package, "reason": "NATIVE_REFRESH_FAILED"})
    if parent_after != EXPECTED_PARENTS[package]:
        failures.append({"package": package, "reason": "PARENT_CHANGED_DURING_REFRESH"})
    if "UPTODATE" not in normalized:
        failures.append(
            {"package": package, "reason": "BLUEPRINT_NOT_UP_TO_DATE", "status": raw_status}
        )

save_operations = []
if not failures:
    for package in PACKAGES:
        result = unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False)
        save_operations.append({"package": package, "result": bool(result)})
        if not result:
            failures.append({"package": package, "reason": "EXACT_SAVE_FAILED"})

after_inventory = inventory(content_root)
disk_after = {package: snapshot(row["file"]) for package, row in disk_before.items()}
created = sorted(set(after_inventory) - set(before_inventory))
removed = sorted(set(before_inventory) - set(after_inventory))
modified = sorted(
    relative
    for relative in set(before_inventory).intersection(after_inventory)
    if before_inventory[relative] != after_inventory[relative]
)
if failures:
    if created or removed or modified:
        failures.append({"reason": "DISK_CHANGED_DESPITE_PRE_SAVE_FAILURE"})
else:
    if created or removed or set(modified) != expected_modified:
        failures.append(
            {
                "reason": "TARGET_DELTA_NOT_EXACT",
                "created": created,
                "removed": removed,
                "modified": modified,
            }
        )

passed = not failures and len(save_operations) == 3
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_CALYSTO_CORE3_NODE_REPAIR_PASS"
        if passed
        else "UE58_CALYSTO_CORE3_NODE_REPAIR_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "backup_evidence": backup_path,
    "backup_evidence_sha256": sha256(backup_path),
    "package_count": 3,
    "packages": list(PACKAGES),
    "support_loads": support_rows,
    "known_controller_dependency": KNOWN_MISSING_CONTROLLER_DEPENDENCY,
    "known_controller_dependency_present": controller_dependency_present,
    "repair_rows": repair_rows,
    "save_operations": save_operations,
    "saved_only_if_all_compiled": True,
    "failures": failures,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_package_files": modified,
    "target_delta_exact": passed,
    "exported_animations_excluded": True,
}
write_evidence(evidence_path, payload)
if not passed:
    fail(
        "Core3 node repair failed before exact completion: failures={}, modified={}".format(
            len(failures), len(modified)
        )
    )
unreal.log("CODEX_CALYSTO_CORE3_NODE_REPAIR58_PASS: " + evidence_path)
