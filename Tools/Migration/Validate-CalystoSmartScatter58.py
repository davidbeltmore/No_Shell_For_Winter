"""Fresh-process, read-only validation for the repaired Calysto data contract."""

import datetime
import hashlib
import json
import os

import unreal


REPAIR_VALUE = os.environ.get("CODEX_CALYSTO_SMART_REPAIR_EVIDENCE", "").strip()
EVIDENCE_VALUE = os.environ.get("CODEX_CALYSTO_SMART_VALIDATION_EVIDENCE", "").strip()
SMART = "/Game/Calysto/Shared/Data/Structure/ST_SmartScatter"
VEGETATION = "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto"
SCATTER_BLUEPRINT = "/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter"
SCATTER_CLASS = SCATTER_BLUEPRINT + ".PDA_CalystoScatter_C"
SCATTER_DEFAULT = "/Game/Calysto/Shared/Data/DA_ScatterDummy"
SCATTER_MODE = "/Game/Calysto/Shared/Data/Structure/Enum_ScatterMode"
PACKAGES = (SMART, VEGETATION)
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")


def fail(message):
    unreal.log_error("CODEX_CALYSTO_SMART_SCATTER_VALIDATE58_FAIL: " + message)
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
        fail("Required package is absent: " + path)
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


def safe_property(asset, name):
    try:
        return {"available": True, "value": str(asset.get_editor_property(name))}
    except Exception as exc:
        return {"available": False, "error": repr(exc)}


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def contains_ascii(path, text):
    with open(path, "rb") as handle:
        return text.encode("utf-8") in handle.read()


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not REPAIR_VALUE or not EVIDENCE_VALUE:
    fail("Both Calysto Smart Scatter validation environment variables are required")
if any(package.lower().startswith("/game/exportedanimations") for package in PACKAGES):
    fail("ExportedAnimations entered the validation cohort")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration"))
repair_path = os.path.realpath(REPAIR_VALUE)
evidence_path = os.path.realpath(EVIDENCE_VALUE)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Validation is not running against NoShellForWinter.uproject")
if not is_under(repair_path, saved_root) or not os.path.isfile(repair_path):
    fail("Repair evidence is absent or escapes Saved/Migration")
if not is_under(evidence_path, saved_root) or evidence_path.lower() == repair_path.lower():
    fail("Validation evidence path is invalid")

with open(repair_path, "r", encoding="utf-8-sig") as handle:
    repair = json.load(handle)
if repair.get("status") != "UE58_CALYSTO_SMART_SCATTER_REPAIR_PASS":
    fail("Repair evidence status is not PASS")
if tuple(str(value) for value in repair.get("packages", [])) != PACKAGES:
    fail("Repair evidence package cohort differs")
if (
    repair.get("package_count") != 2
    or repair.get("saved_package_count") != 2
    or repair.get("target_delta_exact") is not True
    or repair.get("source_hashes_unchanged") is not True
    or repair.get("exported_animations_excluded") is not True
):
    fail("Repair evidence invariants are incomplete")

recorded_after = repair.get("target_after", {})
disk_before = {}
repair_byte_checks = []
for package in PACKAGES:
    current = snapshot(package_file(content_root, package))
    recorded = recorded_after.get(package, {})
    matches = (
        current["length"] == int(recorded.get("length", -1))
        and current["sha256"] == str(recorded.get("sha256", "")).upper()
    )
    repair_byte_checks.append({"package": package, "matches_repair_after": matches})
    if not matches:
        fail("Current bytes differ from repair evidence: " + package)
    disk_before[package] = current

content_before = inventory(content_root)
support_blueprint = unreal.EditorAssetLibrary.load_asset(SCATTER_BLUEPRINT)
if support_blueprint is None:
    fail("PDA_CalystoScatter did not load")
unreal.BlueprintEditorLibrary.compile_blueprint(support_blueprint)
support_status = str(support_blueprint.get_editor_property("status"))
if "UPTODATE" not in "".join(c for c in support_status.upper() if c.isalnum()):
    fail("PDA_CalystoScatter is not UP_TO_DATE: " + support_status)
scatter_class = unreal.load_class(None, SCATTER_CLASS)
scatter_default = unreal.EditorAssetLibrary.load_asset(SCATTER_DEFAULT)
scatter_mode = unreal.EditorAssetLibrary.load_asset(SCATTER_MODE)
if scatter_class is None or scatter_default is None or scatter_mode is None:
    fail("Current Calysto scatter class/default/enum did not load")

smart_struct = unreal.EditorAssetLibrary.load_asset(SMART)
if smart_struct is None or smart_struct.get_class().get_name() != "UserDefinedStruct":
    fail("ST_SmartScatter did not load as a UserDefinedStruct")
struct_status = safe_property(smart_struct, "status")
struct_error = safe_property(smart_struct, "error_message")
native_validation_result = unreal.ProjectCalystoDataRepairLibrary.repair_smart_scatter_data_member(
    smart_struct, scatter_class, scatter_default, scatter_mode
)
if isinstance(native_validation_result, tuple):
    native_struct_valid = next(
        (bool(value) for value in native_validation_result if isinstance(value, bool)),
        False,
    )
    native_struct_details = next(
        (str(value) for value in native_validation_result if isinstance(value, str)),
        "",
    )
else:
    native_struct_valid = bool(native_validation_result)
    native_struct_details = ""
if not native_struct_valid:
    fail("Native ST_SmartScatter fresh validation failed: " + native_struct_details)

vegetation = unreal.EditorAssetLibrary.load_asset(VEGETATION)
if vegetation is None or vegetation.get_class().get_name() != "Blueprint":
    fail("PDA_VegetationCalysto did not load as a Blueprint")
unreal.BlueprintEditorLibrary.compile_blueprint(vegetation)
vegetation_status = str(vegetation.get_editor_property("status"))
vegetation_normalized = "".join(c for c in vegetation_status.upper() if c.isalnum())
vegetation_parent = object_path(
    unreal.BlueprintEditorLibrary.get_blueprint_parent_class(vegetation)
)
if "UPTODATE" not in vegetation_normalized:
    fail("PDA_VegetationCalysto is not UP_TO_DATE: " + vegetation_status)
if vegetation_parent != "/Script/Engine.PrimaryDataAsset":
    fail("PDA_VegetationCalysto direct parent changed: " + vegetation_parent)

smart_file = package_file(content_root, SMART)
binary_signals = {
    "current_scatter_class_path_present": contains_ascii(
        smart_file,
        "/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter.PDA_CalystoScatter_C",
    ),
    "current_scatter_default_path_present": contains_ascii(
        smart_file,
        "/Game/Calysto/Shared/Data/DA_ScatterDummy.DA_ScatterDummy",
    ),
    "stale_scatter_class_path_present": contains_ascii(
        smart_file,
        "/Game/Calysto/Scatter/Data/Structure/PDA_CalystoScatter.PDA_CalystoScatter_C",
    ),
    "stale_scatter_default_path_present": contains_ascii(
        smart_file, "/Game/Calysto/Scatter/Data/DA_ScatterDummy.DA_ScatterDummy"
    ),
}

disk_after = {package: snapshot(row["file"]) for package, row in disk_before.items()}
changed_hashes = [
    package
    for package in PACKAGES
    if disk_after[package]["length"] != disk_before[package]["length"]
    or disk_after[package]["sha256"] != disk_before[package]["sha256"]
]
content_after = inventory(content_root)
content_delta = [
    {"relative": relative, "before": content_before.get(relative), "after": content_after.get(relative)}
    for relative in sorted(set(content_before) | set(content_after))
    if content_before.get(relative) != content_after.get(relative)
]
if changed_hashes or content_delta:
    fail("Target Content changed during fresh read-only validation")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_CALYSTO_SMART_SCATTER_FRESH_READ_ONLY_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "repair_evidence": repair_path,
    "repair_evidence_sha256": sha256(repair_path),
    "packages": list(PACKAGES),
    "package_count": 2,
    "repair_after_byte_checks": repair_byte_checks,
    "support_blueprint_compile_status": support_status,
    "scatter_generated_class": object_path(scatter_class),
    "scatter_default_asset": object_path(scatter_default),
    "scatter_mode_enum": object_path(scatter_mode),
    "smart_struct_status": struct_status,
    "smart_struct_error_message": struct_error,
    "native_struct_validation": native_struct_valid,
    "native_struct_details": native_struct_details,
    "vegetation_compile_status": vegetation_status,
    "vegetation_direct_parent": vegetation_parent,
    "binary_signals": binary_signals,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_hashes": changed_hashes,
    "content_delta": content_delta,
    "package_hashes_unchanged": not changed_hashes,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
    "exported_animations_excluded": True,
    "visual_qa_performed": False,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary = evidence_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, evidence_path)
unreal.log("CODEX_CALYSTO_SMART_SCATTER_VALIDATE58_PASS: " + evidence_path)
