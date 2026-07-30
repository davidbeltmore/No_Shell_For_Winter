"""Migrate exactly fifteen DirtyPawn material contracts through UE 5.7 AssetTools."""

import datetime
import hashlib
import json
import os
import re

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
EXPECTED_CLASS_COUNTS = {"Texture2D": 6, "MaterialFunction": 8, "Material": 1}
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")
DELTA_MARKER = "CODEX_DIRTYPAWN_ASSETS57_TARGET_DELTA_EXACT_JSON:"
HASH_STOP = "UE 5.7 AssetTools output differs from staged source:"

DESTINATION = os.environ.get("CODEX_DIRTYPAWN_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get(
    "CODEX_DIRTYPAWN57_MIGRATION_EVIDENCE", ""
).strip()
RECEIPT_PATH = os.environ.get("CODEX_DIRTYPAWN_RECEIPT", "").strip()
VALIDATION_PATH = os.environ.get(
    "CODEX_DIRTYPAWN57_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()
PRIOR_LOG_PATH = os.environ.get("CODEX_DIRTYPAWN_PRIOR_LOG", "").strip()


def fail(message):
    unreal.log_error("CODEX_DIRTYPAWN_ASSETS57_MIGRATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def batch_fingerprint(rows):
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


def package_file(content_root, package):
    relative = package[len("/Game/") :].replace("/", os.sep) + ".uasset"
    return os.path.realpath(os.path.join(content_root, relative))


def relative_package_file(package):
    return (package[len("/Game/") :] + ".uasset").lower()


def snapshot_package_files(content_root):
    rows = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = os.path.realpath(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/").lower()
            stat = os.stat(path)
            rows[relative] = {"length": stat.st_size, "mtime_ns": stat.st_mtime_ns}
    return rows


def inventory_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(name, rows[name]["length"], rows[name]["mtime_ns"])
        for name in sorted(rows)
    ]
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest().upper()


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


if len(EXPECTED) != EXPECTED_PACKAGE_COUNT:
    raise RuntimeError("Internal DirtyPawn allowlist count differs")
if sum(row["length"] for row in EXPECTED.values()) != EXPECTED_SOURCE_BYTES:
    raise RuntimeError("Internal DirtyPawn byte total differs")
if batch_fingerprint(EXPECTED) != EXPECTED_SOURCE_FINGERPRINT:
    raise RuntimeError("Internal DirtyPawn fingerprint differs")

engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (
        DESTINATION,
        EVIDENCE_PATH,
        RECEIPT_PATH,
        VALIDATION_PATH,
        EXPECTED_TARGET_ROOT,
        EXPECTED_HARNESS_CONTENT,
    )
):
    fail("All DirtyPawn migration environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
destination = os.path.realpath(DESTINATION)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
phase_root = os.path.join(
    target_root, "Saved", "Migration", "Phase4", "DirtyPawnAssets"
)
if destination.lower() != target_content.lower():
    fail("Destination is not the canonical target Content root")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Destination is not the audited target project")
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("Commandlet is not running in the audited isolated harness")
if not is_under(harness_content, phase_root):
    fail("Harness Content escapes the DirtyPawn Saved/Migration root")
if is_under(harness_content, destination):
    fail("Harness Content unexpectedly lives under live target Content")

with open(os.path.realpath(RECEIPT_PATH), "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
with open(os.path.realpath(VALIDATION_PATH), "r", encoding="utf-8-sig") as handle:
    validation = json.load(handle)
if (
    receipt.get("status") != "ISOLATED_DIRTYPAWN_ASSETS57_HARNESS_PASS"
    or receipt.get("package_count") != EXPECTED_PACKAGE_COUNT
    or receipt.get("source_bytes") != EXPECTED_SOURCE_BYTES
    or receipt.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or receipt.get("class_counts") != EXPECTED_CLASS_COUNTS
):
    fail("Harness receipt does not match the frozen baseline")
if (
    validation.get("status") != "UE57_DIRTYPAWN_ASSETS_READ_ONLY_LOAD_PASS"
    or validation.get("package_count") != EXPECTED_PACKAGE_COUNT
    or validation.get("source_bytes") != EXPECTED_SOURCE_BYTES
    or validation.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or validation.get("class_counts") != EXPECTED_CLASS_COUNTS
    or validation.get("unexpected_game_dependencies")
    or validation.get("unexpected_registered_packages")
    or validation.get("redirectors")
    or validation.get("asset_save_requested") is not False
    or validation.get("packages_saved") != 0
):
    fail("UE 5.7 read-only validation gate is not PASS")

receipt_rows = {row["package"]: row for row in receipt.get("staged_assets", [])}
if set(receipt_rows) != set(EXPECTED) or len(receipt_rows) != EXPECTED_PACKAGE_COUNT:
    fail("Harness receipt package set differs from the exact allowlist")
for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    row = receipt_rows[package]
    source_file = os.path.realpath(row["source"])
    staged_file = os.path.realpath(row["staged"])
    expected_target = package_file(destination, package)
    if os.path.realpath(row["target"]).lower() != expected_target.lower():
        fail("Receipt target path differs for " + package)
    for label, path in (("source", source_file), ("staged", staged_file)):
        if not os.path.isfile(path):
            fail("{} asset is absent: {}".format(label, package))
        if os.path.getsize(path) != expected["length"] or sha256(path) != expected[
            "sha256"
        ]:
            fail("{} asset differs from the frozen baseline: {}".format(label, package))

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/_Game/Textures", "/Game/DirtyPawnSystem"], True)
registry.wait_for_completion()
options_dependencies = dependency_options()
validation_dependencies = validation.get("dependencies", {})
dependencies = {}
class_counts = {}
for package in sorted(EXPECTED):
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    primary = [
        item for item in assets if class_name(item) == EXPECTED[package]["class"]
    ]
    if len(primary) != 1:
        fail("Expected one registered primary asset for " + package)
    actual_class = class_name(primary[0])
    class_counts[actual_class] = class_counts.get(actual_class, 0) + 1
    direct = sorted(
        {
            str(value)
            for value in registry.get_dependencies(package, options_dependencies)
        }
    )
    if direct != validation_dependencies.get(package):
        fail("Dependencies changed between validation and migration for " + package)
    dependencies[package] = direct
if class_counts != EXPECTED_CLASS_COUNTS:
    fail("Harness class counts changed before migration: " + repr(class_counts))

expected_paths = {
    package_file(destination, package).lower(): package for package in EXPECTED
}
expected_relatives = sorted(relative_package_file(package) for package in EXPECTED)
existing_expected = {path for path in expected_paths if os.path.exists(path)}
resumed_from_prior_log = False
migration_invoked_this_run = False
prior_log_path = ""
prior_delta = None

if existing_expected:
    if existing_expected != set(expected_paths):
        fail("Refusing a partial pre-existing DirtyPawn target batch")
    if not PRIOR_LOG_PATH:
        fail("Refusing to overwrite a pre-existing DirtyPawn target batch")
    prior_log_path = os.path.realpath(PRIOR_LOG_PATH)
    expected_log_root = os.path.join(target_root, "Saved", "Migration", "Logs")
    if not is_under(prior_log_path, expected_log_root) or not os.path.isfile(
        prior_log_path
    ):
        fail("Prior migration log is absent or escapes target Saved/Migration/Logs")
    with open(prior_log_path, "r", encoding="utf-8-sig", errors="replace") as handle:
        prior_log = handle.read()
    success_tokens = [
        "AssetTools: Package ({}) was migrated successfully as ({})".format(
            package, package
        )
        for package in EXPECTED
    ]
    if any(prior_log.count(token) != 1 for token in success_tokens):
        fail("Prior log does not prove exactly one successful migration per package")
    if prior_log.count("AssetTools: Package (/Game/") != EXPECTED_PACKAGE_COUNT:
        fail("Prior log contains an unexpected AssetTools package count")
    if HASH_STOP not in prior_log:
        fail("Prior log does not contain the audited post-migration hash stop")
    marker_matches = re.findall(
        re.escape(DELTA_MARKER) + r"(\{[^\r\n]+\})", prior_log
    )
    if len(marker_matches) != 1:
        fail("Prior log does not contain exactly one target-delta marker")
    try:
        prior_delta = json.loads(marker_matches[0])
    except Exception as exc:
        fail("Prior target-delta marker is invalid JSON: " + repr(exc))
    if (
        prior_delta.get("created_package_files") != expected_relatives
        or prior_delta.get("removed_package_files")
        or prior_delta.get("modified_existing_package_files")
        or prior_delta.get("target_delta_exact") is not True
    ):
        fail("Prior target-delta marker does not prove the exact batch")
    after = snapshot_package_files(destination)
    after_fingerprint = inventory_fingerprint(after)
    if (
        len(after) != prior_delta.get("target_package_file_count_after")
        or after_fingerprint
        != prior_delta.get("target_inventory_fingerprint_after")
    ):
        fail("Target Content package inventory changed since the audited hash stop")
    before_count = prior_delta["target_package_file_count_before"]
    after_count = prior_delta["target_package_file_count_after"]
    before_fingerprint = prior_delta["target_inventory_fingerprint_before"]
    created = expected_relatives
    removed = []
    modified_existing = []
    resumed_from_prior_log = True
    unreal.log("CODEX_DIRTYPAWN_ASSETS57_MIGRATION_RESUME: " + prior_log_path)
else:
    before = snapshot_package_files(destination)
    before_count = len(before)
    before_fingerprint = inventory_fingerprint(before)
    for relative in expected_relatives:
        if relative in before:
            fail("Target asset appeared before AssetTools migration: " + relative)

    migration_options = unreal.MigrationOptions()
    migration_options.set_editor_property("prompt", False)
    migration_options.set_editor_property("ignore_dependencies", True)
    migration_options.set_editor_property(
        "asset_conflict", unreal.AssetMigrationConflict.SKIP
    )
    migration_options.set_editor_property("orphan_folder", "")

    unreal.log("CODEX_DIRTYPAWN_ASSETS57_MIGRATION_BEGIN: packages=15")
    unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
        list(sorted(EXPECTED)), destination, migration_options
    )
    migration_invoked_this_run = True

    after = snapshot_package_files(destination)
    after_count = len(after)
    after_fingerprint = inventory_fingerprint(after)
    created = sorted(set(after) - set(before))
    removed = sorted(set(before) - set(after))
    modified_existing = sorted(
        name
        for name in set(before).intersection(after)
        if before[name] != after[name]
    )
    if created != expected_relatives or removed or modified_existing:
        fail(
            "AssetTools target delta differs; created={!r}, removed={!r}, "
            "modified={!r}".format(created, removed, modified_existing)
        )
    prior_delta = {
        "created_package_files": created,
        "modified_existing_package_files": modified_existing,
        "removed_package_files": removed,
        "target_delta_exact": True,
        "target_inventory_fingerprint_after": after_fingerprint,
        "target_inventory_fingerprint_before": before_fingerprint,
        "target_package_file_count_after": after_count,
        "target_package_file_count_before": before_count,
    }
    unreal.log(DELTA_MARKER + json.dumps(prior_delta, sort_keys=True))

package_rows = []
target_fingerprint_rows = {}
mismatched_packages = []
for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    path = package_file(destination, package)
    if not os.path.isfile(path):
        fail("AssetTools did not create target package: " + package)
    length = os.path.getsize(path)
    current_sha256 = sha256(path)
    bytes_match_source = (
        length == expected["length"] and current_sha256 == expected["sha256"]
    )
    if not bytes_match_source:
        mismatched_packages.append(package)
    for extension in (".uexp", ".ubulk", ".uptnl"):
        sidecar = os.path.splitext(path)[0] + extension
        if os.path.exists(sidecar):
            fail("Unexpected target package sidecar exists: " + sidecar)
    target_fingerprint_rows[package] = {
        "length": length,
        "sha256": current_sha256,
    }
    package_rows.append(
        {
            "package": package,
            "class": expected["class"],
            "file": path,
            "length": length,
            "sha256": current_sha256,
            "source_length": expected["length"],
            "source_sha256": expected["sha256"],
            "bytes_match_source": bytes_match_source,
        }
    )
    receipt_row = receipt_rows[package]
    for label in ("source", "staged"):
        frozen_path = os.path.realpath(receipt_row[label])
        if (
            os.path.getsize(frozen_path) != expected["length"]
            or sha256(frozen_path) != expected["sha256"]
        ):
            fail("{} bytes changed during migration: {}".format(label, package))

if mismatched_packages and not resumed_from_prior_log:
    fail(HASH_STOP + " " + mismatched_packages[0])

evidence_path = os.path.realpath(EVIDENCE_PATH)
if not is_under(evidence_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Migration evidence escapes target Saved/Migration")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_DIRTYPAWN_ASSETS_MIGRATION_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "harness_content": harness_content,
    "destination": destination,
    "package_count": len(package_rows),
    "created_package_count": len(created),
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "target_bytes_after_assettools": sum(
        row["length"] for row in target_fingerprint_rows.values()
    ),
    "target_fingerprint_after_assettools": batch_fingerprint(
        target_fingerprint_rows
    ),
    "byte_identical_package_count": sum(
        1 for row in package_rows if row["bytes_match_source"]
    ),
    "reserialized_package_count": len(mismatched_packages),
    "reserialized_packages": mismatched_packages,
    "class_counts": class_counts,
    "packages": package_rows,
    "dependencies": dependencies,
    "unexpected_game_dependencies": [],
    "ignore_dependencies": True,
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "target_inventory_algorithm": "lowercase relative package path|length|mtime_ns",
    "target_package_file_count_before": before_count,
    "target_package_file_count_after": after_count,
    "target_inventory_fingerprint_before": before_fingerprint,
    "target_inventory_fingerprint_after": after_fingerprint,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_existing_package_files": modified_existing,
    "target_delta_exact": True,
    "migration_invoked_this_run": migration_invoked_this_run,
    "resumed_from_prior_log": resumed_from_prior_log,
    "prior_migration_log": prior_log_path,
    "source_tree_mounted": False,
    "raw_target_asset_copy_requested": False,
    "source_assets_unchanged": True,
    "staged_assets_unchanged": True,
    "target_sidecars_created": [],
    "post_migration_source_protected_gate": "PENDING_EXTERNAL_GATE",
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DIRTYPAWN_ASSETS57_MIGRATION_PASS: " + evidence_path)
