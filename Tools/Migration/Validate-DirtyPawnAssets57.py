"""Read-only UE 5.7 validation for the exact fifteen-package DirtyPawn batch."""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_PACKAGE_COUNT = 15
EXPECTED_SOURCE_BYTES = 70130437
EXPECTED_SOURCE_FINGERPRINT = (
    "8F58CFD389DA0D63D2633D372C5DE251496EC53F53EEC15FE51AB160C46AEC36"
)
ASSETS = (
    ("/Game/_Game/Textures/leakage_sfhkcazc_4k/T_Leakage_sfhkcazc_2K_Mask", "Texture2D", 3103406, "524C8FA141E6822957DB8FB078162D23A1EBE51174B7352A0437C8AE1211FF6A"),
    ("/Game/DirtyPawnSystem/Demo/Character/Characters/Mannequins/Textures/Manny/T_Manny_01_D", "Texture2D", 5671240, "47CF420ED20BFE200907775D89FB1A08F6D8FAF19A90C4DD9C46F2FB722DC23A"),
    ("/Game/DirtyPawnSystem/Demo/StarterContent/Textures/T_Perlin_Noise_M", "Texture2D", 6804350, "9709239B17825D08BC603D4F9321D209AD980D4A1FB91242C5971BAFDA6ED421"),
    ("/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper", "Material", 255891, "392D4DDFE8E8DD00278364E2F71385960720257614BB30679135C073B417D0F9"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Blood/MF_Blood_Mask", "MaterialFunction", 22408, "57BDD2550E8A1586A9999CAB8307CDB4BA52A926C9B0B9EDF2FCD66045BB852E"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SandSnow_Mask", "MaterialFunction", 23681, "56FBDDDD6762F8CD54DAF49BE8B8B4EA098BD08626C79926FEDB35AA4CDFEA7D"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SkinnedHeightMask", "MaterialFunction", 12443, "02D08E46CC2DE5E92BD9559EDF802DA42FD42460E313F7B754CE0F129DD969F6"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Mud/MF_MudHeight_BaseColor", "MaterialFunction", 30871, "191537151EAD8A64A90F18C7BF19BD5711DDBAF7C5CEE9A2B122706F1CFA6124"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_BaseColor", "MaterialFunction", 20559, "1370B08DEB27C2103029D615080E3A130AC0B50F3B62FAC78164A17B64F294B7"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Normal", "MaterialFunction", 21777, "47CDF279EC8493331CACD9FFCB05754A0FD3F303E4786063E450BB1AE0839665"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Roughness", "MaterialFunction", 28930, "59D6D1E3B53A7AC68318BB618398C988C1A06FC3AFD72CC691535ECDA20AC4CB"),
    ("/Game/DirtyPawnSystem/Materials/Functions/DPS/Smear/MF_Smear_Mask", "MaterialFunction", 24293, "D75FDB123DEADA3C94BE1EC96D41370450784E7F6D103A84AE515D043BC8EDDA"),
    ("/Game/DirtyPawnSystem/Textures/T_BloodScratch_Alpha", "Texture2D", 3151543, "B47114C5A32143F9D904E37D64CBB1DB3EEF0BC4F3C2E8F72CA9EEB0C3DB58F2"),
    ("/Game/DirtyPawnSystem/Textures/T_Noise_Normal", "Texture2D", 44168853, "4FCDADCE5A057901DC58A2D01C4F1222788A57776CEB9C300237C96A5962E252"),
    ("/Game/DirtyPawnSystem/Textures/T_Perlin_Noise_M", "Texture2D", 6790192, "A723B32150877CFFE0C0CCD63012645A6F164A7EA3060F997DC02F9B04FE233A"),
)
EXPECTED = {
    package: {"class": asset_class, "length": length, "sha256": digest}
    for package, asset_class, length, digest in ASSETS
}
EXPECTED_ORDER = tuple(row[0] for row in ASSETS)
WRAPPER = "/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper"
ENGINE_DEFAULTS = {
    "/Engine/EngineMaterials/DefaultDiffuse",
    "/Engine/EngineMaterials/T_Default_Normal",
    "/Engine/EngineResources/DefaultTexture",
    "/Engine/Functions/Engine_MaterialFunctions01/Gradient/LinearGradient",
    "/Engine/Functions/Engine_MaterialFunctions01/ImageAdjustment/CheapContrast",
    "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture",
}
EXPECTED_DEPENDENCIES = {
    ASSETS[0][0]: {"/Script/InterchangeEngine"},
    ASSETS[1][0]: set(),
    ASSETS[2][0]: set(),
    WRAPPER: ENGINE_DEFAULTS | (set(EXPECTED) - {WRAPPER}),
    ASSETS[4][0]: {
        "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture",
        ASSETS[12][0],
    },
    ASSETS[5][0]: {
        "/Engine/Functions/Engine_MaterialFunctions01/ImageAdjustment/CheapContrast",
        "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture",
        ASSETS[2][0],
    },
    ASSETS[6][0]: set(),
    ASSETS[7][0]: {ASSETS[1][0], ASSETS[6][0]},
    ASSETS[8][0]: {ASSETS[5][0], ASSETS[6][0]},
    ASSETS[9][0]: {ASSETS[5][0], ASSETS[6][0], ASSETS[13][0]},
    ASSETS[10][0]: {ASSETS[5][0], ASSETS[6][0]},
    ASSETS[11][0]: {
        "/Engine/Functions/Engine_MaterialFunctions01/ImageAdjustment/CheapContrast",
        "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture",
        ASSETS[2][0],
    },
    ASSETS[12][0]: {"/Script/InterchangeEngine"},
    ASSETS[13][0]: {"/Script/InterchangeEngine"},
    ASSETS[14][0]: set(),
}
EXPECTED_CLASS_COUNTS = {"Texture2D": 6, "MaterialFunction": 8, "Material": 1}

RECEIPT_PATH = os.environ.get("CODEX_DIRTYPAWN_RECEIPT", "").strip()
OUTPUT_PATH = os.environ.get(
    "CODEX_DIRTYPAWN57_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_DIRTYPAWN_ASSETS57_VALIDATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def source_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(
            package, rows[package]["length"], rows[package]["sha256"].upper()
        )
        for package in EXPECTED_ORDER
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


def is_under(path, root):
    normalized_path = os.path.realpath(path).lower()
    normalized_root = os.path.realpath(root).rstrip(os.sep).lower() + os.sep
    return normalized_path.startswith(normalized_root)


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for property_name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        try:
            options.set_editor_property(property_name, True)
        except Exception:
            pass
    try:
        options.set_editor_property("include_searchable_names", False)
    except Exception:
        pass
    return options


def dirty_content_packages():
    try:
        packages = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    except Exception as exc:
        return [], repr(exc)
    result = []
    for package in packages:
        try:
            result.append(package.get_name())
        except Exception:
            result.append(str(package))
    return sorted(set(result)), ""


if len(EXPECTED) != EXPECTED_PACKAGE_COUNT:
    raise RuntimeError("Internal DirtyPawn allowlist count differs")
if sum(row["length"] for row in EXPECTED.values()) != EXPECTED_SOURCE_BYTES:
    raise RuntimeError("Internal DirtyPawn byte total differs")
if source_fingerprint(EXPECTED) != EXPECTED_SOURCE_FINGERPRINT:
    raise RuntimeError("Internal DirtyPawn fingerprint differs")
if set(EXPECTED_DEPENDENCIES) != set(EXPECTED):
    raise RuntimeError("Internal dependency map differs from the exact allowlist")

engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (RECEIPT_PATH, OUTPUT_PATH, EXPECTED_TARGET_ROOT, EXPECTED_HARNESS_CONTENT)
):
    fail("All DirtyPawn validation environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
expected_harness_content = os.path.realpath(EXPECTED_HARNESS_CONTENT)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
phase_root = os.path.join(
    target_root, "Saved", "Migration", "Phase4", "DirtyPawnAssets"
)
if harness_content.lower() != expected_harness_content.lower():
    fail("Commandlet is not running in the audited isolated harness")
if not is_under(harness_content, phase_root):
    fail("Harness Content escapes the DirtyPawn Saved/Migration root")
if is_under(harness_content, target_content):
    fail("Harness Content unexpectedly lives under live target Content")
if os.path.basename(unreal.Paths.get_project_file_path()).lower() != (
    "dirtypawnassets57harness.uproject"
):
    fail("Unexpected harness project descriptor")

receipt_path = os.path.realpath(RECEIPT_PATH)
if not is_under(receipt_path, phase_root):
    fail("Harness receipt escapes the batch evidence root")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if (
    receipt.get("status") != "ISOLATED_DIRTYPAWN_ASSETS57_HARNESS_PASS"
    or receipt.get("package_count") != EXPECTED_PACKAGE_COUNT
    or receipt.get("source_bytes") != EXPECTED_SOURCE_BYTES
    or receipt.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or receipt.get("class_counts") != EXPECTED_CLASS_COUNTS
):
    fail("Harness receipt does not match the frozen batch contract")
if os.path.realpath(receipt.get("harness_content", "")).lower() != (
    harness_content.lower()
):
    fail("Harness Content differs from its receipt")

receipt_rows = {row["package"]: row for row in receipt.get("staged_assets", [])}
if set(receipt_rows) != set(EXPECTED) or len(receipt_rows) != EXPECTED_PACKAGE_COUNT:
    fail("Harness receipt package set differs from the exact allowlist")
for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    row = receipt_rows[package]
    source_file = os.path.realpath(row.get("source", ""))
    staged_file = os.path.realpath(row.get("staged", ""))
    target_file = os.path.realpath(row.get("target", ""))
    if is_under(source_file, target_root):
        fail("Receipt source unexpectedly points inside the target: " + package)
    if not is_under(staged_file, harness_content):
        fail("Staged asset escapes harness Content: " + package)
    expected_target = os.path.realpath(
        os.path.join(
            target_content,
            package[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )
    if target_file.lower() != expected_target.lower():
        fail("Receipt target path differs: " + package)
    for label, path in (("source", source_file), ("staged", staged_file)):
        if not os.path.isfile(path):
            fail("{} asset is absent: {}".format(label, package))
        if (
            os.path.getsize(path) != expected["length"]
            or sha256(path) != expected["sha256"]
            or row.get("length") != expected["length"]
            or row.get("sha256") != expected["sha256"]
            or row.get("class") != expected["class"]
        ):
            fail("{} asset differs from the frozen baseline: {}".format(label, package))
    if os.path.exists(target_file):
        fail("Target collision exists before UE 5.7 validation: " + package)

pre_stage = receipt.get("pre_stage_safety", {})
pre_stage_path = os.path.realpath(pre_stage.get("evidence", ""))
if not is_under(pre_stage_path, os.path.join(target_root, "Saved", "Migration")):
    fail("PRE_STAGE safety evidence escapes target Saved/Migration")
if not os.path.isfile(pre_stage_path) or sha256(pre_stage_path) != pre_stage.get(
    "evidence_sha256"
):
    fail("PRE_STAGE safety evidence is absent or changed")
with open(pre_stage_path, "r", encoding="utf-8-sig") as handle:
    pre_stage_payload = json.load(handle)
if (
    pre_stage_payload.get("status")
    != "DIRTYPAWN_ASSETS_SOURCE_PROTECTED_SAFETY_PASS"
    or pre_stage_payload.get("stage") != "PRE_STAGE"
):
    fail("PRE_STAGE source/protected safety gate is not PASS")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/_Game/Textures", "/Game/DirtyPawnSystem"], True)
registry.wait_for_completion()
all_game_assets = registry.get_assets_by_path(
    "/Game", recursive=True, include_only_on_disk_assets=True
)
registered_game_packages = sorted({str(item.package_name) for item in all_game_assets})
unexpected_registered_packages = sorted(set(registered_game_packages) - set(EXPECTED))
missing_registered_packages = sorted(set(EXPECTED) - set(registered_game_packages))
if unexpected_registered_packages or missing_registered_packages:
    fail(
        "Harness /Game inventory differs; unexpected={!r}, missing={!r}".format(
            unexpected_registered_packages, missing_registered_packages
        )
    )

options = dependency_options()
dependencies = {}
class_counts = {}
loaded_rows = []
dirty_before, dirty_before_error = dirty_content_packages()
for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    redirectors = [item for item in assets if class_name(item) == "ObjectRedirector"]
    primary = [item for item in assets if class_name(item) == expected["class"]]
    if redirectors or len(primary) != 1:
        fail("Expected one primary asset and no redirector for " + package)
    actual_dependencies = sorted(
        {str(value) for value in registry.get_dependencies(package, options)}
    )
    dependencies[package] = actual_dependencies
    if set(actual_dependencies) != EXPECTED_DEPENDENCIES[package]:
        fail(
            "Dependency set differs for {}: {!r}".format(
                package, actual_dependencies
            )
        )
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Asset did not load in UE 5.7: " + package)
    actual_class = asset.get_class().get_name()
    if actual_class != expected["class"]:
        fail(
            "Loaded class differs for {}: {} != {}".format(
                package, actual_class, expected["class"]
            )
        )
    class_counts[actual_class] = class_counts.get(actual_class, 0) + 1
    loaded_rows.append(
        {
            "package": package,
            "class": actual_class,
            "object_path": asset.get_path_name(),
        }
    )
if class_counts != EXPECTED_CLASS_COUNTS:
    fail("UE 5.7 class counts differ: " + repr(class_counts))

dirty_after, dirty_after_error = dirty_content_packages()
dirty_allowlisted = sorted(set(dirty_after).intersection(EXPECTED))
if dirty_allowlisted:
    fail("Read-only loads marked allowlisted packages dirty: " + repr(dirty_allowlisted))
for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    row = receipt_rows[package]
    for label in ("source", "staged"):
        path = os.path.realpath(row[label])
        if os.path.getsize(path) != expected["length"] or sha256(path) != expected[
            "sha256"
        ]:
            fail("{} bytes changed during read-only load: {}".format(label, package))
    if os.path.exists(os.path.realpath(row["target"])):
        fail("Target asset appeared during read-only validation: " + package)

output_path = os.path.realpath(OUTPUT_PATH)
if not is_under(output_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Validation evidence escapes target Saved/Migration")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_DIRTYPAWN_ASSETS_READ_ONLY_LOAD_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "harness_content": harness_content,
    "package_count": EXPECTED_PACKAGE_COUNT,
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "class_counts": class_counts,
    "packages": loaded_rows,
    "dependencies": dependencies,
    "unexpected_game_dependencies": [],
    "unexpected_registered_packages": [],
    "redirectors": [],
    "dirty_content_packages_before_load": dirty_before,
    "dirty_content_packages_after_load": dirty_after,
    "dirty_content_probe_error_before": dirty_before_error,
    "dirty_content_probe_error_after": dirty_after_error,
    "dirty_allowlisted_packages_after_load": [],
    "asset_load_requested": True,
    "material_compile_requested": False,
    "asset_save_requested": False,
    "packages_saved": 0,
    "source_tree_mounted": False,
    "target_content_writes": 0,
    "pre_stage_source_protected_gate": "PASS",
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DIRTYPAWN_ASSETS57_VALIDATION_PASS: " + output_path)
