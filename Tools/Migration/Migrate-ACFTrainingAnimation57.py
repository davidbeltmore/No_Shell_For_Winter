"""Migrate one allowlisted ACFTrainingSystem animation through UE 5.7 AssetTools."""

import datetime
import hashlib
import json
import os

import unreal


DESTINATION = os.environ.get("CODEX_ACFTRAIN_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_ACFTRAIN_EVIDENCE", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
PACKAGE = "/Game/ExportedAnimations/Anim_KA_Idle53_Seiza_Loop1"
RELATIVE_FILE = os.path.join(
    "ExportedAnimations", "Anim_KA_Idle53_Seiza_Loop1.uasset"
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


def fail(message):
    unreal.log_error("CODEX_ACFTRAIN_ANIMATION_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


if not DESTINATION or not EVIDENCE_PATH or not EXPECTED_TARGET_ROOT:
    fail("CODEX_ACFTRAIN_DESTINATION, CODEX_ACFTRAIN_EVIDENCE, and CODEX_EXPECTED_TARGET_ROOT are required")

destination = os.path.realpath(DESTINATION)
target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
expected_destination = os.path.realpath(os.path.join(target_root, "Content"))
if destination.lower() != expected_destination.lower():
    fail("Destination invariant failed: {!r} != {!r}".format(destination, expected_destination))
if not os.path.isdir(destination):
    fail("Target Content folder does not exist: " + destination)
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Expected target project descriptor is absent")
if not os.path.realpath(EVIDENCE_PATH).lower().startswith(
    os.path.realpath(os.path.join(target_root, "Saved", "Migration")).lower() + os.sep
):
    fail("Evidence path escapes target Saved/Migration")

expected_file = os.path.realpath(os.path.join(destination, RELATIVE_FILE))
if os.path.exists(expected_file):
    fail("Target animation already exists: " + expected_file)
for relative_file in FORBIDDEN_RELATIVE_FILES:
    path = os.path.realpath(os.path.join(destination, relative_file))
    if os.path.exists(path):
        fail("Forbidden dependency was already present before migration: " + path)
protected_hashes = {}
for relative_file, expected_hash in PROTECTED_TARGET_FILES:
    path = os.path.realpath(os.path.join(destination, relative_file))
    if not os.path.isfile(path):
        fail("Protected target dependency is absent: " + path)
    actual_hash = sha256(path)
    if actual_hash != expected_hash:
        fail("Protected target dependency hash differs from baseline: " + path)
    protected_hashes[relative_file] = actual_hash

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/ExportedAnimations"], True)
registry.wait_for_completion()
assets = registry.get_assets_by_package_name(PACKAGE, include_only_on_disk_assets=True)
asset_names = [str(asset.asset_name) for asset in assets]
if "Anim_KA_Idle53_Seiza_Loop1" not in asset_names:
    fail("Staged animation was not registered: " + repr(asset_names))

options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", True)
options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
options.set_editor_property("orphan_folder", "")

unreal.log("CODEX_ACFTRAIN_ANIMATION_MIGRATION_BEGIN: " + PACKAGE)
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    [PACKAGE], destination, options
)

if not os.path.isfile(expected_file):
    fail("AssetTools did not create expected animation: " + expected_file)
for relative_file in FORBIDDEN_RELATIVE_FILES:
    path = os.path.realpath(os.path.join(destination, relative_file))
    if os.path.exists(path):
        fail("AssetTools copied a forbidden dependency: " + path)
for relative_file, expected_hash in PROTECTED_TARGET_FILES:
    path = os.path.realpath(os.path.join(destination, relative_file))
    actual_hash = sha256(path)
    if actual_hash != expected_hash or actual_hash != protected_hashes[relative_file]:
        fail("AssetTools modified protected target dependency: " + path)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_ROOT_ONLY_MIGRATION_PASS",
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "harness_project": unreal.Paths.get_project_file_path(),
    "destination": destination,
    "package": PACKAGE,
    "file": expected_file,
    "length": os.path.getsize(expected_file),
    "sha256": sha256(expected_file),
    "asset_conflict": "SKIP_WITH_ABSENCE_PREFLIGHT",
    "ignore_dependencies": True,
    "forbidden_dependency_count": len(FORBIDDEN_RELATIVE_FILES),
    "forbidden_dependencies_absent": True,
    "protected_target_files": protected_hashes,
    "protected_target_files_unchanged": True,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_ACFTRAIN_ANIMATION_MIGRATION_PASS: " + EVIDENCE_PATH)
