"""Fresh-process read-only UE 5.8 validation of 21 remigrated Calysto BPs.

Required environment:
  CODEX_CALYSTO_REMIGRATE_EVIDENCE
  CODEX_CALYSTO_VALIDATION_EVIDENCE
"""

import datetime
import hashlib
import json
import os

import unreal


REMIGRATE_VALUE = os.environ.get("CODEX_CALYSTO_REMIGRATE_EVIDENCE", "").strip()
VALIDATION_VALUE = (
    os.environ.get("CODEX_CALYSTO_VALIDATION_EVIDENCE", "").strip()
    or os.environ.get("CODEX_CALYSTO_VALIDATE_EVIDENCE", "").strip()
)
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


def fail(message):
    unreal.log_error("CODEX_CALYSTO_EDITOR_BP_VALIDATE58_FAIL: " + message)
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
    if not package.startswith("/Game/Calysto/"):
        fail("Package escapes /Game/Calysto: " + package)
    return os.path.realpath(
        os.path.join(
            content_root,
            package[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def snapshot(path):
    if not os.path.isfile(path):
        fail("Remigrated Blueprint file is absent: " + path)
    stat = os.stat(path)
    return {
        "file": os.path.realpath(path),
        "length": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sha256": sha256(path),
    }


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def compile_status(blueprint):
    value = str(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in value.upper() if character.isalnum())
    return value, normalized


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not REMIGRATE_VALUE or not VALIDATION_VALUE:
    fail(
        "CODEX_CALYSTO_REMIGRATE_EVIDENCE and "
        "CODEX_CALYSTO_VALIDATION_EVIDENCE are required"
    )
if len(PACKAGES) != 21 or len(set(PACKAGES)) != 21:
    fail("The guarded validation cohort is not exactly 21 unique packages")
if any(package.lower().startswith("/game/exportedanimations/") for package in PACKAGES):
    fail("ExportedAnimations entered the Calysto validation cohort")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_migration_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration"))
remigrate_path = os.path.realpath(REMIGRATE_VALUE)
validation_path = os.path.realpath(VALIDATION_VALUE)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if not os.path.isfile(remigrate_path):
    fail("Calysto remigration evidence is absent")
if not is_under(remigrate_path, saved_migration_root):
    fail("Remigration evidence escapes target Saved/Migration")
if not is_under(validation_path, saved_migration_root):
    fail("Validation evidence escapes target Saved/Migration")
if remigrate_path.lower() == validation_path.lower():
    fail("Validation evidence must not overwrite remigration evidence")

with open(project_file, "r", encoding="utf-8-sig") as handle:
    project_descriptor = json.load(handle)
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
    value = unreal.load_class(None, class_path)
    if value is None:
        fail("Required Scriptable Tools class did not load: " + class_path)
    native_class_rows.append({"class": class_path, "resolved": object_path(value)})

with open(remigrate_path, "r", encoding="utf-8-sig") as handle:
    remigrate = json.load(handle)
if remigrate.get("status") != "ASSETTOOLS_EXACT_CALYSTO_EDITOR_BP57_OVERWRITE_PASS":
    fail("Calysto remigration evidence status is not PASS")
if remigrate.get("package_count") != 21:
    fail("Calysto remigration evidence package count is not 21")
evidence_packages = tuple(str(value) for value in remigrate.get("packages", []))
if evidence_packages != PACKAGES:
    fail("Calysto remigration evidence does not contain the exact guarded cohort")
if os.path.realpath(str(remigrate.get("target_root", ""))).lower() != project_root.lower():
    fail("Calysto remigration evidence belongs to a different target project")
if (
    remigrate.get("target_delta_exact") is not True
    or remigrate.get("harness_unchanged") is not True
    or remigrate.get("ignore_dependencies") is not True
    or remigrate.get("asset_conflict") != "OVERWRITE"
    or remigrate.get("exported_animations_excluded") is not True
):
    fail("Calysto remigration evidence invariants are incomplete")

target_after_evidence = remigrate.get("target_after", {})
if set(target_after_evidence) != set(PACKAGES):
    fail("Remigration target_after snapshot is not the exact 21-package cohort")

disk_before = {}
evidence_byte_checks = []
for package in PACKAGES:
    current = snapshot(package_file(content_root, package))
    recorded = target_after_evidence.get(package, {})
    byte_match = (
        current["length"] == int(recorded.get("length", -1))
        and current["sha256"] == str(recorded.get("sha256", "")).upper()
    )
    evidence_byte_checks.append(
        {
            "package": package,
            "length_matches_target_after": current["length"]
            == int(recorded.get("length", -1)),
            "sha256_matches_target_after": current["sha256"]
            == str(recorded.get("sha256", "")).upper(),
            "bytes_match_target_after": byte_match,
        }
    )
    if not byte_match:
        fail("Current target bytes differ from remigration target_after: " + package)
    disk_before[package] = current

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Calysto"], True)
registry.wait_for_completion()
validation_rows = []
failures = []
for package in PACKAGES:
    rows = list(
        registry.get_assets_by_package_name(package, include_only_on_disk_assets=True)
    )
    classes = sorted({class_name(row) for row in rows})
    if len(rows) != 1 or "Blueprint" not in classes:
        failures.append(
            {
                "package": package,
                "reason": "ON_DISK_BLUEPRINT_ROW_INVALID",
                "registry_row_count": len(rows),
                "registry_classes": classes,
            }
        )
        continue
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(package)
        if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
            raise RuntimeError("Asset is not a Blueprint")
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status, normalized_status = compile_status(blueprint)
        parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
        parent_path = object_path(parent)
    except Exception as exc:
        failures.append(
            {"package": package, "reason": "LOAD_OR_COMPILE_EXCEPTION", "error": repr(exc)}
        )
        continue
    row = {
        "package": package,
        "compile_status": status,
        "direct_parent": parent_path,
        "direct_parent_non_null": parent is not None and bool(parent_path),
    }
    validation_rows.append(row)
    if "UPTODATE" not in normalized_status:
        failures.append(
            {"package": package, "reason": "BLUEPRINT_NOT_UP_TO_DATE", "status": status}
        )
    if parent is None or not parent_path:
        failures.append({"package": package, "reason": "NULL_BLUEPRINT_PARENT"})

disk_after = {package: snapshot(row["file"]) for package, row in disk_before.items()}
changed_hashes = [
    package
    for package in PACKAGES
    if disk_after[package]["length"] != disk_before[package]["length"]
    or disk_after[package]["sha256"] != disk_before[package]["sha256"]
]
for package in changed_hashes:
    failures.append({"package": package, "reason": "PACKAGE_BYTES_CHANGED_DURING_VALIDATION"})

passed = len(validation_rows) == 21 and not failures and not changed_hashes
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_CALYSTO_EDITOR_BP_FRESH_READ_ONLY_PASS"
        if passed
        else "UE58_CALYSTO_EDITOR_BP_FRESH_READ_ONLY_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "remigration_evidence": remigrate_path,
    "remigration_evidence_sha256": sha256(remigrate_path),
    "package_count": len(PACKAGES),
    "packages": list(PACKAGES),
    "plugin_contract": list(REQUIRED_PLUGINS),
    "native_class_checks": native_class_rows,
    "target_after_byte_checks": evidence_byte_checks,
    "loaded_compiled_validated_count": len(validation_rows),
    "validations": validation_rows,
    "all_parents_non_null": len(validation_rows) == 21
    and all(row["direct_parent_non_null"] for row in validation_rows),
    "failures": failures,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_hashes": changed_hashes,
    "package_hashes_unchanged": not changed_hashes,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
    "exported_animations_excluded": True,
}
os.makedirs(os.path.dirname(validation_path), exist_ok=True)
temporary_path = validation_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, validation_path)
if not passed:
    fail(
        "Fresh read-only validation failed: validated={}/21, failures={}, changed={}".format(
            len(validation_rows), len(failures), len(changed_hashes)
        )
    )
unreal.log("CODEX_CALYSTO_EDITOR_BP_VALIDATE58_PASS: " + validation_path)
