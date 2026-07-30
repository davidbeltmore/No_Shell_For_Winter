"""Fresh-process read-only validation of repaired Altar and Locked Blueprints.

Required process environment:
  CODEX_NULL_PARENT_REPAIR_EVIDENCE
  CODEX_NULL_PARENT_VALIDATION_EVIDENCE

Both assets are loaded and compiled in memory, their exact project-owned parent,
generated-class ancestry, required override events, and preserved event links are
verified, and package hashes prove that validation performs no asset save.
"""

import datetime
import hashlib
import json
import os

import unreal


EXPORTED_ANIMATIONS_PREFIX = "/Game/ExportedAnimations"
SPECS = (
    {
        "package": "/Game/Procedural/Blueprints/Altar",
        "parent": "/Script/EFProjectSystemsGameplay.ProjectSinfulAscensionAltar",
        "helper": "altar",
    },
    {
        "package": "/Game/_Game/Lockpicking/Locked",
        "parent": "/Script/EFProjectSystemsGameplay.ProjectLockedWorldItem",
        "helper": "locked",
    },
)
REPAIR_EVIDENCE_VALUE = os.environ.get(
    "CODEX_NULL_PARENT_REPAIR_EVIDENCE", ""
).strip()
VALIDATION_EVIDENCE_VALUE = os.environ.get(
    "CODEX_NULL_PARENT_VALIDATION_EVIDENCE", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_NULL_PARENT_GAMEPLAY_VALIDATE58_FAIL: " + message)
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


def is_exported_animation(package):
    value = str(package).strip().rstrip("/").lower()
    prefix = EXPORTED_ANIMATIONS_PREFIX.lower()
    return value == prefix or value.startswith(prefix + "/")


def package_file(content_root, package):
    if not package.startswith("/Game/") or is_exported_animation(package):
        fail("Invalid or explicitly excluded package: " + package)
    path = os.path.realpath(
        os.path.join(
            content_root,
            package[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )
    if not is_under(path, content_root):
        fail("Package file escapes target Content: " + path)
    return path


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


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def normalized_status(blueprint):
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(
        character for character in status.upper() if character.isalnum()
    )
    return status, normalized


def generated_class(blueprint, package):
    try:
        value = blueprint.get_editor_property("generated_class")
    except Exception:
        value = None
    if value is not None:
        return value
    name = package.rsplit("/", 1)[-1]
    return unreal.load_class(None, package + "." + name + "_C")


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not REPAIR_EVIDENCE_VALUE or not VALIDATION_EVIDENCE_VALUE:
    fail("Repair and validation evidence paths are required")
if len(SPECS) != 2 or any(
    is_exported_animation(spec["package"]) for spec in SPECS
):
    fail("The guarded two-package validation cohort is invalid")

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
for label, path in (
    ("Repair evidence", repair_evidence_path),
    ("Validation evidence", validation_evidence_path),
):
    if not is_under(path, saved_migration_root):
        fail(label + " escapes target Saved/Migration")
if repair_evidence_path.lower() == validation_evidence_path.lower():
    fail("Validation evidence must not overwrite repair evidence")
if not os.path.isfile(repair_evidence_path):
    fail("Null-parent repair evidence is absent")

with open(repair_evidence_path, "r", encoding="utf-8-sig") as handle:
    repair = json.load(handle)
if repair.get("status") != "UE58_NULL_PARENT_GAMEPLAY_BLUEPRINT_REPAIR_PASS":
    fail("Null-parent repair evidence status is not PASS")
if os.path.realpath(str(repair.get("project", ""))).lower() != project_file.lower():
    fail("Null-parent repair evidence belongs to a different project")
if repair.get("saved_only_selected_packages") is not True:
    fail("Null-parent repair exact save-set invariant is not PASS")
if repair.get("override_event_links_preserved") is not True:
    fail("Null-parent repair event-link invariant is not PASS")
if repair.get("exported_animations_excluded") is not True:
    fail("Null-parent repair did not explicitly exclude ExportedAnimations")

packages = [spec["package"] for spec in SPECS]
if repair.get("packages") != packages or repair.get("save_operations") != packages:
    fail("Null-parent repair evidence package/save set differs")
repair_rows = {
    str(row.get("package", "")): row for row in repair.get("repairs", [])
}
repair_after = repair.get("package_files_after", {})
if set(repair_rows) != set(packages) or set(repair_after) != set(packages):
    fail("Null-parent repair evidence rows/snapshots are not exact")

disk_before = {}
for package in packages:
    current = snapshot(package_file(content_root, package))
    recorded = repair_after[package]
    if (
        current["length"] != recorded.get("length")
        or current["sha256"] != str(recorded.get("sha256", "")).upper()
    ):
        fail("Current package bytes differ from repaired evidence: " + package)
    disk_before[package] = current

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    ["/Game/Procedural/Blueprints", "/Game/_Game/Lockpicking"], True
)
registry.wait_for_completion()

validation_rows = []
failures = []
for spec in SPECS:
    package = spec["package"]
    parent_class = unreal.load_class(None, spec["parent"])
    blueprint = unreal.EditorAssetLibrary.load_asset(package)
    if parent_class is None or object_path(parent_class) != spec["parent"]:
        failures.append({"package": package, "reason": "PARENT_CLASS_LOAD_FAILED"})
        continue
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        failures.append({"package": package, "reason": "BLUEPRINT_LOAD_FAILED"})
        continue

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    status, normalized = normalized_status(blueprint)
    direct_parent = object_path(
        unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
    )
    generated = generated_class(blueprint, package)
    ancestry = generated is not None and unreal.MathLibrary.class_is_child_of(
        generated, parent_class
    )
    event_links = list(
        unreal.ProjectNullParentBlueprintRepairLibrary.describe_override_event_links(
            blueprint
        )
    )
    if spec["helper"] == "altar":
        helper_valid = bool(
            unreal.ProjectNullParentBlueprintRepairLibrary.validate_sinful_ascension_altar_repair(
                blueprint
            )
        )
    else:
        helper_valid = bool(
            unreal.ProjectNullParentBlueprintRepairLibrary.validate_locked_world_item_repair(
                blueprint
            )
        )

    row = {
        "package": package,
        "expected_parent": spec["parent"],
        "direct_parent": direct_parent,
        "compile_status": status,
        "generated_class": object_path(generated),
        "generated_class_is_child_of_target_parent": bool(ancestry),
        "project_editor_helper_validated": helper_valid,
        "override_event_links": event_links,
        "override_event_links_match_repair": (
            event_links == repair_rows[package].get("override_event_links_after")
        ),
    }
    validation_rows.append(row)
    if direct_parent != spec["parent"]:
        failures.append({"package": package, "reason": "DIRECT_PARENT_MISMATCH"})
    if "UPTODATE" not in normalized:
        failures.append(
            {
                "package": package,
                "reason": "BLUEPRINT_NOT_UP_TO_DATE",
                "status": status,
            }
        )
    if not ancestry:
        failures.append(
            {"package": package, "reason": "GENERATED_CLASS_ANCESTRY_MISMATCH"}
        )
    if not helper_valid:
        failures.append(
            {"package": package, "reason": "EDITOR_HELPER_VALIDATION_FAILED"}
        )
    if not row["override_event_links_match_repair"]:
        failures.append(
            {"package": package, "reason": "OVERRIDE_EVENT_LINKS_CHANGED"}
        )

disk_after = {
    package: snapshot(package_file(content_root, package)) for package in packages
}
changed_package_files = [
    package for package in packages if disk_after[package] != disk_before[package]
]
for package in changed_package_files:
    failures.append(
        {"package": package, "reason": "PACKAGE_CHANGED_DURING_VALIDATION"}
    )

passed = (
    len(validation_rows) == 2
    and not failures
    and not changed_package_files
)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_NULL_PARENT_GAMEPLAY_BLUEPRINT_FRESH_READ_ONLY_PASS"
        if passed
        else "UE58_NULL_PARENT_GAMEPLAY_BLUEPRINT_FRESH_READ_ONLY_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "repair_evidence": repair_evidence_path,
    "repair_evidence_sha256": sha256(repair_evidence_path),
    "package_count": len(packages),
    "packages": packages,
    "validations": validation_rows,
    "failures": failures,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_files": changed_package_files,
    "package_files_unchanged": not changed_package_files,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
    "exported_animations_excluded": True,
}
os.makedirs(os.path.dirname(validation_evidence_path), exist_ok=True)
temporary_path = validation_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, validation_evidence_path)

if not passed:
    fail(
        "Fresh read-only validation failed: validated={}/2 failures={} changed={}".format(
            len(validation_rows), len(failures), len(changed_package_files)
        )
    )
unreal.log("CODEX_NULL_PARENT_GAMEPLAY_VALIDATE58_PASS: " + validation_evidence_path)
