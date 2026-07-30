"""Fresh-process read-only validation of repaired UE 5.8 Food pickups.

Required process environment:
  CODEX_FOOD_PARENT_REPAIR_EVIDENCE
  CODEX_FOOD_PARENT_VALIDATION_EVIDENCE

Optional process environment:
  CODEX_FOOD_PARENT_EXPECTED_COUNT (default: 81)

The script loads and compiles all repaired Blueprints, verifies their direct
parent and generated-class ancestry, and proves that validation saved nothing.
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
REPAIR_EVIDENCE_VALUE = (
    os.environ.get("CODEX_FOOD_PARENT_REPAIR_EVIDENCE", "").strip()
    or os.environ.get("CODEX_FOOD_PICKUP_REPAIR_EVIDENCE", "").strip()
)
VALIDATION_EVIDENCE_VALUE = (
    os.environ.get("CODEX_FOOD_PARENT_VALIDATION_EVIDENCE", "").strip()
    or os.environ.get("CODEX_FOOD_PICKUP_VALIDATION_EVIDENCE", "").strip()
)
EXPECTED_COUNT_VALUE = os.environ.get(
    "CODEX_FOOD_PARENT_EXPECTED_COUNT",
    os.environ.get("CODEX_FOOD_PICKUP_EXPECTED_COUNT", "81"),
).strip()


def fail(message):
    unreal.log_error("CODEX_FOOD_PICKUP_PARENT_VALIDATE58_FAIL: " + message)
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
        fail("Repair evidence package escapes the Food pickup prefix: " + package_name)
    return os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def snapshot(path):
    if not os.path.isfile(path):
        fail("Repaired Blueprint file is absent: " + path)
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


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


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
if not REPAIR_EVIDENCE_VALUE or not VALIDATION_EVIDENCE_VALUE:
    fail(
        "CODEX_FOOD_PARENT_REPAIR_EVIDENCE and "
        "CODEX_FOOD_PARENT_VALIDATION_EVIDENCE are required"
    )
try:
    expected_count = int(EXPECTED_COUNT_VALUE)
except ValueError:
    fail("CODEX_FOOD_PARENT_EXPECTED_COUNT is not an integer")
if expected_count != 81:
    fail("The guarded Food pickup validation contract requires exactly 81 packages")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_migration_root = os.path.realpath(
    os.path.join(project_root, "Saved", "Migration")
)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")

repair_evidence_path = os.path.realpath(REPAIR_EVIDENCE_VALUE)
validation_evidence_path = os.path.realpath(VALIDATION_EVIDENCE_VALUE)
if not is_under(repair_evidence_path, saved_migration_root):
    fail("Repair evidence escapes target Saved/Migration")
if not is_under(validation_evidence_path, saved_migration_root):
    fail("Validation evidence escapes target Saved/Migration")
if repair_evidence_path.lower() == validation_evidence_path.lower():
    fail("Validation evidence must not overwrite repair evidence")
if not os.path.isfile(repair_evidence_path):
    fail("Food pickup repair evidence is absent")

with open(repair_evidence_path, "r", encoding="utf-8-sig") as handle:
    repair_evidence = json.load(handle)
if repair_evidence.get("status") != "UE58_FOOD_PICKUP_PARENT_REPAIR_PASS":
    fail("Food pickup repair evidence status is not PASS")
if os.path.realpath(str(repair_evidence.get("project", ""))).lower() != project_file.lower():
    fail("Food pickup repair evidence belongs to a different project")
if repair_evidence.get("saved_only_selected_packages") is not True:
    fail("Food pickup repair exact save-set invariant is not PASS")

packages = [str(value).strip() for value in repair_evidence.get("packages", [])]
if len(packages) != expected_count or len(set(packages)) != expected_count:
    fail("Food pickup repair evidence does not contain exactly 81 unique packages")
if packages != sorted(packages):
    fail("Food pickup repair package list is not deterministically sorted")
if any(not package.startswith(FOOD_PICKUP_PREFIX) for package in packages):
    fail("Food pickup repair evidence contains an out-of-scope package")
if repair_evidence.get("save_operations") != packages:
    fail("Food pickup repair evidence save set differs from package set")

parent_class = unreal.load_class(None, PARENT_CLASS_PATH)
if parent_class is None or object_path(parent_class) != PARENT_CLASS_PATH:
    fail("Current ACF_WorldItem_BP_C parent class could not load exactly")

disk_before = {
    package: snapshot(package_file(content_root, package)) for package in packages
}
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([FOOD_PICKUP_PREFIX.rstrip("/")], True)
registry.wait_for_completion()

validation_rows = []
failures = []
for package in packages:
    registry_rows = list(
        registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
    )
    registry_classes = sorted({class_name(row) for row in registry_rows})
    if len(registry_rows) != 1 or "Blueprint" not in registry_classes:
        failures.append(
            {
                "package": package,
                "reason": "ON_DISK_BLUEPRINT_ROW_INVALID",
                "registry_classes": registry_classes,
                "registry_row_count": len(registry_rows),
            }
        )
        continue
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(package)
    except Exception as exc:
        blueprint = None
        load_error = repr(exc)
    else:
        load_error = ""
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        failures.append(
            {
                "package": package,
                "reason": "BLUEPRINT_LOAD_FAILED",
                "error": load_error,
            }
        )
        continue

    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        compile_status, normalized = normalized_status(blueprint)
        direct_parent = object_path(
            unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
        )
        generated = generated_class(blueprint, package)
        ancestry_pass = generated is not None and unreal.MathLibrary.class_is_child_of(
            generated, parent_class
        )
    except Exception as exc:
        failures.append(
            {
                "package": package,
                "reason": "BLUEPRINT_VALIDATE_EXCEPTION",
                "error": repr(exc),
            }
        )
        continue

    row = {
        "package": package,
        "compile_status": compile_status,
        "direct_parent_class": direct_parent,
        "expected_parent_class": PARENT_CLASS_PATH,
        "generated_class": object_path(generated),
        "generated_class_is_child_of_expected_parent": bool(ancestry_pass),
    }
    validation_rows.append(row)
    if "UPTODATE" not in normalized:
        failures.append(
            {
                "package": package,
                "reason": "BLUEPRINT_NOT_UP_TO_DATE",
                "status": compile_status,
            }
        )
    if direct_parent != PARENT_CLASS_PATH:
        failures.append(
            {
                "package": package,
                "reason": "DIRECT_PARENT_CLASS_MISMATCH",
                "actual": direct_parent,
            }
        )
    if not ancestry_pass:
        failures.append(
            {
                "package": package,
                "reason": "GENERATED_CLASS_ANCESTRY_MISMATCH",
                "generated_class": object_path(generated),
            }
        )

disk_after = {
    package: snapshot(package_file(content_root, package)) for package in packages
}
changed_package_files = [
    package for package in packages if disk_after[package] != disk_before[package]
]
for package in changed_package_files:
    failures.append(
        {"package": package, "reason": "PACKAGE_FILE_CHANGED_DURING_VALIDATION"}
    )

passed = (
    len(validation_rows) == expected_count
    and not failures
    and not changed_package_files
)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_FOOD_PICKUP_PARENT_FRESH_READ_ONLY_PASS"
        if passed
        else "UE58_FOOD_PICKUP_PARENT_FRESH_READ_ONLY_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "repair_evidence": repair_evidence_path,
    "repair_evidence_sha256": sha256(repair_evidence_path),
    "expected_count": expected_count,
    "package_count": len(packages),
    "loaded_compiled_validated_count": len(validation_rows),
    "expected_parent_class": PARENT_CLASS_PATH,
    "all_direct_parents_match": all(
        row["direct_parent_class"] == PARENT_CLASS_PATH for row in validation_rows
    ) and len(validation_rows) == expected_count,
    "all_generated_classes_have_expected_ancestry": all(
        row["generated_class_is_child_of_expected_parent"]
        for row in validation_rows
    ) and len(validation_rows) == expected_count,
    "validations": validation_rows,
    "failures": failures,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_files": changed_package_files,
    "package_files_unchanged": not changed_package_files,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
}
os.makedirs(os.path.dirname(validation_evidence_path), exist_ok=True)
temporary_path = validation_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, validation_evidence_path)

if not passed:
    fail(
        "Fresh read-only validation failed: validated={}/{}, failures={}, changed={}".format(
            len(validation_rows), expected_count, len(failures), len(changed_package_files)
        )
    )
unreal.log("CODEX_FOOD_PICKUP_PARENT_VALIDATE58_PASS: " + validation_evidence_path)
