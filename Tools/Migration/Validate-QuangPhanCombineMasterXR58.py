"""Read-only UE 5.8 validation of BP_Combine_Master's XRBase node.

Required process environment:
  CODEX_QUANGPHAN_XR_VALIDATION_EVIDENCE

Optional process environment:
  CODEX_QUANGPHAN_XR_REQUIRE_RESOLVED (default: 1)

The validator checks the actual enabled-plugin set, resolves
UHeadMountedDisplayFunctionLibrary.ResetOrientationAndPosition from XRBase,
loads and compiles only BP_Combine_Master, and proves that neither the Blueprint
nor the project descriptor changed on disk.  It never invokes a save API or the
XR reset function.
"""

import datetime
import hashlib
import json
import os

import unreal


BLUEPRINT_PACKAGE = (
    "/Game/QuangPhan/G2_HairCard_01/Demo/Blueprints/"
    "BP_Examples/BP_Combine_Master"
)
XRBASE_CLASS_PATH = "/Script/XRBase.HeadMountedDisplayFunctionLibrary"
XRBASE_PYTHON_CLASS = "HeadMountedDisplayFunctionLibrary"
XRBASE_PYTHON_FUNCTION = "reset_orientation_and_position"
EXCLUDED_PREFIX = "/Game/ExportedAnimations"
EVIDENCE_VALUE = os.environ.get(
    "CODEX_QUANGPHAN_XR_VALIDATION_EVIDENCE", ""
).strip()
REQUIRE_RESOLVED_VALUE = os.environ.get(
    "CODEX_QUANGPHAN_XR_REQUIRE_RESOLVED", "1"
).strip().lower()


def hard_fail(message):
    unreal.log_error("CODEX_QUANGPHAN_XR_VALIDATE58_FAIL: " + message)
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


def snapshot(path):
    if not os.path.isfile(path):
        return None
    stat = os.stat(path)
    return {
        "file": path,
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


def normalized_blueprint_status(blueprint):
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(
        character for character in status.upper() if character.isalnum()
    )
    return status, normalized


def explicit_plugin_entry(project_descriptor, plugin_name):
    matches = [
        entry
        for entry in project_descriptor.get("Plugins", [])
        if str(entry.get("Name", "")) == plugin_name
    ]
    if len(matches) > 1:
        hard_fail("Duplicate {} entries in .uproject".format(plugin_name))
    return matches[0] if matches else None


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    hard_fail("Expected UE 5.8 but found " + engine_version)
if not EVIDENCE_VALUE:
    hard_fail("CODEX_QUANGPHAN_XR_VALIDATION_EVIDENCE is required")
if REQUIRE_RESOLVED_VALUE not in ("0", "1", "false", "true", "no", "yes"):
    hard_fail("CODEX_QUANGPHAN_XR_REQUIRE_RESOLVED must be boolean-like")
require_resolved = REQUIRE_RESOLVED_VALUE in ("1", "true", "yes")
if BLUEPRINT_PACKAGE.startswith(EXCLUDED_PREFIX):
    hard_fail("Guarded package unexpectedly overlaps ExportedAnimations")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_migration_root = os.path.realpath(
    os.path.join(project_root, "Saved", "Migration")
)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    hard_fail("Commandlet is not running against NoShellForWinter.uproject")
if content_root.lower() != os.path.realpath(
    os.path.join(project_root, "Content")
).lower():
    hard_fail("Target Content invariant failed")

evidence_path = os.path.realpath(EVIDENCE_VALUE)
if not is_under(evidence_path, saved_migration_root):
    hard_fail("Validation evidence escapes target Saved/Migration")
if os.path.splitext(evidence_path)[1].lower() != ".json":
    hard_fail("Validation evidence must be a JSON file")

blueprint_file = os.path.realpath(
    os.path.join(
        content_root,
        BLUEPRINT_PACKAGE[len("/Game/") :].replace("/", os.sep) + ".uasset",
    )
)
if not is_under(blueprint_file, content_root):
    hard_fail("Blueprint file escapes target Content")

project_before = snapshot(project_file)
blueprint_before = snapshot(blueprint_file)
if project_before is None:
    hard_fail("NoShellForWinter.uproject is absent")
if blueprint_before is None:
    hard_fail("BP_Combine_Master.uasset is absent")

with open(project_file, "r", encoding="utf-8-sig") as handle:
    project_descriptor = json.load(handle)
openxr_entry = explicit_plugin_entry(project_descriptor, "OpenXR")
xrbase_entry = explicit_plugin_entry(project_descriptor, "XRBase")
openxr_explicit_enabled = bool(
    openxr_entry is not None and openxr_entry.get("Enabled", False)
)
xrbase_explicit_enabled = bool(
    xrbase_entry is not None and xrbase_entry.get("Enabled", False)
)

enabled_plugins = sorted(
    str(name) for name in unreal.PluginBlueprintLibrary.get_enabled_plugin_names()
)
openxr_runtime_enabled = "OpenXR" in enabled_plugins
xrbase_runtime_enabled = "XRBase" in enabled_plugins

failures = []
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_files_synchronous([blueprint_file], True)
registry.wait_for_completion()
registry_rows = list(
    registry.get_assets_by_package_name(
        BLUEPRINT_PACKAGE, include_only_on_disk_assets=True
    )
)
registry_classes = []
for row in registry_rows:
    try:
        registry_classes.append(str(row.asset_class_path.asset_name))
    except Exception:
        registry_classes.append(str(row.asset_class))
if len(registry_rows) != 1 or registry_classes != ["Blueprint"]:
    failures.append(
        "Asset Registry expected one Blueprint row but found {} ({})".format(
            len(registry_rows), registry_classes
        )
    )

xrbase_library_class = unreal.load_class(None, XRBASE_CLASS_PATH)
xrbase_library_class_path = object_path(xrbase_library_class)
xrbase_class_loaded = xrbase_library_class is not None
python_library = getattr(unreal, XRBASE_PYTHON_CLASS, None)
python_function = (
    getattr(python_library, XRBASE_PYTHON_FUNCTION, None)
    if python_library is not None
    else None
)
reset_function_exposed = callable(python_function)
if not xrbase_runtime_enabled:
    failures.append("XRBase is not enabled in the active plugin set")
if not xrbase_class_loaded:
    failures.append("UHeadMountedDisplayFunctionLibrary could not load from XRBase")
elif xrbase_library_class_path != XRBASE_CLASS_PATH:
    failures.append(
        "XRBase function-library class path differs: " + xrbase_library_class_path
    )
if not reset_function_exposed:
    failures.append(
        "ResetOrientationAndPosition is not exposed by " + XRBASE_CLASS_PATH
    )

blueprint = unreal.EditorAssetLibrary.load_asset(BLUEPRINT_PACKAGE)
blueprint_loaded = blueprint is not None
blueprint_class = blueprint.get_class().get_name() if blueprint_loaded else ""
status_before = ""
status_after = ""
compile_up_to_date = False
if not blueprint_loaded or blueprint_class != "Blueprint":
    failures.append("BP_Combine_Master did not load as Blueprint")
else:
    status_before, _ = normalized_blueprint_status(blueprint)
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status_after, normalized_after = normalized_blueprint_status(blueprint)
        compile_up_to_date = "UPTODATE" in normalized_after
    except Exception as exc:
        status_after = "COMPILE_EXCEPTION: " + repr(exc)
    if not compile_up_to_date:
        failures.append("BP_Combine_Master did not compile UP_TO_DATE: " + status_after)

project_after = snapshot(project_file)
blueprint_after = snapshot(blueprint_file)
project_unchanged = project_before == project_after
blueprint_unchanged = blueprint_before == blueprint_after
if not project_unchanged:
    failures.append("Read-only validation changed NoShellForWinter.uproject")
if not blueprint_unchanged:
    failures.append("Read-only validation changed BP_Combine_Master.uasset")

xrbase_reset_resolved = bool(
    xrbase_runtime_enabled
    and xrbase_class_loaded
    and xrbase_library_class_path == XRBASE_CLASS_PATH
    and reset_function_exposed
    and compile_up_to_date
)
status = (
    "UE58_QUANGPHAN_COMBINE_MASTER_XRBASE_RESOLVED_PASS"
    if xrbase_reset_resolved and not failures
    else "UE58_QUANGPHAN_COMBINE_MASTER_XRBASE_UNRESOLVED"
)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": status,
    "engine_version": engine_version,
    "project": project_file,
    "blueprint_package": BLUEPRINT_PACKAGE,
    "blueprint_file": blueprint_file,
    "excluded_prefix": EXCLUDED_PREFIX,
    "registry_row_count": len(registry_rows),
    "registry_classes": registry_classes,
    "project_plugin_entries": {
        "OpenXR": openxr_entry,
        "XRBase": xrbase_entry,
    },
    "project_openxr_explicit_enabled": openxr_explicit_enabled,
    "project_xrbase_explicit_enabled": xrbase_explicit_enabled,
    "active_openxr_enabled": openxr_runtime_enabled,
    "active_xrbase_enabled": xrbase_runtime_enabled,
    "xrbase_enabled_transitively": bool(
        xrbase_runtime_enabled and not xrbase_explicit_enabled
    ),
    "xrbase_function_library_class_requested": XRBASE_CLASS_PATH,
    "xrbase_function_library_class_loaded": xrbase_class_loaded,
    "xrbase_function_library_class_actual": xrbase_library_class_path,
    "reset_orientation_and_position_python_callable": reset_function_exposed,
    "reset_orientation_and_position_invoked": False,
    "blueprint_loaded": blueprint_loaded,
    "blueprint_class": blueprint_class,
    "blueprint_status_before_compile": status_before,
    "blueprint_status_after_compile": status_after,
    "blueprint_compile_up_to_date": compile_up_to_date,
    "reset_orientation_and_position_xrbase_resolved": xrbase_reset_resolved,
    "project_file_before": project_before,
    "project_file_after": project_after,
    "project_file_unchanged": project_unchanged,
    "blueprint_file_before": blueprint_before,
    "blueprint_file_after": blueprint_after,
    "blueprint_file_unchanged": blueprint_unchanged,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
    "failures": failures,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary_path = evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, evidence_path)

if status.endswith("PASS"):
    unreal.log("CODEX_QUANGPHAN_XR_VALIDATE58_PASS: " + evidence_path)
elif require_resolved:
    hard_fail(
        "ResetOrientationAndPosition/XRBase remains unresolved; evidence: "
        + evidence_path
        + "; failures="
        + repr(failures)
    )
else:
    unreal.log_warning(
        "CODEX_QUANGPHAN_XR_VALIDATE58_UNRESOLVED: " + evidence_path
    )
