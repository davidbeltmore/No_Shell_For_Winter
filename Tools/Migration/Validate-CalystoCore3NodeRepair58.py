"""Fresh, no-save validation of UE 5.8 Calysto Core3 node repair.

Required environment:
  CODEX_CALYSTO_CORE3_NODE_REPAIR_EVIDENCE
  CODEX_CALYSTO_CORE3_NODE_VALIDATION_EVIDENCE
"""

import datetime
import hashlib
import json
import os

import unreal


REPAIR_VALUE = os.environ.get(
    "CODEX_CALYSTO_CORE3_NODE_REPAIR_EVIDENCE", ""
).strip()
EVIDENCE_VALUE = os.environ.get(
    "CODEX_CALYSTO_CORE3_NODE_VALIDATION_EVIDENCE", ""
).strip()
PACKAGES = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController",
    "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto",
)
EXPECTED_PARENTS = {
    PACKAGES[0]: "/Script/Engine.Actor",
    PACKAGES[1]: "/Script/Engine.PlayerController",
    PACKAGES[2]: "/Script/Engine.PrimaryDataAsset",
}


def fail(message):
    unreal.log_error("CODEX_CALYSTO_CORE3_NODE_VALIDATE58_FAIL: " + message)
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
        fail("Required repaired package is absent: " + path)
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


def status(blueprint):
    raw = str(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in raw.upper() if character.isalnum())
    return raw, normalized


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not REPAIR_VALUE or not EVIDENCE_VALUE:
    fail("Both documented Core3 node-validation environment variables are required")
if len(PACKAGES) != 3 or len(set(PACKAGES)) != 3:
    fail("Guarded validation cohort is not exactly three unique packages")
if any(package.lower().startswith("/game/exportedanimations") for package in PACKAGES):
    fail("ExportedAnimations entered the Core3 node-validation cohort")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration"))
repair_path = os.path.realpath(REPAIR_VALUE)
evidence_path = os.path.realpath(EVIDENCE_VALUE)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if not is_under(repair_path, saved_root) or not os.path.isfile(repair_path):
    fail("Core3 node-repair evidence is absent or escapes Saved/Migration")
if not is_under(evidence_path, saved_root) or evidence_path.lower() == repair_path.lower():
    fail("Core3 node-validation evidence path is invalid")

with open(repair_path, "r", encoding="utf-8-sig") as handle:
    repair = json.load(handle)
if repair.get("status") != "UE58_CALYSTO_CORE3_NODE_REPAIR_PASS":
    fail("Core3 node-repair evidence status is not PASS")
if tuple(str(value) for value in repair.get("packages", [])) != PACKAGES:
    fail("Core3 node-repair evidence package cohort differs")
if os.path.realpath(str(repair.get("project", ""))).lower() != project_file.lower():
    fail("Core3 node-repair evidence belongs to another project")
if (
    repair.get("package_count") != 3
    or repair.get("target_delta_exact") is not True
    or repair.get("saved_only_if_all_compiled") is not True
    or repair.get("exported_animations_excluded") is not True
    or len(repair.get("save_operations", [])) != 3
):
    fail("Core3 node-repair evidence invariants are incomplete")

recorded_after = repair.get("package_files_after", {})
if set(recorded_after) != set(PACKAGES):
    fail("Core3 repair after-snapshot is not the exact three-package cohort")
disk_before = {}
byte_checks = []
for package in PACKAGES:
    current = snapshot(package_file(content_root, package))
    recorded = recorded_after.get(package, {})
    matches = (
        current["length"] == int(recorded.get("length", -1))
        and current["sha256"] == str(recorded.get("sha256", "")).upper()
    )
    byte_checks.append({"package": package, "matches_repair_after": matches})
    if not matches:
        fail("Current bytes differ from Core3 node-repair evidence: " + package)
    disk_before[package] = current

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Calysto"], True)
registry.wait_for_completion()
validation_rows = []
failures = []
for package in PACKAGES:
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(package)
        if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
            raise RuntimeError("Asset is not a Blueprint")
        parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
        parent_path = object_path(parent)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        raw_status, normalized = status(blueprint)
        asset_name = package.rsplit("/", 1)[-1]
        generated = unreal.load_class(None, package + "." + asset_name + "_C")
        expected_parent = unreal.load_class(None, EXPECTED_PARENTS[package])
        ancestry = (
            generated is not None
            and expected_parent is not None
            and unreal.MathLibrary.class_is_child_of(generated, expected_parent)
        )
    except Exception as exc:
        failures.append(
            {"package": package, "reason": "LOAD_OR_COMPILE_EXCEPTION", "error": repr(exc)}
        )
        continue
    validation_rows.append(
        {
            "package": package,
            "compile_status": raw_status,
            "direct_parent": parent_path,
            "expected_parent": EXPECTED_PARENTS[package],
            "generated_class": object_path(generated),
            "generated_class_has_expected_ancestry": bool(ancestry),
        }
    )
    if parent_path != EXPECTED_PARENTS[package]:
        failures.append({"package": package, "reason": "DIRECT_PARENT_MISMATCH"})
    if "UPTODATE" not in normalized:
        failures.append(
            {"package": package, "reason": "BLUEPRINT_NOT_UP_TO_DATE", "status": raw_status}
        )
    if not ancestry:
        failures.append(
            {"package": package, "reason": "GENERATED_CLASS_ANCESTRY_MISMATCH"}
        )

disk_after = {package: snapshot(row["file"]) for package, row in disk_before.items()}
changed_hashes = [
    package
    for package in PACKAGES
    if disk_after[package]["length"] != disk_before[package]["length"]
    or disk_after[package]["sha256"] != disk_before[package]["sha256"]
]
for package in changed_hashes:
    failures.append({"package": package, "reason": "PACKAGE_BYTES_CHANGED_DURING_VALIDATION"})

passed = len(validation_rows) == 3 and not failures and not changed_hashes
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_CALYSTO_CORE3_NODE_FRESH_READ_ONLY_PASS"
        if passed
        else "UE58_CALYSTO_CORE3_NODE_FRESH_READ_ONLY_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "node_repair_evidence": repair_path,
    "node_repair_evidence_sha256": sha256(repair_path),
    "package_count": 3,
    "packages": list(PACKAGES),
    "repair_after_byte_checks": byte_checks,
    "loaded_compiled_validated_count": len(validation_rows),
    "validations": validation_rows,
    "failures": failures,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_hashes": changed_hashes,
    "package_hashes_unchanged": not changed_hashes,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
    "exported_animations_excluded": True,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary = evidence_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, evidence_path)
if not passed:
    fail(
        "Fresh Core3 node validation failed: validated={}/3 failures={} changed={}".format(
            len(validation_rows), len(failures), len(changed_hashes)
        )
    )
unreal.log("CODEX_CALYSTO_CORE3_NODE_VALIDATE58_PASS: " + evidence_path)
