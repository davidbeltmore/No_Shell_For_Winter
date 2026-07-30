"""Repair and validate-save the single migrated Locked Blueprint parent in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


PACKAGE = "/Game/_Game/Lockpicking/Locked"
PARENT_PATH = "/Script/EFProjectSystemsGameplay.ProjectLockedWorldItem"
EVIDENCE_VALUE = os.environ.get("CODEX_LOCKED_PARENT_REPAIR_EVIDENCE", "").strip()


def fail(message):
    unreal.log_error("CODEX_LOCKED_PARENT_REPAIR58_FAIL: " + message)
    raise RuntimeError(message)


def digest(path):
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
if os.path.basename(project).lower() != "noshellforwinter.uproject":
    fail("Wrong project")
evidence = os.path.realpath(EVIDENCE_VALUE)
saved_root = os.path.realpath(os.path.join(root, "Saved", "Migration"))
if not EVIDENCE_VALUE or os.path.commonpath([evidence, saved_root]).lower() != saved_root.lower():
    fail("Evidence path must be under Saved/Migration")
asset_file = os.path.realpath(os.path.join(root, "Content", "_Game", "Lockpicking", "Locked.uasset"))
before = digest(asset_file)
parent = unreal.load_class(None, PARENT_PATH)
if parent is None or object_path(parent) != PARENT_PATH:
    fail("ProjectLockedWorldItem parent class did not load exactly")
blueprint = unreal.EditorAssetLibrary.load_asset(PACKAGE)
if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
    fail("Locked did not load as Blueprint")
parent_before_object = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
parent_before = object_path(parent_before_object)
if parent_before_object is None:
    unreal.BlueprintEditorLibrary.reparent_blueprint(blueprint, parent)
elif parent_before != PARENT_PATH:
    fail("Refusing to replace non-null parent: " + parent_before)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
status = str(blueprint.get_editor_property("status"))
normalized = "".join(ch for ch in status.upper() if ch.isalnum())
parent_after = object_path(unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint))
generated = unreal.load_class(None, PACKAGE + ".Locked_C")
if parent_after != PARENT_PATH or "UPTODATE" not in normalized:
    fail("Locked parent or compile status invalid: {} / {}".format(parent_after, status))
if generated is None or not unreal.MathLibrary.class_is_child_of(generated, parent):
    fail("Locked generated class ancestry is invalid")
if not unreal.EditorAssetLibrary.save_asset(PACKAGE, only_if_is_dirty=False):
    fail("Locked save failed")
after = digest(asset_file)
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_LOCKED_PARENT_REPAIR_PASS",
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project": project,
    "package": PACKAGE,
    "parent_before": parent_before,
    "parent_after": parent_after,
    "compile_status": status,
    "generated_class": object_path(generated),
    "package_file_before": before,
    "package_file_after": after,
    "save_operations": [PACKAGE],
}
os.makedirs(os.path.dirname(evidence), exist_ok=True)
with open(evidence + ".tmp", "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(evidence + ".tmp", evidence)
unreal.log("CODEX_LOCKED_PARENT_REPAIR58_PASS: " + evidence)
