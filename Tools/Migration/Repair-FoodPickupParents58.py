"""Repair the UE 5.8 parent of the failed FoodSystem pickup Blueprints.

Required process environment:
  CODEX_BULK_VALIDATION_EVIDENCE
  CODEX_FOOD_PARENT_REPAIR_EVIDENCE

Optional process environment:
  CODEX_FOOD_PARENT_EXPECTED_COUNT (default: 81)

The input is the read-only bulk validation JSON.  Selection is deliberately
restricted to BLUEPRINT_NOT_UP_TO_DATE failures below FOOD_PICKUP_PREFIX.  The
script reparents only Blueprints whose current parent is null, compiles all
selected Blueprints, and saves exactly the selected package set.
"""

import datetime
import hashlib
import json
import os

import unreal


FOOD_PICKUP_PREFIX = "/Game/_Game/FoodSystem/Food/Items/PickuableItems/"
PARENT_CLASS_PATH = (
    "/AscentCombatFramework/Blueprints/Actors/"
    "ACF_WorldItem_BP.ACF_WorldItem_BP_C"
)
SOURCE_EVIDENCE_VALUE = os.environ.get(
    "CODEX_BULK_VALIDATION_EVIDENCE", ""
).strip()
REPAIR_EVIDENCE_VALUE = (
    os.environ.get("CODEX_FOOD_PARENT_REPAIR_EVIDENCE", "").strip()
    or os.environ.get("CODEX_FOOD_PICKUP_REPAIR_EVIDENCE", "").strip()
)
EXPECTED_COUNT_VALUE = os.environ.get(
    "CODEX_FOOD_PARENT_EXPECTED_COUNT",
    os.environ.get("CODEX_FOOD_PICKUP_EXPECTED_COUNT", "81"),
).strip()


def fail(message):
    unreal.log_error("CODEX_FOOD_PICKUP_PARENT_REPAIR58_FAIL: " + message)
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


def package_file(content_root, package_name):
    if not package_name.startswith(FOOD_PICKUP_PREFIX):
        fail("Selected package escapes the Food pickup prefix: " + package_name)
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def snapshot(path):
    if not os.path.isfile(path):
        fail("Selected Blueprint file is absent: " + path)
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


def blueprint_parent(blueprint):
    try:
        return unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
    except Exception as exc:
        fail("Could not query Blueprint parent class: " + repr(exc))


def normalized_status(blueprint):
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(
        character for character in status.upper() if character.isalnum()
    )
    return status, normalized


def generated_class(blueprint, package_name):
    try:
        value = blueprint.get_editor_property("generated_class")
    except Exception:
        value = None
    if value is not None:
        return value
    asset_name = package_name.rsplit("/", 1)[-1]
    return unreal.load_class(None, package_name + "." + asset_name + "_C")


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not SOURCE_EVIDENCE_VALUE or not REPAIR_EVIDENCE_VALUE:
    fail(
        "CODEX_BULK_VALIDATION_EVIDENCE and "
        "CODEX_FOOD_PARENT_REPAIR_EVIDENCE are required"
    )
try:
    expected_count = int(EXPECTED_COUNT_VALUE)
except ValueError:
    fail("CODEX_FOOD_PARENT_EXPECTED_COUNT is not an integer")
if expected_count != 81:
    fail("The guarded Food pickup repair contract requires exactly 81 packages")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_migration_root = os.path.realpath(
    os.path.join(project_root, "Saved", "Migration")
)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if content_root.lower() != os.path.realpath(
    os.path.join(project_root, "Content")
).lower():
    fail("Target Content invariant failed")

source_evidence_path = os.path.realpath(SOURCE_EVIDENCE_VALUE)
repair_evidence_path = os.path.realpath(REPAIR_EVIDENCE_VALUE)
if not is_under(source_evidence_path, saved_migration_root):
    fail("Bulk validation evidence escapes target Saved/Migration")
if not is_under(repair_evidence_path, saved_migration_root):
    fail("Repair evidence escapes target Saved/Migration")
if source_evidence_path.lower() == repair_evidence_path.lower():
    fail("Repair evidence must not overwrite bulk validation evidence")
if not os.path.isfile(source_evidence_path):
    fail("BulkProjectContent58Validation.json is absent")

with open(source_evidence_path, "r", encoding="utf-8-sig") as handle:
    source_evidence = json.load(handle)
source_project = os.path.realpath(str(source_evidence.get("project", "")))
if source_project.lower() != project_file.lower():
    fail("Bulk validation evidence belongs to a different project")

matching_rows = [
    row
    for row in source_evidence.get("failed", [])
    if str(row.get("reason", "")) == "BLUEPRINT_NOT_UP_TO_DATE"
    and str(row.get("package", "")).startswith(FOOD_PICKUP_PREFIX)
]
packages = [str(row.get("package", "")).strip() for row in matching_rows]
if len(packages) != expected_count:
    fail(
        "Bulk validation selected {} Food pickup failures, expected {}".format(
            len(packages), expected_count
        )
    )
if len(set(packages)) != expected_count:
    fail("Bulk validation contains duplicate Food pickup failure rows")
packages = sorted(packages)

parent_class = unreal.load_class(None, PARENT_CLASS_PATH)
if parent_class is None:
    fail("Current ACF_WorldItem_BP_C parent class could not load")
if object_path(parent_class) != PARENT_CLASS_PATH:
    fail("Loaded ACF_WorldItem parent path differs: " + object_path(parent_class))
if not hasattr(unreal.BlueprintEditorLibrary, "reparent_blueprint"):
    fail("BlueprintEditorLibrary.reparent_blueprint is unavailable")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([FOOD_PICKUP_PREFIX.rstrip("/")], True)
registry.wait_for_completion()

disk_before = {
    package: snapshot(package_file(content_root, package)) for package in packages
}
repair_rows = []
loaded_blueprints = {}
for package in packages:
    registry_rows = list(
        registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
    )
    if len(registry_rows) != 1:
        fail(
            "Expected exactly one on-disk asset row for {} but found {}".format(
                package, len(registry_rows)
            )
        )
    blueprint = unreal.EditorAssetLibrary.load_asset(package)
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        fail("Selected package did not load as Blueprint: " + package)

    parent_before_object = blueprint_parent(blueprint)
    parent_before = object_path(parent_before_object)
    reparented = False
    if parent_before_object is None:
        try:
            result = unreal.BlueprintEditorLibrary.reparent_blueprint(
                blueprint, parent_class
            )
        except Exception as exc:
            fail("Reparent failed for {}: {}".format(package, repr(exc)))
        if result is False:
            fail("Reparent returned False for " + package)
        reparented = True
    elif parent_before != PARENT_CLASS_PATH:
        fail(
            "Refusing to replace non-null parent on {}: {}".format(
                package, parent_before
            )
        )

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    compile_status, normalized = normalized_status(blueprint)
    parent_after = object_path(blueprint_parent(blueprint))
    generated = generated_class(blueprint, package)
    if parent_after != PARENT_CLASS_PATH:
        fail("Direct parent repair did not stick for {}: {}".format(package, parent_after))
    if generated is None or not unreal.MathLibrary.class_is_child_of(
        generated, parent_class
    ):
        fail("Generated class ancestry is invalid for " + package)
    if "UPTODATE" not in normalized:
        fail("Blueprint did not compile UP_TO_DATE: {} ({})".format(package, compile_status))

    loaded_blueprints[package] = blueprint
    repair_rows.append(
        {
            "package": package,
            "parent_before": parent_before,
            "parent_after": parent_after,
            "reparented_from_null": reparented,
            "compile_status": compile_status,
            "generated_class": object_path(generated),
            "generated_class_is_child_of_expected_parent": True,
        }
    )

saved_packages = []
for package in packages:
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("Failed to save repaired Blueprint: " + package)
    saved_packages.append(package)

if saved_packages != packages or len(saved_packages) != expected_count:
    fail("The exact save-set invariant failed")
disk_after = {
    package: snapshot(package_file(content_root, package)) for package in packages
}

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_FOOD_PICKUP_PARENT_REPAIR_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "source_bulk_validation_evidence": source_evidence_path,
    "source_bulk_validation_evidence_sha256": sha256(source_evidence_path),
    "selection_reason": "BLUEPRINT_NOT_UP_TO_DATE",
    "selection_prefix": FOOD_PICKUP_PREFIX,
    "expected_count": expected_count,
    "package_count": len(packages),
    "expected_parent_class": PARENT_CLASS_PATH,
    "loaded_current_parent_class": object_path(parent_class),
    "reparent_only_when_parent_null": True,
    "reparented_count": sum(1 for row in repair_rows if row["reparented_from_null"]),
    "compiled_count": len(repair_rows),
    "saved_count": len(saved_packages),
    "packages": packages,
    "repairs": repair_rows,
    "save_operations": saved_packages,
    "saved_only_selected_packages": saved_packages == packages,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "backup_performed_by_script": False,
}
os.makedirs(os.path.dirname(repair_evidence_path), exist_ok=True)
temporary_path = repair_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, repair_evidence_path)
unreal.log("CODEX_FOOD_PICKUP_PARENT_REPAIR58_PASS: " + repair_evidence_path)
