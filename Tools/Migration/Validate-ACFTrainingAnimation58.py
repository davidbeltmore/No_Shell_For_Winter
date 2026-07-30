"""Validate and resave the one allowlisted ACFTrainingSystem animation in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


ASSET = "/Game/ExportedAnimations/Anim_KA_Idle53_Seiza_Loop1"
ALLOWED_PRE_RESAVE_SHA256 = (
    "E12ECFF82A7424BC256A0325B729C3816D17117129CD6E423E9134F8FA9ECE4F",
    "075F215945E0262BF189DAE947B6AAAB53DCFCC3D043E776D392E0D8035A5176",
)
EXPECTED_SKELETON = (
    "/Game/FullSample/GASP/UEFN_Mannequin/Meshes/"
    "SK_UEFN_Mannequin.SK_UEFN_Mannequin"
)
EXPECTED_PREVIEW_MESH = "/Game/DazToUnreal/Female/Female.Female"
PROTECTED_TARGET_FILES = (
    (
        os.path.join("DazToUnreal", "Female", "Female.uasset"),
        "B3BC3B6AEDF79C025F826305EFB219F827E3BE487DF7F2F7A4DC31FF17377BAB",
    ),
    (
        os.path.join(
            "FullSample",
            "GASP",
            "UEFN_Mannequin",
            "Meshes",
            "SK_UEFN_Mannequin.uasset",
        ),
        "2F23059E4A1F166030724EB4369BA462302D025F2F4A09079EF95402846A8693",
    ),
)
FORBIDDEN_RELATIVE_FILES = (
    os.path.join("DazToUnreal", "Deformers", "MDC_DazDualQuatMorph.uasset"),
    os.path.join("DazToUnreal", "Female", "Materials", "Genesis9", "Genesis9_Arms_Profile.uasset"),
    os.path.join("DazToUnreal", "Female", "Materials", "Genesis9", "Genesis9_Body_Profile.uasset"),
    os.path.join("DazToUnreal", "Female", "Materials", "Genesis9", "Genesis9_Legs_Profile.uasset"),
    os.path.join("DazToUnreal", "Female", "Materials", "Genesis9", "Genesis9_MouthCavity.uasset"),
    os.path.join("DazToUnreal", "Female", "Materials", "Genesis9", "Genesis9_MouthCavity_Profile.uasset"),
    os.path.join("FullSample", "GASP", "UEFN_Mannequin", "Materials", "M_UEFN_Mannequin.uasset"),
    os.path.join("FullSample", "GASP", "UEFN_Mannequin", "Rigs", "ABP_UEFN_Mannequin_PostProcess.uasset"),
    os.path.join("FullSample", "GASP", "UEFN_Mannequin", "Textures", "T_UEFN_Mannequin_D.uasset"),
    os.path.join("FullSample", "GASP", "UEFN_Mannequin", "Textures", "T_UEFN_Mannequin_N.uasset"),
)


def fail(message):
    unreal.log_error("CODEX_ACFTRAIN_ANIMATION_58_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(os.path.join(project_root, "Content"))
physical_file = os.path.realpath(
    os.path.join(
        content_root,
        "ExportedAnimations",
        "Anim_KA_Idle53_Seiza_Loop1.uasset",
    )
)
evidence_path = os.path.realpath(
    os.path.join(
        project_root,
        "Saved",
        "Migration",
        "Phase3",
        "ACFTrainingAnimation58Validation.json",
    )
)
if not physical_file.lower().startswith(content_root.lower() + os.sep):
    fail("Animation path escapes target Content")
if not os.path.isfile(physical_file):
    fail("Migrated animation is absent: " + physical_file)
pre_resave_hash = sha256(physical_file)
if pre_resave_hash not in ALLOWED_PRE_RESAVE_SHA256:
    fail("Unexpected pre-resave animation hash: " + pre_resave_hash)

protected_hashes = {}
for relative_file, expected_hash in PROTECTED_TARGET_FILES:
    path = os.path.realpath(os.path.join(content_root, relative_file))
    if not os.path.isfile(path):
        fail("Protected target package is absent: " + path)
    actual_hash = sha256(path)
    if actual_hash != expected_hash:
        fail("Protected target package hash differs from baseline: " + path)
    protected_hashes[relative_file] = actual_hash
for relative_file in FORBIDDEN_RELATIVE_FILES:
    path = os.path.realpath(os.path.join(content_root, relative_file))
    if os.path.exists(path):
        fail("Forbidden dependency exists before UE 5.8 validation: " + path)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/ExportedAnimations"], True)
registry.wait_for_completion()
dependency_options = unreal.AssetRegistryDependencyOptions()
for property_name in (
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
    "include_searchable_names",
):
    try:
        dependency_options.set_editor_property(
            property_name, property_name != "include_searchable_names"
        )
    except Exception:
        pass
dependencies = sorted(
    {str(item) for item in registry.get_dependencies(ASSET, dependency_options)}
)
if not unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    fail("Migrated animation is not registered")
asset = unreal.EditorAssetLibrary.load_asset(ASSET)
if asset is None:
    fail("Migrated animation failed to load")
class_name = asset.get_class().get_name()
if class_name != "AnimSequence":
    fail("Expected AnimSequence but found " + class_name)

skeleton = asset.get_editor_property("skeleton")
if skeleton is None:
    fail("Animation has no skeleton")
skeleton_path = skeleton.get_path_name()
if skeleton_path != EXPECTED_SKELETON:
    fail("Unexpected animation skeleton: " + skeleton_path)

preview_mesh_path = ""
try:
    preview_mesh = asset.get_editor_property("preview_skeletal_mesh")
    if preview_mesh is not None:
        preview_mesh_path = preview_mesh.get_path_name()
except Exception as error:
    unreal.log_warning("Preview mesh property unavailable: " + str(error))
if preview_mesh_path and preview_mesh_path != EXPECTED_PREVIEW_MESH:
    fail("Unexpected animation preview mesh: " + preview_mesh_path)

if not unreal.EditorAssetLibrary.save_asset(ASSET, only_if_is_dirty=False):
    fail("UE 5.8 resave returned false")
post_resave_hash = sha256(physical_file)

for relative_file, expected_hash in PROTECTED_TARGET_FILES:
    path = os.path.realpath(os.path.join(content_root, relative_file))
    actual_hash = sha256(path)
    if actual_hash != expected_hash or actual_hash != protected_hashes[relative_file]:
        fail("UE 5.8 validation modified protected target package: " + path)
for relative_file in FORBIDDEN_RELATIVE_FILES:
    path = os.path.realpath(os.path.join(content_root, relative_file))
    if os.path.exists(path):
        fail("UE 5.8 validation created forbidden dependency: " + path)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_LOAD_RESAVE_VALIDATION_PASS",
    "engine_version": engine_version,
    "asset": ASSET,
    "class": class_name,
    "skeleton": skeleton_path,
    "preview_mesh": preview_mesh_path,
    "dependencies": dependencies,
    "dependency_count": len(dependencies),
    "pre_resave_sha256": pre_resave_hash,
    "post_resave_sha256": post_resave_hash,
    "post_resave_length": os.path.getsize(physical_file),
    "protected_target_files": protected_hashes,
    "protected_target_files_unchanged": True,
    "forbidden_dependency_count": len(FORBIDDEN_RELATIVE_FILES),
    "forbidden_dependencies_absent": True,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_ACFTRAIN_ANIMATION_58_PASS: " + evidence_path)
