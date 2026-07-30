"""Fresh-process read-only validation of the repaired Locked Blueprint."""

import datetime
import hashlib
import json
import os

import unreal


PACKAGE = "/Game/_Game/Lockpicking/Locked"
PARENT_PATH = "/Script/EFProjectSystemsGameplay.ProjectLockedWorldItem"
REPAIR_VALUE = os.environ.get("CODEX_LOCKED_PARENT_REPAIR_EVIDENCE", "").strip()
EVIDENCE_VALUE = os.environ.get("CODEX_LOCKED_PARENT_VALIDATION_EVIDENCE", "").strip()


def fail(message):
    unreal.log_error("CODEX_LOCKED_PARENT_VALIDATE58_FAIL: " + message)
    raise RuntimeError(message)


def snapshot(path):
    value = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    stat = os.stat(path)
    return {"file": path, "length": stat.st_size, "mtime_ns": stat.st_mtime_ns, "sha256": value.hexdigest().upper()}


def object_path(value):
    return "" if value is None else str(value.get_path_name())


if not unreal.SystemLibrary.get_engine_version().startswith("5.8."):
    fail("Expected UE 5.8")
project = os.path.realpath(unreal.Paths.get_project_file_path())
root = os.path.realpath(unreal.Paths.project_dir())
saved_root = os.path.realpath(os.path.join(root, "Saved", "Migration"))
repair = os.path.realpath(REPAIR_VALUE)
evidence = os.path.realpath(EVIDENCE_VALUE)
if os.path.basename(project).lower() != "noshellforwinter.uproject":
    fail("Wrong project")
if not REPAIR_VALUE or not EVIDENCE_VALUE:
    fail("Evidence environment is required")
if any(os.path.commonpath([path, saved_root]).lower() != saved_root.lower() for path in (repair, evidence)):
    fail("Evidence path escapes Saved/Migration")
with open(repair, "r", encoding="utf-8-sig") as handle:
    repair_data = json.load(handle)
if repair_data.get("status") != "UE58_LOCKED_PARENT_REPAIR_PASS" or repair_data.get("package") != PACKAGE:
    fail("Repair evidence is invalid")
asset_file = os.path.realpath(os.path.join(root, "Content", "_Game", "Lockpicking", "Locked.uasset"))
before = snapshot(asset_file)
parent = unreal.load_class(None, PARENT_PATH)
blueprint = unreal.EditorAssetLibrary.load_asset(PACKAGE)
if parent is None or blueprint is None:
    fail("Parent or Locked failed to load")
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
status = str(blueprint.get_editor_property("status"))
normalized = "".join(ch for ch in status.upper() if ch.isalnum())
direct_parent = object_path(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint))
generated = unreal.load_class(None, PACKAGE + ".Locked_C")
ancestry = generated is not None and unreal.MathLibrary.class_is_child_of(generated, parent)
after = snapshot(asset_file)
failures = []
if direct_parent != PARENT_PATH:
    failures.append("DIRECT_PARENT_MISMATCH")
if "UPTODATE" not in normalized:
    failures.append("BLUEPRINT_NOT_UP_TO_DATE")
if not ancestry:
    failures.append("GENERATED_CLASS_ANCESTRY_MISMATCH")
if before != after:
    failures.append("PACKAGE_CHANGED_DURING_VALIDATION")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_LOCKED_PARENT_FRESH_READ_ONLY_PASS" if not failures else "UE58_LOCKED_PARENT_FRESH_READ_ONLY_FAIL",
    "project": project,
    "package": PACKAGE,
    "direct_parent": direct_parent,
    "compile_status": status,
    "generated_class": object_path(generated),
    "generated_class_is_child_of_parent": bool(ancestry),
    "package_file_before": before,
    "package_file_after": after,
    "package_file_unchanged": before == after,
    "save_operations": [],
    "failures": failures,
}
os.makedirs(os.path.dirname(evidence), exist_ok=True)
with open(evidence + ".tmp", "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(evidence + ".tmp", evidence)
if failures:
    fail("; ".join(failures))
unreal.log("CODEX_LOCKED_PARENT_VALIDATE58_PASS: " + evidence)
