"""Repair the 14 Kawaii Quinn PoseAssets that lost their Skeleton in UE 5.8.

Required process environment:
  CODEX_BULK_VALIDATION_EVIDENCE
  CODEX_BULK_VALIDATION_LOG
  CODEX_KAWAII_QUINN_REPAIR_EVIDENCE

Optional process environment:
  CODEX_KAWAII_QUINN_EXPECTED_COUNT (default: 14; any other value is rejected)

Selection is intentionally redundant and fail-closed.  The script requires the
same 14 Quinn bones in the prior compiler log, the same 14 PoseAsset packages in
the prior validation JSON, and the same 14 on-disk PoseAssets in Asset Registry.
It assigns only SK_Mannequin, saves exactly those 14 PoseAssets, and recompiles /
saves ABP_Quinn_PostProcess only when the repair makes that necessary.
"""

import datetime
import hashlib
import json
import os
import re

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
SKM_QUINN_PACKAGE = (
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/Meshes/SKM_Quinn"
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
LOG_BONE_PATTERN = re.compile(
    r"Pose Driver - Source:\s*([A-Za-z0-9_]+)\s+references Pose Asset "
    r"that uses a missing skeleton\s+<None>"
)

SOURCE_EVIDENCE_VALUE = os.environ.get(
    "CODEX_BULK_VALIDATION_EVIDENCE", ""
).strip()
SOURCE_LOG_VALUE = os.environ.get("CODEX_BULK_VALIDATION_LOG", "").strip()
REPAIR_EVIDENCE_VALUE = os.environ.get(
    "CODEX_KAWAII_QUINN_REPAIR_EVIDENCE", ""
).strip()
EXPECTED_COUNT_VALUE = os.environ.get(
    "CODEX_KAWAII_QUINN_EXPECTED_COUNT", "14"
).strip()


def fail(message):
    unreal.log_error("CODEX_KAWAII_QUINN_POSE_REPAIR58_FAIL: " + message)
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
        fail("Non-/Game package entered the save set: " + package_name)
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
if not SOURCE_EVIDENCE_VALUE or not SOURCE_LOG_VALUE or not REPAIR_EVIDENCE_VALUE:
    fail(
        "CODEX_BULK_VALIDATION_EVIDENCE, CODEX_BULK_VALIDATION_LOG, and "
        "CODEX_KAWAII_QUINN_REPAIR_EVIDENCE are required"
    )
try:
    expected_count = int(EXPECTED_COUNT_VALUE)
except ValueError:
    fail("CODEX_KAWAII_QUINN_EXPECTED_COUNT is not an integer")
if expected_count != 14 or len(EXPECTED_PACKAGES) != expected_count:
    fail("The guarded Kawaii Quinn repair contract requires exactly 14 packages")
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

source_evidence_path = os.path.realpath(SOURCE_EVIDENCE_VALUE)
source_log_path = os.path.realpath(SOURCE_LOG_VALUE)
repair_evidence_path = os.path.realpath(REPAIR_EVIDENCE_VALUE)
for label, path in (
    ("bulk validation evidence", source_evidence_path),
    ("bulk validation log", source_log_path),
    ("repair evidence", repair_evidence_path),
):
    if not is_under(path, saved_migration_root):
        fail(label + " escapes target Saved/Migration: " + path)
if repair_evidence_path.lower() in (
    source_evidence_path.lower(),
    source_log_path.lower(),
):
    fail("Repair evidence must not overwrite its source evidence")
if not os.path.isfile(source_evidence_path):
    fail("Bulk validation evidence is absent: " + source_evidence_path)
if not os.path.isfile(source_log_path):
    fail("Bulk validation log is absent: " + source_log_path)

with open(source_evidence_path, "r", encoding="utf-8-sig") as handle:
    source_evidence = json.load(handle)
source_project = os.path.realpath(str(source_evidence.get("project", "")))
if source_project.lower() != project_file.lower():
    fail("Bulk validation evidence belongs to a different project")

json_pose_rows = [
    row
    for row in source_evidence.get("loaded", [])
    if str(row.get("package", "")).startswith(QUINN_POSE_PREFIX)
    and str(row.get("class", "")) == "PoseAsset"
]
json_pose_packages = sorted(str(row.get("package", "")) for row in json_pose_rows)
if json_pose_packages != sorted(EXPECTED_PACKAGES):
    fail(
        "Prior validation JSON Quinn PoseAsset set differs from the guarded 14"
    )
if not all(bool(row.get("loaded", False)) for row in json_pose_rows):
    fail("Prior validation did not load every guarded Quinn PoseAsset")
abp_failure_rows = [
    row
    for row in source_evidence.get("failed", [])
    if str(row.get("package", "")) == POST_PROCESS_ABP_PACKAGE
    and str(row.get("reason", "")) == "BLUEPRINT_NOT_UP_TO_DATE"
]
if len(abp_failure_rows) != 1:
    fail("Prior validation does not contain the exact Quinn post-process failure")

log_bones = set()
with open(source_log_path, "r", encoding="utf-8-sig", errors="replace") as handle:
    for line in handle:
        if "ABP_Quinn_PostProcess.uasset" not in line:
            continue
        match = LOG_BONE_PATTERN.search(line)
        if match:
            log_bones.add(match.group(1))
if sorted(log_bones) != sorted(EXPECTED_BONES):
    fail(
        "Prior compiler log missing-skeleton bone set differs: "
        + repr(sorted(log_bones))
    )

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

target_skeleton = unreal.EditorAssetLibrary.load_asset(SKELETON_PACKAGE)
if target_skeleton is None or target_skeleton.get_class().get_name() != "Skeleton":
    fail("SK_Mannequin did not load as a Skeleton")
if object_path(target_skeleton) != SKELETON_OBJECT_PATH:
    fail("Loaded target Skeleton path differs: " + object_path(target_skeleton))

tracked_packages = (
    list(EXPECTED_ANIMATION_PACKAGES)
    + list(EXPECTED_PACKAGES)
    + [POST_PROCESS_ABP_PACKAGE]
)
disk_before = {
    package: snapshot(package_file(content_root, package))
    for package in tracked_packages
}
animation_repair_rows = []
animation_save_operations = []
# SKM_Quinn recursively preloads the broken post-process chain. Its first load
# is expected to report the missing Skeletons, but it leaves the AnimSequences
# resident so their Skeleton property can be repaired without another PostLoad.
prewarm_error = ""
try:
    unreal.EditorAssetLibrary.load_asset(SKM_QUINN_PACKAGE)
except Exception as exc:
    prewarm_error = repr(exc)
for package in EXPECTED_ANIMATION_PACKAGES:
    try:
        animation_asset = unreal.EditorAssetLibrary.load_asset(package)
    except Exception:
        # The recursive prewarm can finish asynchronously; one guarded retry
        # mirrors the later successful loads observed by the bulk validator.
        animation_asset = unreal.EditorAssetLibrary.load_asset(package)
    if animation_asset is None or animation_asset.get_class().get_name() != "AnimSequence":
        fail("Guarded package did not load as AnimSequence: " + package)
    skeleton_before_object = animation_asset.get_editor_property("skeleton")
    skeleton_before = object_path(skeleton_before_object)
    changed = False
    if skeleton_before_object is None:
        if not unreal.ProjectAnimationDiagnosticsLibrary.assign_missing_animation_skeleton(
            animation_asset, target_skeleton
        ):
            fail("Native bridge could not assign AnimSequence Skeleton: " + package)
        changed = True
    elif skeleton_before != SKELETON_OBJECT_PATH:
        fail("Refusing foreign AnimSequence Skeleton on {}: {}".format(package, skeleton_before))
    skeleton_after = object_path(animation_asset.get_editor_property("skeleton"))
    if skeleton_after != SKELETON_OBJECT_PATH:
        fail("AnimSequence Skeleton assignment did not stick: " + package)
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("Failed to save repaired AnimSequence: " + package)
    animation_save_operations.append(package)
    animation_repair_rows.append(
        {
            "package": package,
            "skeleton_before": skeleton_before,
            "skeleton_after": skeleton_after,
            "assigned_from_null": changed,
        }
    )

repair_rows = []
loaded_poses = {}
for package in EXPECTED_PACKAGES:
    pose_asset = unreal.EditorAssetLibrary.load_asset(package)
    if pose_asset is None or pose_asset.get_class().get_name() != "PoseAsset":
        fail("Guarded package did not load as PoseAsset: " + package)
    skeleton_before_object = pose_asset.get_editor_property("skeleton")
    skeleton_before = object_path(skeleton_before_object)
    changed = False
    if skeleton_before_object is None:
        if not unreal.ProjectAnimationDiagnosticsLibrary.assign_missing_animation_skeleton(
            pose_asset, target_skeleton
        ):
            fail("Native bridge could not assign PoseAsset Skeleton: " + package)
        changed = True
    elif skeleton_before != SKELETON_OBJECT_PATH:
        fail(
            "Refusing to replace a non-null foreign Skeleton on {}: {}".format(
                package, skeleton_before
            )
        )
    skeleton_after = object_path(pose_asset.get_editor_property("skeleton"))
    if skeleton_after != SKELETON_OBJECT_PATH:
        fail("Skeleton assignment did not stick on {}: {}".format(package, skeleton_after))
    loaded_poses[package] = pose_asset
    repair_rows.append(
        {
            "package": package,
            "skeleton_before": skeleton_before,
            "skeleton_after": skeleton_after,
            "assigned_from_null": changed,
        }
    )

pose_save_operations = []
for package in EXPECTED_PACKAGES:
    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("Failed to save repaired PoseAsset: " + package)
    pose_save_operations.append(package)
if pose_save_operations != list(EXPECTED_PACKAGES):
    fail("Exact PoseAsset save-set invariant failed")

post_process_abp = unreal.EditorAssetLibrary.load_asset(POST_PROCESS_ABP_PACKAGE)
if post_process_abp is None or post_process_abp.get_class().get_name() != "AnimBlueprint":
    fail("ABP_Quinn_PostProcess did not load as AnimBlueprint")
status_before, normalized_before = normalized_blueprint_status(post_process_abp)
assigned_count = sum(1 for row in repair_rows if row["assigned_from_null"])
compile_needed = assigned_count > 0 or "UPTODATE" not in normalized_before
abp_save_operations = []
if compile_needed:
    unreal.BlueprintEditorLibrary.compile_blueprint(post_process_abp)
    status_after, normalized_after = normalized_blueprint_status(post_process_abp)
    if "UPTODATE" not in normalized_after:
        fail(
            "ABP_Quinn_PostProcess did not compile UP_TO_DATE: " + status_after
        )
    if not unreal.EditorAssetLibrary.save_asset(
        POST_PROCESS_ABP_PACKAGE, only_if_is_dirty=False
    ):
        fail("Failed to save repaired ABP_Quinn_PostProcess")
    abp_save_operations.append(POST_PROCESS_ABP_PACKAGE)
else:
    status_after = status_before

save_operations = animation_save_operations + pose_save_operations + abp_save_operations
expected_save_operations = list(EXPECTED_ANIMATION_PACKAGES) + list(EXPECTED_PACKAGES) + (
    [POST_PROCESS_ABP_PACKAGE] if compile_needed else []
)
if save_operations != expected_save_operations:
    fail("Overall exact save-set invariant failed")

disk_after = {
    package: snapshot(package_file(content_root, package))
    for package in tracked_packages
}
changed_files = [
    package
    for package in tracked_packages
    if disk_before[package]["sha256"] != disk_after[package]["sha256"]
]
if not compile_needed and POST_PROCESS_ABP_PACKAGE in changed_files:
    fail("Post-process AnimBlueprint changed despite compile/save being unnecessary")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_KAWAII_QUINN_POSE_SKELETON_REPAIR_PASS",
    "engine_version": engine_version,
    "project": project_file,
    "source_bulk_validation_evidence": source_evidence_path,
    "source_bulk_validation_evidence_sha256": sha256(source_evidence_path),
    "source_bulk_validation_log": source_log_path,
    "source_bulk_validation_log_sha256": sha256(source_log_path),
    "selection_prefix": QUINN_POSE_PREFIX,
    "excluded_prefix": EXCLUDED_PREFIX,
    "expected_count": expected_count,
    "package_count": len(EXPECTED_PACKAGES),
    "expected_bones": list(EXPECTED_BONES),
    "compiler_log_missing_skeleton_bones": sorted(log_bones),
    "validation_json_pose_packages": json_pose_packages,
    "registry_pose_packages": registry_pose_packages,
    "registry_animation_packages": registry_animation_packages,
    "packages": list(EXPECTED_PACKAGES),
    "source_animation_packages": list(EXPECTED_ANIMATION_PACKAGES),
    "target_skeleton_package": SKELETON_PACKAGE,
    "target_skeleton_object": SKELETON_OBJECT_PATH,
    "loaded_target_skeleton_class": target_skeleton.get_class().get_name(),
    "repairs": repair_rows,
    "source_animation_repairs": animation_repair_rows,
    "source_animation_assigned_from_null_count": sum(
        1 for row in animation_repair_rows if row["assigned_from_null"]
    ),
    "source_animation_saved_count": len(animation_save_operations),
    "skeletal_mesh_prewarm_package": SKM_QUINN_PACKAGE,
    "skeletal_mesh_prewarm_error": prewarm_error,
    "assigned_from_null_count": assigned_count,
    "pose_asset_saved_count": len(pose_save_operations),
    "post_process_anim_blueprint": POST_PROCESS_ABP_PACKAGE,
    "post_process_status_before": status_before,
    "post_process_compile_needed": compile_needed,
    "post_process_status_after": status_after,
    "post_process_saved": bool(abp_save_operations),
    "save_operations": save_operations,
    "saved_only_guarded_packages": save_operations == expected_save_operations,
    "tracked_package_files_before": disk_before,
    "tracked_package_files_after": disk_after,
    "changed_tracked_package_files": changed_files,
    "backup_performed_by_script": False,
}
os.makedirs(os.path.dirname(repair_evidence_path), exist_ok=True)
temporary_path = repair_evidence_path + ".tmp"
with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary_path, repair_evidence_path)
unreal.log("CODEX_KAWAII_QUINN_POSE_REPAIR58_PASS: " + repair_evidence_path)
