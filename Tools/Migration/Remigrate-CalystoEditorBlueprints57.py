"""Overwrite the 21 damaged Calysto editor Blueprints from detached UE 5.7.

Required environment:
  CODEX_CALYSTO_REMIGRATE_TARGET_ROOT
  CODEX_CALYSTO_REMIGRATE_EVIDENCE

Run only with BulkProjectContent57Harness.uproject under UE 5.7.  The script
does not compile or save the harness assets; AssetTools writes only the exact
21 destination packages with dependencies disabled.
"""

import datetime
import hashlib
import json
import os

import unreal


TARGET_ROOT_VALUE = os.environ.get(
    "CODEX_CALYSTO_REMIGRATE_TARGET_ROOT", ""
).strip()
EVIDENCE_VALUE = os.environ.get("CODEX_CALYSTO_REMIGRATE_EVIDENCE", "").strip()

# Dependency order: native-parent property/tool Blueprints first, followed by
# the Blueprints whose parents are other members of this exact cohort.
PACKAGES = (
    "/Game/Calysto/Shared/Blueprint/Editor/BP_DebugProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_RemoveSpawnerProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_SpawnerOverrideProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_ToolsProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_DecorRoomProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_EditDungeonProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_PaintRoomProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_PlaceLightProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_SpawnerProperty",
    "/Game/Calysto/Dungeon/Blueprint/Editor/Property/BP_WallOverrideProperty",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_DebugTool",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_DragMaster",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_EditorModularBehabiorTool",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_OverrideSpawner",
    "/Game/Calysto/Shared/Blueprint/Editor/BP_RemoveSpawner",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_DecorateRoom",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_EditDungeon",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_OverrideSpawner",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_OverrideWall",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_PaintRoom",
    "/Game/Calysto/Dungeon/Blueprint/Editor/BP_PlaceLight",
)
REQUIRED_PLUGINS = ("ScriptableToolsFramework", "ScriptableToolsEditorMode")
REQUIRED_NATIVE_CLASSES = (
    "/Script/ScriptableToolsFramework.ScriptableInteractiveToolPropertySet",
    "/Script/EditorScriptableToolsFramework.EditorScriptableClickDragTool",
    "/Script/EditorScriptableToolsFramework.EditorScriptableInteractiveToolPropertySet",
    "/Script/EditorScriptableToolsFramework.EditorScriptableModularBehaviorTool",
)
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_CALYSTO_EDITOR_BP57_REMIGRATE_FAIL: " + message)
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
    if not package.startswith("/Game/"):
        fail("Package is not rooted at /Game: " + package)
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


def content_inventory(content_root):
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


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not TARGET_ROOT_VALUE or not EVIDENCE_VALUE:
    fail("Both documented Calysto remigration environment variables are required")
if len(PACKAGES) != 21 or len(set(PACKAGES)) != 21:
    fail("The hardcoded Calysto editor Blueprint cohort is not exactly 21 unique packages")
if any(
    package.lower() == "/game/exportedanimations"
    or package.lower().startswith("/game/exportedanimations/")
    for package in PACKAGES
):
    fail("ExportedAnimations entered the Calysto editor Blueprint cohort")

target_root = os.path.realpath(TARGET_ROOT_VALUE)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
evidence_path = os.path.realpath(EVIDENCE_VALUE)
harness_project = os.path.realpath(unreal.Paths.get_project_file_path())
harness_root = os.path.dirname(harness_project)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
saved_migration_root = os.path.realpath(os.path.join(target_root, "Saved", "Migration"))

if os.path.basename(harness_project).lower() != "bulkprojectcontent57harness.uproject":
    fail("UE 5.7 process is not using BulkProjectContent57Harness.uproject")
if os.path.basename(harness_root).lower() != "harness57":
    fail("Detached project directory is not Harness57")
if not is_under(harness_root, saved_migration_root):
    fail("Harness57 is not detached under target Saved/Migration")
if harness_content.lower() != os.path.realpath(os.path.join(harness_root, "Content")).lower():
    fail("Loaded project Content does not belong to Harness57")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Target root is not NoShellForWinter")
if not os.path.isdir(target_content) or not is_under(target_content, target_root):
    fail("Target Content is absent or escapes NoShellForWinter")
if is_under(harness_content, target_content) or is_under(target_content, harness_content):
    fail("Harness57 and live target Content are not detached")
if not is_under(evidence_path, saved_migration_root):
    fail("Evidence path escapes target Saved/Migration")
if os.path.splitext(evidence_path)[1].lower() != ".json":
    fail("Evidence path must be a JSON file")

with open(harness_project, "r", encoding="utf-8-sig") as handle:
    project_descriptor = json.load(handle)
if str(project_descriptor.get("EngineAssociation", "")) != "5.7":
    fail("Harness project descriptor is not associated with UE 5.7")
enabled_plugins = {
    str(row.get("Name", ""))
    for row in project_descriptor.get("Plugins", [])
    if row.get("Enabled") is True
}
missing_plugins = sorted(set(REQUIRED_PLUGINS) - enabled_plugins)
if missing_plugins:
    fail("Required Scriptable Tools plugins are not enabled: {!r}".format(missing_plugins))

native_class_rows = []
for class_path in REQUIRED_NATIVE_CLASSES:
    native_class = unreal.load_class(None, class_path)
    if native_class is None:
        fail("Required Scriptable Tools class did not load: " + class_path)
    native_class_rows.append(
        {"class": class_path, "resolved": native_class.get_path_name()}
    )

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Calysto"], True)
registry.wait_for_completion()

source_before = {}
target_before = {}
load_rows = []
expected_modified = set()
for package in PACKAGES:
    source_path = package_file(harness_content, package)
    target_path = package_file(target_content, package)
    if not is_under(source_path, harness_content) or not is_under(target_path, target_content):
        fail("Package path escaped its Content root: " + package)
    for extension in PACKAGE_EXTENSIONS[2:]:
        if os.path.exists(os.path.splitext(source_path)[0] + extension):
            fail("Harness sidecar violates the exact 21-file contract: " + package)
        if os.path.exists(os.path.splitext(target_path)[0] + extension):
            fail("Target sidecar violates the exact 21-file contract: " + package)
    source_before[package] = snapshot(source_path)
    target_before[package] = snapshot(target_path)
    assets = list(
        registry.get_assets_by_package_name(package, include_only_on_disk_assets=True)
    )
    if not assets:
        fail("Harness Asset Registry has no on-disk asset for " + package)
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None or asset.get_class().get_name() != "Blueprint":
        fail("Harness asset is not a loadable Blueprint: " + package)
    object_name = package.rsplit("/", 1)[-1]
    generated_class = unreal.load_class(None, package + "." + object_name + "_C")
    if generated_class is None:
        fail("Blueprint generated class or parent chain did not load: " + package)
    try:
        parent_class = generated_class.get_super_class()
        parent_path = parent_class.get_path_name() if parent_class else ""
    except Exception:
        parent_path = "AVAILABLE_GENERATED_CLASS"
    load_rows.append(
        {
            "package": package,
            "generated_class": generated_class.get_path_name(),
            "parent_class": parent_path,
        }
    )
    expected_modified.add(
        os.path.relpath(target_path, target_content).replace(os.sep, "/").lower()
    )

before_inventory = content_inventory(target_content)
migration_options = unreal.MigrationOptions()
migration_options.set_editor_property("prompt", False)
migration_options.set_editor_property("ignore_dependencies", True)
migration_options.set_editor_property(
    "asset_conflict", unreal.AssetMigrationConflict.OVERWRITE
)
migration_options.set_editor_property("orphan_folder", "")
unreal.log("CODEX_CALYSTO_EDITOR_BP57_REMIGRATE_BEGIN: packages=21")
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(PACKAGES), target_content, migration_options
)

after_inventory = content_inventory(target_content)
created = sorted(set(after_inventory) - set(before_inventory))
removed = sorted(set(before_inventory) - set(after_inventory))
modified = sorted(
    relative
    for relative in set(before_inventory).intersection(after_inventory)
    if before_inventory[relative] != after_inventory[relative]
)
if created or removed or set(modified) != expected_modified:
    fail(
        "Target delta is not the exact 21 overwritten files; "
        "created={!r}, removed={!r}, modified={!r}".format(
            created[:50], removed[:50], modified[:50]
        )
    )

source_after = {package: snapshot(row["file"]) for package, row in source_before.items()}
target_after = {package: snapshot(row["file"]) for package, row in target_before.items()}
for package in PACKAGES:
    if source_after[package] != source_before[package]:
        fail("Harness package bytes or metadata changed: " + package)
    if (
        target_after[package]["length"] == target_before[package]["length"]
        and target_after[package]["sha256"] == target_before[package]["sha256"]
    ):
        fail("OVERWRITE did not change target package bytes: " + package)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_CALYSTO_EDITOR_BP57_OVERWRITE_PASS",
    "engine_version": engine_version,
    "harness_project": harness_project,
    "harness_content": harness_content,
    "target_root": target_root,
    "destination": target_content,
    "package_count": len(PACKAGES),
    "packages": list(PACKAGES),
    "plugin_contract": list(REQUIRED_PLUGINS),
    "native_class_checks": native_class_rows,
    "blueprint_load_checks": load_rows,
    "source_before": source_before,
    "source_after": source_after,
    "target_before": target_before,
    "target_after": target_after,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_package_files": modified,
    "target_delta_exact": True,
    "harness_unchanged": True,
    "ignore_dependencies": True,
    "asset_conflict": "OVERWRITE",
    "exported_animations_excluded": True,
    "raw_copy_to_target_content": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary_path = evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, evidence_path)
unreal.log("CODEX_CALYSTO_EDITOR_BP57_REMIGRATE_PASS: " + evidence_path)
