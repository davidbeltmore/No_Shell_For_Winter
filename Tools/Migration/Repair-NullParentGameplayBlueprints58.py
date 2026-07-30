"""Repair exactly Altar and Locked after their UE 5.7 parents serialized null.

Required process environment:
  CODEX_BULK_MIGRATION_EVIDENCE
  CODEX_NULL_PARENT_BACKUP_EVIDENCE
  CODEX_NULL_PARENT_REPAIR_EVIDENCE

This script is fail-closed: both target files must still match the UE 5.7 bulk
migration and the detached backup, both Blueprint parents must be null, and all
override-event links must survive the editor-only targeted repair unchanged.
Only the exact two selected packages are saved. ExportedAnimations is forbidden.
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
        "required_events": ("OnInteractedByPawn",),
    },
    {
        "package": "/Game/_Game/Lockpicking/Locked",
        "parent": "/Script/EFProjectSystemsGameplay.ProjectLockedWorldItem",
        "helper": "locked",
        "required_events": ("OnInteractedByPawn", "OnLocalInteractedByPawn"),
    },
)

MIGRATION_EVIDENCE_VALUE = (
    os.environ.get("CODEX_BULK_MIGRATION_EVIDENCE", "").strip()
    or os.environ.get("CODEX_BULK_EVIDENCE", "").strip()
)
BACKUP_EVIDENCE_VALUE = os.environ.get(
    "CODEX_NULL_PARENT_BACKUP_EVIDENCE", ""
).strip()
REPAIR_EVIDENCE_VALUE = os.environ.get(
    "CODEX_NULL_PARENT_REPAIR_EVIDENCE", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_NULL_PARENT_GAMEPLAY_REPAIR58_FAIL: " + message)
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
    if not package.startswith("/Game/"):
        fail("Selected package is not rooted at /Game: " + package)
    if is_exported_animation(package):
        fail("ExportedAnimations is explicitly excluded: " + package)
    path = os.path.realpath(
        os.path.join(
            content_root,
            package[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )
    if not is_under(path, content_root):
        fail("Selected package file escapes target Content: " + path)
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


def validate_snapshot_record(record, expected_path, label):
    if not isinstance(record, dict):
        fail(label + " is not a snapshot object")
    recorded_path = os.path.realpath(str(record.get("file", "")))
    if recorded_path.lower() != expected_path.lower():
        fail(label + " path differs from the selected target file")
    length = record.get("length")
    digest = str(record.get("sha256", "")).upper()
    if not isinstance(length, int) or length < 0 or len(digest) != 64:
        fail(label + " contains invalid byte metadata")
    return {"file": recorded_path, "length": length, "sha256": digest}


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if len(SPECS) != 2 or len({spec["package"] for spec in SPECS}) != 2:
    fail("The guarded null-parent repair cohort must contain exactly two packages")
if any(is_exported_animation(spec["package"]) for spec in SPECS):
    fail("The guarded null-parent cohort overlaps ExportedAnimations")
if not all(
    (
        MIGRATION_EVIDENCE_VALUE,
        BACKUP_EVIDENCE_VALUE,
        REPAIR_EVIDENCE_VALUE,
    )
):
    fail("Migration, backup, and repair evidence paths are required")

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

migration_evidence_path = os.path.realpath(MIGRATION_EVIDENCE_VALUE)
backup_evidence_path = os.path.realpath(BACKUP_EVIDENCE_VALUE)
repair_evidence_path = os.path.realpath(REPAIR_EVIDENCE_VALUE)
for label, path in (
    ("Migration evidence", migration_evidence_path),
    ("Backup evidence", backup_evidence_path),
    ("Repair evidence", repair_evidence_path),
):
    if not is_under(path, saved_migration_root):
        fail(label + " escapes target Saved/Migration")
if len(
    {
        migration_evidence_path.lower(),
        backup_evidence_path.lower(),
        repair_evidence_path.lower(),
    }
) != 3:
    fail("Input and output evidence paths must be distinct")
if not os.path.isfile(migration_evidence_path):
    fail("UE 5.7 bulk migration evidence is absent")
if not os.path.isfile(backup_evidence_path):
    fail("Null-parent backup evidence is absent")
if os.path.exists(repair_evidence_path):
    fail("Repair evidence already exists; refusing an ambiguous rerun")

with open(migration_evidence_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if migration.get("status") != "ASSETTOOLS_EXACT_BULK_PROJECT_CONTENT57_PASS":
    fail("UE 5.7 bulk migration evidence status is not PASS")
if os.path.realpath(str(migration.get("target_root", ""))).lower() != project_root.lower():
    fail("UE 5.7 bulk migration evidence belongs to a different target")
migration_by_package = {
    str(row.get("package", "")): row for row in migration.get("packages", [])
}

with open(backup_evidence_path, "r", encoding="utf-8-sig") as handle:
    backup = json.load(handle)
if backup.get("status") != "NULL_PARENT_GAMEPLAY_BLUEPRINT_BACKUP_PASS":
    fail("Null-parent backup evidence status is not PASS")
if os.path.realpath(str(backup.get("project", ""))).lower() != project_file.lower():
    fail("Null-parent backup evidence belongs to a different project")
if backup.get("exported_animations_excluded") is not True:
    fail("Backup evidence does not explicitly exclude ExportedAnimations")

packages = [spec["package"] for spec in SPECS]
if backup.get("packages") != packages or backup.get("package_count") != 2:
    fail("Backup evidence package cohort differs from the exact two targets")
backup_rows = list(backup.get("files", []))
if len(backup_rows) != 2:
    fail("Backup evidence does not contain exactly two file rows")
backup_by_package = {
    str(row.get("package", "")): row for row in backup_rows
}
if set(backup_by_package) != set(packages):
    fail("Backup evidence file-row set differs from the exact two targets")

disk_before = {}
for package in packages:
    path = package_file(content_root, package)
    current = snapshot(path)
    disk_before[package] = current
    backup_row = backup_by_package[package]
    if backup_row.get("bytes_match") is not True:
        fail("Backup byte equality is not PASS for " + package)
    backup_source = validate_snapshot_record(
        backup_row.get("source"), path, package + " backup source"
    )
    backup_copy = backup_row.get("backup", {})
    backup_copy_path = os.path.realpath(str(backup_copy.get("file", "")))
    backup_root = os.path.realpath(str(backup.get("backup_root", "")))
    if not is_under(backup_copy_path, backup_root) or not os.path.isfile(backup_copy_path):
        fail("Detached backup copy is absent or out of bounds: " + package)
    if (
        str(backup_copy.get("sha256", "")).upper() != backup_source["sha256"]
        or backup_copy.get("length") != backup_source["length"]
        or sha256(backup_copy_path) != backup_source["sha256"]
    ):
        fail("Detached backup bytes differ: " + package)
    migration_row = migration_by_package.get(package)
    migration_snapshot = validate_snapshot_record(
        migration_row, path, package + " UE 5.7 migration"
    )
    if (
        current["length"] != backup_source["length"]
        or current["sha256"] != backup_source["sha256"]
        or current["length"] != migration_snapshot["length"]
        or current["sha256"] != migration_snapshot["sha256"]
    ):
        fail("Current target bytes differ from backup/original migration: " + package)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    ["/Game/Procedural/Blueprints", "/Game/_Game/Lockpicking"], True
)
registry.wait_for_completion()

loaded_blueprints = {}
repair_rows = []
for spec in SPECS:
    package = spec["package"]
    parent_class = unreal.load_class(None, spec["parent"])
    if parent_class is None or object_path(parent_class) != spec["parent"]:
        fail("Expected project-owned parent class could not load: " + spec["parent"])
    blueprint = unreal.EditorAssetLibrary.load_asset(package)
    if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
        fail("Selected package did not load as Blueprint: " + package)

    parent_before_object = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(
        blueprint
    )
    if parent_before_object is not None:
        fail(
            "Repair is restricted to a null parent; found {} on {}".format(
                object_path(parent_before_object), package
            )
        )
    links_before = list(
        unreal.ProjectNullParentBlueprintRepairLibrary.describe_override_event_links(
            blueprint
        )
    )
    for required_event in spec["required_events"]:
        if not any(row.startswith("event=" + required_event + "|") for row in links_before):
            fail("Required override event is absent before repair: " + required_event)

    if spec["helper"] == "altar":
        repaired = bool(
            unreal.ProjectNullParentBlueprintRepairLibrary.repair_sinful_ascension_altar_null_parent(
                blueprint
            )
        )
        configured = bool(
            unreal.ProjectSinfulAscensionEditorLibrary.configure_blueprint_as_sxp_altar(
                blueprint
            )
        )
        helper_valid = bool(
            unreal.ProjectNullParentBlueprintRepairLibrary.validate_sinful_ascension_altar_repair(
                blueprint
            )
        )
    else:
        repaired = bool(
            unreal.ProjectNullParentBlueprintRepairLibrary.repair_locked_world_item_null_parent(
                blueprint
            )
        )
        configured = bool(
            unreal.ProjectLockpickingEditorLibrary.configure_blueprint_for_component_lockpicking(
                blueprint
            )
        )
        helper_valid = bool(
            unreal.ProjectNullParentBlueprintRepairLibrary.validate_locked_world_item_repair(
                blueprint
            )
        )
    if not repaired or not configured or not helper_valid:
        fail("Project-owned editor repair/configuration failed: " + package)

    links_after = list(
        unreal.ProjectNullParentBlueprintRepairLibrary.describe_override_event_links(
            blueprint
        )
    )
    if links_after != links_before:
        fail("Override-event identities or useful links changed: " + package)

    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    status, normalized = normalized_status(blueprint)
    parent_after = object_path(
        unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
    )
    generated = generated_class(blueprint, package)
    ancestry = generated is not None and unreal.MathLibrary.class_is_child_of(
        generated, parent_class
    )
    if parent_after != spec["parent"]:
        fail("Direct parent repair did not stick: " + package)
    if "UPTODATE" not in normalized:
        fail("Blueprint did not compile UP_TO_DATE: {} ({})".format(package, status))
    if not ancestry:
        fail("Generated class ancestry is invalid: " + package)

    loaded_blueprints[package] = blueprint
    repair_rows.append(
        {
            "package": package,
            "parent_before": "",
            "parent_after": parent_after,
            "target_parent": spec["parent"],
            "required_override_events": list(spec["required_events"]),
            "override_event_links_before": links_before,
            "override_event_links_after": links_after,
            "override_event_links_preserved": True,
            "project_editor_helper_repaired": repaired,
            "existing_subsystem_editor_library_configured": configured,
            "project_editor_helper_validated": helper_valid,
            "compile_status": status,
            "generated_class": object_path(generated),
            "generated_class_is_child_of_target_parent": bool(ancestry),
        }
    )

save_operations = []
for package in packages:
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("Failed to save repaired Blueprint: " + package)
    save_operations.append(package)
if save_operations != packages:
    fail("The exact two-package save-set invariant failed")

disk_after = {
    package: snapshot(package_file(content_root, package)) for package in packages
}
for package in packages:
    if disk_after[package]["sha256"] == disk_before[package]["sha256"]:
        fail("Saved repair did not change package bytes: " + package)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_NULL_PARENT_GAMEPLAY_BLUEPRINT_REPAIR_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "migration_evidence": migration_evidence_path,
    "migration_evidence_sha256": sha256(migration_evidence_path),
    "backup_evidence": backup_evidence_path,
    "backup_evidence_sha256": sha256(backup_evidence_path),
    "package_count": len(packages),
    "packages": packages,
    "repairs": repair_rows,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "save_operations": save_operations,
    "saved_only_selected_packages": save_operations == packages,
    "override_event_links_preserved": all(
        row["override_event_links_preserved"] for row in repair_rows
    ),
    "exported_animations_excluded": True,
    "raw_asset_file_edits": [],
    "marketplace_or_engine_plugin_edits": [],
}
os.makedirs(os.path.dirname(repair_evidence_path), exist_ok=True)
temporary_path = repair_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, repair_evidence_path)
unreal.log("CODEX_NULL_PARENT_GAMEPLAY_REPAIR58_PASS: " + repair_evidence_path)
