"""Fresh read-only UE 5.8 validation of the Kawaii Quinn PoseAsset repair.

Required process environment:
  CODEX_KAWAII_QUINN_REPAIR_EVIDENCE
  CODEX_KAWAII_QUINN_VALIDATION_EVIDENCE

Optional process environment:
  CODEX_KAWAII_QUINN_EXPECTED_COUNT (default: 14; any other value is rejected)

The validator loads the exact 14 PoseAssets, verifies SK_Mannequin on each,
compiles ABP_Quinn_PostProcess in memory, and proves that no tracked asset file
was written.  It never calls a save API.
"""

import datetime
import hashlib
import json
import os

import unreal


QUINN_POSE_PREFIX = (
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/"
    "Rigs/Poses/Quinn/"
)
SKELETON_PACKAGE = (
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/Meshes/SK_Mannequin"
)
SKELETON_OBJECT_PATH = SKELETON_PACKAGE + ".SK_Mannequin"
POST_PROCESS_ABP_PACKAGE = (
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/"
    "Rigs/ABP_Quinn_PostProcess"
)
EXCLUDED_PREFIX = "/Game/ExportedAnimations"
EXPECTED_BONES = (
    "calf_l",
    "calf_r",
    "clavicle_l",
    "clavicle_r",
    "foot_l",
    "foot_r",
    "hand_l",
    "hand_r",
    "lowerarm_l",
    "lowerarm_r",
    "thigh_l",
    "thigh_r",
    "upperarm_l",
    "upperarm_r",
)
EXPECTED_PACKAGES = tuple(
    QUINN_POSE_PREFIX + "Quinn_" + bone + "_pose" for bone in EXPECTED_BONES
)
EXPECTED_ANIMATION_PACKAGES = tuple(
    QUINN_POSE_PREFIX + "Quinn_" + bone + "_anim" for bone in EXPECTED_BONES
)

REPAIR_EVIDENCE_VALUE = os.environ.get(
    "CODEX_KAWAII_QUINN_REPAIR_EVIDENCE", ""
).strip()
VALIDATION_EVIDENCE_VALUE = os.environ.get(
    "CODEX_KAWAII_QUINN_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_COUNT_VALUE = os.environ.get(
    "CODEX_KAWAII_QUINN_EXPECTED_COUNT", "14"
).strip()


def fail(message):
    unreal.log_error("CODEX_KAWAII_QUINN_POSE_VALIDATE58_FAIL: " + message)
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
    if not package_name.startswith("/Game/"):
        fail("Non-/Game package entered the validation set: " + package_name)
    if package_name.startswith(EXCLUDED_PREFIX):
        fail("ExportedAnimations is explicitly excluded: " + package_name)
    path = os.path.realpath(
        os.path.join(
            content_root,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )
    if not is_under(path, content_root):
        fail("Package file escapes target Content: " + path)
    return path


def snapshot(path):
    if not os.path.isfile(path):
        fail("Required target asset file is absent: " + path)
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


def asset_data_class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        try:
            return str(asset_data.asset_class)
        except Exception:
            return ""


def normalized_blueprint_status(blueprint):
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(
        character for character in status.upper() if character.isalnum()
    )
    return status, normalized


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not REPAIR_EVIDENCE_VALUE or not VALIDATION_EVIDENCE_VALUE:
    fail(
        "CODEX_KAWAII_QUINN_REPAIR_EVIDENCE and "
        "CODEX_KAWAII_QUINN_VALIDATION_EVIDENCE are required"
    )
try:
    expected_count = int(EXPECTED_COUNT_VALUE)
except ValueError:
    fail("CODEX_KAWAII_QUINN_EXPECTED_COUNT is not an integer")
if expected_count != 14 or len(EXPECTED_PACKAGES) != expected_count:
    fail("The guarded Kawaii Quinn validation contract requires exactly 14 packages")
if any(package.startswith(EXCLUDED_PREFIX) for package in EXPECTED_PACKAGES):
    fail("The guarded package set overlaps ExportedAnimations")

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

repair_evidence_path = os.path.realpath(REPAIR_EVIDENCE_VALUE)
validation_evidence_path = os.path.realpath(VALIDATION_EVIDENCE_VALUE)
if not is_under(repair_evidence_path, saved_migration_root):
    fail("Repair evidence escapes target Saved/Migration")
if not is_under(validation_evidence_path, saved_migration_root):
    fail("Validation evidence escapes target Saved/Migration")
if repair_evidence_path.lower() == validation_evidence_path.lower():
    fail("Validation evidence must not overwrite repair evidence")
if not os.path.isfile(repair_evidence_path):
    fail("Repair evidence is absent: " + repair_evidence_path)

with open(repair_evidence_path, "r", encoding="utf-8-sig") as handle:
    repair_evidence = json.load(handle)
if repair_evidence.get("status") != (
    "UE58_KAWAII_QUINN_POSE_SKELETON_REPAIR_PASS"
):
    fail("Repair evidence does not record the expected PASS status")
repair_project = os.path.realpath(str(repair_evidence.get("project", "")))
if repair_project.lower() != project_file.lower():
    fail("Repair evidence belongs to a different project")
if int(repair_evidence.get("expected_count", -1)) != expected_count:
    fail("Repair evidence count contract differs")
if repair_evidence.get("packages", []) != list(EXPECTED_PACKAGES):
    fail("Repair evidence package set differs from the guarded 14")
if repair_evidence.get("source_animation_packages", []) != list(EXPECTED_ANIMATION_PACKAGES):
    fail("Repair evidence source animation set differs from the guarded 14")
if repair_evidence.get("target_skeleton_object") != SKELETON_OBJECT_PATH:
    fail("Repair evidence target Skeleton differs")
if repair_evidence.get("excluded_prefix") != EXCLUDED_PREFIX:
    fail("Repair evidence does not preserve the ExportedAnimations exclusion")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([QUINN_POSE_PREFIX.rstrip("/")], True)
registry.wait_for_completion()
registry_pose_packages = sorted(
    str(row.package_name)
    for row in registry.get_assets_by_path(
        QUINN_POSE_PREFIX.rstrip("/"),
        recursive=False,
        include_only_on_disk_assets=True,
    )
    if asset_data_class_name(row) == "PoseAsset"
)
if registry_pose_packages != sorted(EXPECTED_PACKAGES):
    fail("Asset Registry Quinn PoseAsset set differs from the guarded 14")
registry_animation_packages = sorted(
    str(row.package_name)
    for row in registry.get_assets_by_path(
        QUINN_POSE_PREFIX.rstrip("/"),
        recursive=False,
        include_only_on_disk_assets=True,
    )
    if asset_data_class_name(row) == "AnimSequence"
)
if registry_animation_packages != sorted(EXPECTED_ANIMATION_PACKAGES):
    fail("Asset Registry Quinn AnimSequence set differs from the guarded 14")

tracked_packages = (
    list(EXPECTED_ANIMATION_PACKAGES)
    + list(EXPECTED_PACKAGES)
    + [POST_PROCESS_ABP_PACKAGE]
)
disk_before = {
    package: snapshot(package_file(content_root, package))
    for package in tracked_packages
}

target_skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PACKAGE)
if target_skeleton is None or target_skeleton.get_class().get_name() != "Skeleton":
    fail("SK_Mannequin did not load as a Skeleton")
if object_path(target_skeleton) != SKELETON_OBJECT_PATH:
    fail("Loaded target Skeleton path differs: " + object_path(target_skeleton))

animation_validation_rows = []
for package in EXPECTED_ANIMATION_PACKAGES:
    animation_asset = unreal.EditorAssetLibrary.load_asset(package)
    if animation_asset is None or animation_asset.get_class().get_name() != "AnimSequence":
        fail("Guarded package did not load as AnimSequence: " + package)
    skeleton_path = object_path(animation_asset.get_editor_property("skeleton"))
    if skeleton_path != SKELETON_OBJECT_PATH:
        fail("Fresh AnimSequence Skeleton differs on {}: {}".format(package, skeleton_path))
    animation_validation_rows.append(
        {"package": package, "class": "AnimSequence", "skeleton": skeleton_path, "result": "PASS"}
    )

validation_rows = []
for package in EXPECTED_PACKAGES:
    pose_asset = unreal.EditorAssetLibrary.load_asset(package)
    if pose_asset is None or pose_asset.get_class().get_name() != "PoseAsset":
        fail("Guarded package did not load as PoseAsset: " + package)
    skeleton_path = object_path(pose_asset.get_editor_property("skeleton"))
    if skeleton_path != SKELETON_OBJECT_PATH:
        fail(
            "Fresh PoseAsset Skeleton differs on {}: {}".format(
                package, skeleton_path
            )
        )
    validation_rows.append(
        {
            "package": package,
            "class": pose_asset.get_class().get_name(),
            "skeleton": skeleton_path,
            "result": "PASS",
        }
    )

post_process_abp = unreal.EditorAssetLibrary.load_asset(POST_PROCESS_ABP_PACKAGE)
if post_process_abp is None or post_process_abp.get_class().get_name() != "AnimBlueprint":
    fail("ABP_Quinn_PostProcess did not load as AnimBlueprint")
status_before, _ = normalized_blueprint_status(post_process_abp)
unreal.BlueprintEditorLibrary.compile_blueprint(post_process_abp)
status_after, normalized_after = normalized_blueprint_status(post_process_abp)
if "UPTODATE" not in normalized_after:
    fail("ABP_Quinn_PostProcess fresh compile failed: " + status_after)

disk_after = {
    package: snapshot(package_file(content_root, package))
    for package in tracked_packages
}
changed_files = [
    package
    for package in tracked_packages
    if disk_before[package] != disk_after[package]
]
if changed_files:
    fail("Read-only validation changed tracked asset files: " + repr(changed_files))

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_KAWAII_QUINN_POSE_SKELETON_FRESH_VALIDATE_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "repair_evidence": repair_evidence_path,
    "repair_evidence_sha256": sha256(repair_evidence_path),
    "selection_prefix": QUINN_POSE_PREFIX,
    "excluded_prefix": EXCLUDED_PREFIX,
    "expected_count": expected_count,
    "package_count": len(EXPECTED_PACKAGES),
    "packages": list(EXPECTED_PACKAGES),
    "registry_pose_packages": registry_pose_packages,
    "source_animation_packages": list(EXPECTED_ANIMATION_PACKAGES),
    "registry_animation_packages": registry_animation_packages,
    "target_skeleton_package": SKELETON_PACKAGE,
    "target_skeleton_object": SKELETON_OBJECT_PATH,
    "loaded_target_skeleton_class": target_skeleton.get_class().get_name(),
    "pose_asset_validations": validation_rows,
    "source_animation_validations": animation_validation_rows,
    "post_process_anim_blueprint": POST_PROCESS_ABP_PACKAGE,
    "post_process_status_before_compile": status_before,
    "post_process_status_after_compile": status_after,
    "post_process_compile_up_to_date": True,
    "tracked_package_files_before": disk_before,
    "tracked_package_files_after": disk_after,
    "changed_tracked_package_files": changed_files,
    "tracked_package_files_unchanged": not changed_files,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
}
os.makedirs(os.path.dirname(validation_evidence_path), exist_ok=True)
temporary_path = validation_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, validation_evidence_path)
unreal.log("CODEX_KAWAII_QUINN_POSE_VALIDATE58_PASS: " + validation_evidence_path)
