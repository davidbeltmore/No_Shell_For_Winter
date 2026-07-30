"""Exact UE 5.8 repair for ST_SmartScatter and PDA_VegetationCalysto.

Required environment:
  CODEX_CALYSTO_SMART_BACKUP_EVIDENCE
  CODEX_CALYSTO_SMART_REPAIR_EVIDENCE

The source project is inspected only by hash.  Only the two guarded target
packages may be saved.  /Game/ExportedAnimations is explicitly excluded.
"""

import datetime
import hashlib
import json
import os

import unreal


BACKUP_VALUE = os.environ.get("CODEX_CALYSTO_SMART_BACKUP_EVIDENCE", "").strip()
EVIDENCE_VALUE = os.environ.get("CODEX_CALYSTO_SMART_REPAIR_EVIDENCE", "").strip()
SOURCE_ROOT = os.path.realpath(r"D:\Projects UE5\LustAsDeadlySin")
SMART = "/Game/Calysto/Shared/Data/Structure/ST_SmartScatter"
VEGETATION = "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto"
SCATTER_BLUEPRINT = "/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter"
SCATTER_CLASS = SCATTER_BLUEPRINT + ".PDA_CalystoScatter_C"
SCATTER_DEFAULT = "/Game/Calysto/Shared/Data/DA_ScatterDummy"
SCATTER_MODE = "/Game/Calysto/Shared/Data/Structure/Enum_ScatterMode"
PACKAGES = (SMART, VEGETATION)
RELATIVE_FILES = {
    SMART: r"Content\Calysto\Shared\Data\Structure\ST_SmartScatter.uasset",
    VEGETATION: r"Content\Calysto\Shared\Data\Structure\PDA_VegetationCalysto.uasset",
}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_CALYSTO_SMART_SCATTER_REPAIR58_FAIL: " + message)
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


def blueprint_status(blueprint):
    raw = str(blueprint.get_editor_property("status"))
    return raw, "".join(character for character in raw.upper() if character.isalnum())


def safe_property(asset, name):
    try:
        return {"available": True, "value": str(asset.get_editor_property(name))}
    except Exception as exc:
        return {"available": False, "error": repr(exc)}


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
    fail("Both Calysto Smart Scatter repair environment variables are required")
if len(PACKAGES) != 2 or len(set(PACKAGES)) != 2:
    fail("The guarded repair cohort is not exactly two unique packages")
if any(package.lower().startswith("/game/exportedanimations") for package in PACKAGES):
    fail("ExportedAnimations entered the repair cohort")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration"))
backup_path = os.path.realpath(BACKUP_VALUE)
evidence_path = os.path.realpath(EVIDENCE_VALUE)
source_content = os.path.realpath(os.path.join(SOURCE_ROOT, "Content"))
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Repair is not running against NoShellForWinter.uproject")
if is_under(project_root, SOURCE_ROOT) or is_under(SOURCE_ROOT, project_root):
    fail("Source and target roots overlap")
if not is_under(backup_path, saved_root) or not os.path.isfile(backup_path):
    fail("Backup evidence is absent or escapes target Saved/Migration")
if not is_under(evidence_path, saved_root) or evidence_path.lower() == backup_path.lower():
    fail("Repair evidence path is invalid")

with open(backup_path, "r", encoding="utf-8-sig") as handle:
    backup = json.load(handle)
if backup.get("status") != "CALYSTO_SMART_SCATTER_PRE_REPAIR_BACKUP_PASS":
    fail("Backup evidence status is not PASS")
if backup.get("package_count") != 2 or backup.get("exported_animations_excluded") is not True:
    fail("Backup evidence cohort invariants are incomplete")
if os.path.realpath(str(backup.get("target_root", ""))).lower() != project_root.lower():
    fail("Backup evidence belongs to another target")

target_before = {package: snapshot(package_file(content_root, package)) for package in PACKAGES}
source_before = {
    package: snapshot(package_file(source_content, package)) for package in PACKAGES
}
backup_target = backup.get("target_before", {})
backup_source = backup.get("source_before", {})
for package in PACKAGES:
    relative = RELATIVE_FILES[package]
    target_record = backup_target.get(relative, {})
    source_record = backup_source.get(relative, {})
    if (
        target_before[package]["length"] != int(target_record.get("length", -1))
        or target_before[package]["sha256"] != str(target_record.get("sha256", "")).upper()
    ):
        fail("Current target bytes differ from the exact pre-repair backup: " + package)
    if (
        source_before[package]["length"] != int(source_record.get("length", -1))
        or source_before[package]["sha256"] != str(source_record.get("sha256", "")).upper()
    ):
        fail("Current source bytes differ from the read-only backup record: " + package)

content_before = inventory(content_root)
support_blueprint = unreal.EditorAssetLibrary.load_asset(SCATTER_BLUEPRINT)
if support_blueprint is None or support_blueprint.get_class().get_name() != "Blueprint":
    fail("PDA_CalystoScatter did not load as a Blueprint")
unreal.BlueprintEditorLibrary.compile_blueprint(support_blueprint)
support_status, support_normalized = blueprint_status(support_blueprint)
if "UPTODATE" not in support_normalized:
    fail("PDA_CalystoScatter is not UP_TO_DATE: " + support_status)
scatter_class = unreal.load_class(None, SCATTER_CLASS)
scatter_default = unreal.EditorAssetLibrary.load_asset(SCATTER_DEFAULT)
scatter_mode = unreal.EditorAssetLibrary.load_asset(SCATTER_MODE)
if scatter_class is None or scatter_default is None or scatter_mode is None:
    fail("Current Calysto scatter class/default/enum did not load")

smart_struct = unreal.EditorAssetLibrary.load_asset(SMART)
if smart_struct is None or smart_struct.get_class().get_name() != "UserDefinedStruct":
    fail("ST_SmartScatter did not load as a UserDefinedStruct")
struct_before = {
    "status": safe_property(smart_struct, "status"),
    "error_message": safe_property(smart_struct, "error_message"),
}
repair_result = unreal.ProjectCalystoDataRepairLibrary.repair_smart_scatter_data_member(
    smart_struct, scatter_class, scatter_default, scatter_mode
)
if isinstance(repair_result, tuple):
    helper_ok = next(
        (bool(value) for value in repair_result if isinstance(value, bool)), False
    )
    helper_details = next(
        (str(value) for value in repair_result if isinstance(value, str)), ""
    )
else:
    helper_ok = bool(repair_result)
    helper_details = ""
if not helper_ok:
    fail("Native Smart Scatter repair failed: " + helper_details)

struct_after_memory = {
    "status": safe_property(smart_struct, "status"),
    "error_message": safe_property(smart_struct, "error_message"),
}
normalized_struct_status = "".join(
    character
    for character in struct_after_memory["status"].get("value", "").upper()
    if character.isalnum()
)
if "INVALID OBJECT PROPERTY" in struct_after_memory["error_message"].get("value", "").upper():
    fail("ST_SmartScatter still reports an invalid object property")

vegetation = unreal.EditorAssetLibrary.load_asset(VEGETATION)
if vegetation is None or vegetation.get_class().get_name() != "Blueprint":
    fail("PDA_VegetationCalysto did not load as a Blueprint")
if not unreal.ProjectBlueprintMigrationLibrary.refresh_all_blueprint_nodes_and_compile_without_saving(
    vegetation
):
    fail("PDA_VegetationCalysto failed Refresh All Nodes + compile")
unreal.BlueprintEditorLibrary.compile_blueprint(vegetation)
vegetation_status, vegetation_normalized = blueprint_status(vegetation)
vegetation_parent = object_path(
    unreal.BlueprintEditorLibrary.get_blueprint_parent_class(vegetation)
)
if "UPTODATE" not in vegetation_normalized:
    fail("PDA_VegetationCalysto is not UP_TO_DATE: " + vegetation_status)
if vegetation_parent != "/Script/Engine.PrimaryDataAsset":
    fail("PDA_VegetationCalysto direct parent changed: " + vegetation_parent)

save_operations = []
for package in PACKAGES:
    result = unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False)
    save_operations.append({"package": package, "saved": bool(result)})
    if not result:
        fail("Failed to save repaired package: " + package)

target_after = {package: snapshot(package_file(content_root, package)) for package in PACKAGES}
source_after = {
    package: snapshot(package_file(source_content, package)) for package in PACKAGES
}
source_changed = [
    package
    for package in PACKAGES
    if source_after[package]["length"] != source_before[package]["length"]
    or source_after[package]["sha256"] != source_before[package]["sha256"]
]
if source_changed:
    fail("Read-only source package hashes changed: " + ", ".join(source_changed))

content_after = inventory(content_root)
content_delta = []
for relative in sorted(set(content_before) | set(content_after)):
    before = content_before.get(relative)
    after = content_after.get(relative)
    if before != after:
        content_delta.append({"relative": relative, "before": before, "after": after})
expected_delta = {
    os.path.relpath(package_file(content_root, package), content_root)
    .replace(os.sep, "/")
    .lower()
    for package in PACKAGES
}
actual_delta = {row["relative"] for row in content_delta}
if actual_delta != expected_delta:
    fail(
        "Content delta is not the exact Smart Scatter + vegetation cohort: "
        + repr(sorted(actual_delta))
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_CALYSTO_SMART_SCATTER_REPAIR_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "target_root": project_root,
    "source_root_read_only": SOURCE_ROOT,
    "backup_evidence": backup_path,
    "backup_evidence_sha256": sha256(backup_path),
    "packages": list(PACKAGES),
    "package_count": 2,
    "support_blueprint": {
        "package": SCATTER_BLUEPRINT,
        "compile_status": support_status,
        "generated_class": object_path(scatter_class),
        "default_asset": object_path(scatter_default),
        "scatter_mode_enum": object_path(scatter_mode),
    },
    "smart_struct_before": struct_before,
    "native_helper_details": helper_details,
    "smart_struct_after_in_memory": struct_after_memory,
    "vegetation_blueprint": {
        "compile_status": vegetation_status,
        "direct_parent": vegetation_parent,
    },
    "target_before": target_before,
    "target_after": target_after,
    "source_before": source_before,
    "source_after": source_after,
    "source_hashes_unchanged": not source_changed,
    "save_operations": save_operations,
    "saved_package_count": len(save_operations),
    "content_delta": content_delta,
    "target_delta_exact": actual_delta == expected_delta,
    "editor_only_native_helper": "/Script/EFProjectSystemsEditor.ProjectCalystoDataRepairLibrary",
    "runtime_marketplace_dependency_added": False,
    "exported_animations_excluded": True,
    "visual_qa_performed": False,
}
write_evidence(evidence_path, payload)
unreal.log(
    "CODEX_CALYSTO_SMART_SCATTER_REPAIR58_PASS: packages=2 evidence=" + evidence_path
)
