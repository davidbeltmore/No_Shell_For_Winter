"""Overwrite only three damaged Calysto assets from detached Harness57.

Required environment:
  CODEX_CALYSTO_CORE3_TARGET_ROOT
  CODEX_CALYSTO_CORE3_BACKUP_EVIDENCE
  CODEX_CALYSTO_CORE3_REMIGRATE_EVIDENCE
"""

import datetime
import hashlib
import json
import os

import unreal


TARGET_VALUE = os.environ.get("CODEX_CALYSTO_CORE3_TARGET_ROOT", "").strip()
BACKUP_VALUE = os.environ.get("CODEX_CALYSTO_CORE3_BACKUP_EVIDENCE", "").strip()
EVIDENCE_VALUE = os.environ.get("CODEX_CALYSTO_CORE3_REMIGRATE_EVIDENCE", "").strip()
PACKAGES = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController",
    "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto",
)
EXPECTED_HARNESS_BYTES = {
    PACKAGES[0]: (411781, "334CB0FDABE322EC42B79DB3280567A942FB3D113CF327A271E19300E1BFCDDF"),
    PACKAGES[1]: (91040, "08A66B96D7687D15A89715925C59DC3880AC54D066E8763A1E63AE16773192B1"),
    PACKAGES[2]: (11312, "647A24176CDEEF92AD0F70E44A1815D0FB5773AFAB6A7C9C5FB295DC316D0A3E"),
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
BLUEPRINT_CLASS_SUPPORT = {
    "/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
}
KNOWN_MISSING_CONTROLLER_DEPENDENCY = (
    "/Game/Calysto/World/Blueprint/Legacy/BP_Biome3_0"
)
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_CALYSTO_CORE3_REMIGRATE57_FAIL: " + message)
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


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not TARGET_VALUE or not BACKUP_VALUE or not EVIDENCE_VALUE:
    fail("All documented Calysto Core3 environment variables are required")
if len(PACKAGES) != 3 or len(set(PACKAGES)) != 3:
    fail("Guarded remigration cohort is not exactly three unique packages")
if any(package.lower().startswith("/game/exportedanimations") for package in PACKAGES):
    fail("ExportedAnimations entered the Core3 cohort")

target_root = os.path.realpath(TARGET_VALUE)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
saved_root = os.path.realpath(os.path.join(target_root, "Saved", "Migration"))
backup_path = os.path.realpath(BACKUP_VALUE)
evidence_path = os.path.realpath(EVIDENCE_VALUE)
harness_project = os.path.realpath(unreal.Paths.get_project_file_path())
harness_root = os.path.dirname(harness_project)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if os.path.basename(harness_project).lower() != "bulkprojectcontent57harness.uproject":
    fail("UE 5.7 process is not using BulkProjectContent57Harness.uproject")
if os.path.basename(harness_root).lower() != "harness57" or not is_under(harness_root, saved_root):
    fail("Loaded Harness57 is not detached below target Saved/Migration")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Target root is not NoShellForWinter")
if not os.path.isdir(target_content) or is_under(harness_content, target_content):
    fail("Harness and target Content are not safely detached")
if not is_under(backup_path, saved_root) or not os.path.isfile(backup_path):
    fail("Backup evidence is absent or escapes target Saved/Migration")
if not is_under(evidence_path, saved_root) or evidence_path.lower() == backup_path.lower():
    fail("Remigration evidence path is invalid")

with open(harness_project, "r", encoding="utf-8-sig") as handle:
    descriptor = json.load(handle)
if str(descriptor.get("EngineAssociation", "")) != "5.7":
    fail("Harness descriptor is not associated with UE 5.7")
with open(backup_path, "r", encoding="utf-8-sig") as handle:
    backup = json.load(handle)
if backup.get("status") != "EXACT_CALYSTO_CORE3_TARGET_BACKUP_PASS":
    fail("Core3 backup evidence status is not PASS")
if tuple(str(value) for value in backup.get("packages", [])) != PACKAGES:
    fail("Core3 backup evidence package cohort differs")
if os.path.realpath(str(backup.get("target_root", ""))).lower() != target_root.lower():
    fail("Core3 backup belongs to a different target")

harness_inventory_before = inventory(harness_content)
target_inventory_before = inventory(target_content)
source_before = {}
target_before = {}
target_matches_backup_before_run = {}
expected_modified = set()
for package in PACKAGES:
    source_file = package_file(harness_content, package)
    target_file = package_file(target_content, package)
    source_before[package] = snapshot(source_file)
    target_before[package] = snapshot(target_file)
    expected_length, expected_hash = EXPECTED_HARNESS_BYTES[package]
    if (
        source_before[package]["length"] != expected_length
        or source_before[package]["sha256"] != expected_hash
    ):
        fail("Harness bytes differ from audited bulk evidence: " + package)
    backed_up = backup.get("target_before", {}).get(package, {})
    target_matches_backup_before_run[package] = not (
        target_before[package]["length"] != int(backed_up.get("length", -1))
        or target_before[package]["sha256"]
        != str(backed_up.get("sha256", "")).upper()
    )
    for extension in PACKAGE_EXTENSIONS[2:]:
        if os.path.exists(os.path.splitext(source_file)[0] + extension):
            fail("Harness sidecar violates exact three-file contract: " + package)
        if os.path.exists(os.path.splitext(target_file)[0] + extension):
            fail("Target sidecar violates exact three-file contract: " + package)
    expected_modified.add(
        os.path.relpath(target_file, target_content).replace(os.sep, "/").lower()
    )

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Calysto"], True)
registry.wait_for_completion()
support_rows = []
for package in SUPPORT_LOAD_ORDER:
    support_file = package_file(harness_content, package)
    if not os.path.isfile(support_file):
        fail("Required prewarm support package is absent: " + package)
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Required prewarm support asset did not load: " + package)
    row = {"package": package, "asset_class": asset.get_class().get_name()}
    if package in BLUEPRINT_CLASS_SUPPORT:
        asset_name = package.rsplit("/", 1)[-1]
        generated = unreal.load_class(None, package + "." + asset_name + "_C")
        if generated is None:
            fail("Prewarm Blueprint generated class did not load: " + package)
        row["generated_class"] = object_path(generated)
    support_rows.append(row)

missing_controller_file = package_file(
    harness_content, KNOWN_MISSING_CONTROLLER_DEPENDENCY
)
controller_dependency_present = os.path.isfile(missing_controller_file)
if controller_dependency_present:
    dependency_asset = unreal.EditorAssetLibrary.load_asset(
        KNOWN_MISSING_CONTROLLER_DEPENDENCY
    )
    if dependency_asset is None:
        fail("Present BP_Biome3_0 dependency did not load")

source_load_rows = []
source_parent_chain_failures = []
for package in PACKAGES:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None or asset.get_class().get_name() != "Blueprint":
        fail("Core3 source asset is not a loadable Blueprint: " + package)
    asset_name = package.rsplit("/", 1)[-1]
    generated = unreal.load_class(None, package + "." + asset_name + "_C")
    if generated is None:
        fail("Core3 generated class did not load after prewarm: " + package)
    try:
        parent = generated.get_super_class()
    except Exception:
        parent = None
    parent_path = object_path(parent)
    parent_chain_valid = parent is not None and bool(parent_path)
    if not parent_chain_valid:
        source_parent_chain_failures.append(package)
        unreal.log_warning(
            "CODEX_CALYSTO_CORE3_SOURCE_PARENT_DIAGNOSTIC: "
            "generated class has no resolved superclass: " + package
        )
    source_load_rows.append(
        {
            "package": package,
            "generated_class": object_path(generated),
            "parent_class": parent_path,
            "parent_chain_valid": parent_chain_valid,
        }
    )

options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", True)
options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.OVERWRITE)
options.set_editor_property("orphan_folder", "")
unreal.log("CODEX_CALYSTO_CORE3_REMIGRATE57_BEGIN: packages=3")
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(PACKAGES), target_content, options
)

target_inventory_after = inventory(target_content)
harness_inventory_after = inventory(harness_content)
created = sorted(set(target_inventory_after) - set(target_inventory_before))
removed = sorted(set(target_inventory_before) - set(target_inventory_after))
modified = sorted(
    relative
    for relative in set(target_inventory_before).intersection(target_inventory_after)
    if target_inventory_before[relative] != target_inventory_after[relative]
)
if created or removed or set(modified) != expected_modified:
    fail(
        "Target delta is not the exact three overwritten files: "
        "created={!r}, removed={!r}, modified={!r}".format(created, removed, modified)
    )
if harness_inventory_after != harness_inventory_before:
    fail("Harness57 package inventory changed during remigration")

source_after = {package: snapshot(row["file"]) for package, row in source_before.items()}
target_after = {package: snapshot(row["file"]) for package, row in target_before.items()}
byte_changed_packages = []
byte_unchanged_rewritten_packages = []
for package in PACKAGES:
    if source_after[package] != source_before[package]:
        fail("Harness57 source package changed: " + package)
    if (
        target_after[package]["length"] == target_before[package]["length"]
        and target_after[package]["sha256"] == target_before[package]["sha256"]
    ):
        byte_unchanged_rewritten_packages.append(package)
    else:
        byte_changed_packages.append(package)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_CALYSTO_CORE3_OVERWRITE57_PASS",
    "engine_version": engine_version,
    "harness_project": harness_project,
    "harness_content": harness_content,
    "target_root": target_root,
    "backup_evidence": backup_path,
    "backup_evidence_sha256": sha256(backup_path),
    "package_count": 3,
    "packages": list(PACKAGES),
    "support_loads": support_rows,
    "source_load_checks": source_load_rows,
    "source_parent_chain_valid": not source_parent_chain_failures,
    "source_parent_chain_failures": source_parent_chain_failures,
    "known_controller_dependency": KNOWN_MISSING_CONTROLLER_DEPENDENCY,
    "known_controller_dependency_present": controller_dependency_present,
    "source_before": source_before,
    "source_after": source_after,
    "target_before": target_before,
    "target_after": target_after,
    "target_matches_backup_before_run": target_matches_backup_before_run,
    "all_targets_matched_backup_before_run": all(
        target_matches_backup_before_run.values()
    ),
    "byte_changed_packages": byte_changed_packages,
    "byte_unchanged_rewritten_packages": byte_unchanged_rewritten_packages,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_package_files": modified,
    "target_delta_exact": True,
    "harness_unchanged": True,
    "ignore_dependencies": True,
    "asset_conflict": "OVERWRITE",
    "exported_animations_excluded": True,
    "source_project_touched": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary = evidence_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, evidence_path)
unreal.log("CODEX_CALYSTO_CORE3_REMIGRATE57_PASS: " + evidence_path)
