"""Read-only UE 5.7 validation for the exact DoorToLevel visual closure."""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_PACKAGE_COUNT = 4
EXPECTED_SOURCE_BYTES = 189704
EXPECTED_SOURCE_FINGERPRINT = (
    "4477D83F3722FA80674C18791BB2A85DCCE5DBE19FD57D17B52C20BE716212CC"
)
EXPECTED = {
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial": {
        "class": "Material",
        "length": 15251,
        "sha256": "1DA354F36752F99E8741529372A580CB4B174C390DB080A62037E55CC8771941",
        "dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette",
            "/Script/Engine",
        },
    },
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal": {
        "class": "Material",
        "length": 19454,
        "sha256": "8722C616F22B81315E266A471785B4169F45F2E9CCEF5229F08E27A6ED824B23",
        "dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette",
            "/Script/Engine",
        },
    },
    "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette": {
        "class": "Texture2D",
        "length": 100800,
        "sha256": "515E0A851F19035D612620101F32133243442FFAEF0F0DC93FA049F0C931D26B",
        "dependencies": {"/Script/Engine"},
    },
    "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors": {
        "class": "StaticMesh",
        "length": 54199,
        "sha256": "40AF92CF0E91C356B13AB171065C17A649CD874BBB6724A29793A7B91EB8A3A7",
        "dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial",
            "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal",
            "/Script/Engine",
            "/Script/MeshDescription",
            "/Script/NavigationSystem",
            "/Script/PhysicsCore",
            "/Script/UnrealEd",
        },
    },
}
LOAD_ORDER = (
    "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial",
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal",
    "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors",
)

RECEIPT_PATH = os.environ.get("CODEX_DOOR57_RECEIPT", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_DOOR57_VALIDATION_EVIDENCE", "").strip()
RUN_ID = os.environ.get("CODEX_DOOR_RUN_ID", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_DOOR_VISUAL57_VALIDATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    normalized_path = os.path.realpath(path).lower()
    normalized_root = os.path.realpath(root).rstrip(os.sep).lower() + os.sep
    return normalized_path.startswith(normalized_root)


def fingerprint(rows):
    lines = [
        "{}|{}|{}".format(package, rows[package]["length"], rows[package]["sha256"])
        for package in sorted(rows)
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


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (
        RECEIPT_PATH,
        EVIDENCE_PATH,
        RUN_ID,
        EXPECTED_TARGET_ROOT,
        EXPECTED_HARNESS_CONTENT,
    )
):
    fail("All DoorToLevel UE 5.7 validation environment variables are required")
if not (8 <= len(RUN_ID) <= 64) or not all(
    character.isalnum() or character in "_-" for character in RUN_ID
):
    fail("DoorToLevel run_id is invalid")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
run_root = os.path.realpath(
    os.path.join(
        target_root,
        "Saved",
        "Migration",
        "Phase4",
        "DoorToLevel",
        "Runs",
        RUN_ID,
    )
)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("Commandlet is not running in the audited DoorToLevel harness")
if not is_under(harness_content, run_root):
    fail("Harness Content escapes the immutable DoorToLevel run root")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Expected target project descriptor is absent")

receipt_path = os.path.realpath(RECEIPT_PATH)
evidence_path = os.path.realpath(EVIDENCE_PATH)
expected_receipt_path = os.path.realpath(
    os.path.join(run_root, "DoorToLevel57HarnessReceipt.json")
)
expected_evidence_path = os.path.realpath(
    os.path.join(run_root, "DoorToLevelVisualAssets57Validation.json")
)
if receipt_path.lower() != expected_receipt_path.lower():
    fail("Harness receipt is not the immutable run receipt")
if evidence_path.lower() != expected_evidence_path.lower():
    fail("Validation evidence is not the immutable run evidence path")
if os.path.exists(evidence_path):
    fail("Validation evidence already exists for this immutable run")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
receipt_sha256 = sha256(receipt_path)
if (
    receipt.get("status") != "ISOLATED_DOOR_VISUAL57_HARNESS_PASS"
    or receipt.get("run_id") != RUN_ID
    or receipt.get("package_count") != EXPECTED_PACKAGE_COUNT
    or receipt.get("source_bytes") != EXPECTED_SOURCE_BYTES
    or receipt.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or receipt.get("source_tree_mounted") is not False
    or receipt.get("source_package_saves") != 0
    or receipt.get("target_content_writes") != 0
):
    fail("Harness receipt does not match the frozen four-package contract")
if os.path.realpath(receipt.get("harness_content", "")).lower() != harness_content.lower():
    fail("Harness receipt names a different Content root")
if os.path.realpath(receipt.get("target_root", "")).lower() != target_root.lower():
    fail("Harness receipt names a different target root")
if os.path.realpath(receipt.get("run_root", "")).lower() != run_root.lower():
    fail("Harness receipt names a different immutable run root")
if receipt.get("legacy_blueprint_reference", {}).get("staged") is not False:
    fail("Legacy DoorToLevel Blueprint must not be staged")
if receipt.get("legacy_blueprint_reference", {}).get("migration_requested") is not False:
    fail("Legacy DoorToLevel Blueprint migration must remain disabled")

pre_stage = receipt.get("pre_stage_safety", {})
pre_stage_path = os.path.realpath(pre_stage.get("evidence", ""))
expected_pre_stage_path = os.path.realpath(
    os.path.join(run_root, "Gates", "PRE_STAGE_SafetyGate.json")
)
if pre_stage_path.lower() != expected_pre_stage_path.lower():
    fail("Receipt points to a different PRE_STAGE gate")
if not os.path.isfile(pre_stage_path):
    fail("PRE_STAGE gate evidence is absent")
pre_stage_sha256 = sha256(pre_stage_path)
if pre_stage.get("evidence_sha256") != pre_stage_sha256:
    fail("Receipt PRE_STAGE gate hash differs")
with open(pre_stage_path, "r", encoding="utf-8-sig") as handle:
    pre_stage_evidence = json.load(handle)
if (
    pre_stage_evidence.get("status")
    != "DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS"
    or pre_stage_evidence.get("stage") != "PRE_STAGE"
    or pre_stage_evidence.get("run_id") != RUN_ID
):
    fail("PRE_STAGE evidence is not a PASS for the current run")
for label, node_name in (
    ("source", "source_read_only"),
    ("protected", "protected_invariants"),
):
    node = pre_stage_evidence.get(node_name, {})
    nested_path = os.path.realpath(node.get("evidence", ""))
    if (
        not os.path.isfile(nested_path)
        or node.get("evidence_sha256") != sha256(nested_path)
    ):
        fail("PRE_STAGE nested {} evidence hash differs".format(label))

staged_rows = {row["package"]: row for row in receipt.get("staged_assets", [])}
if set(staged_rows) != set(EXPECTED):
    fail("Harness staged package set differs from the exact visual allowlist")

physical_packages = []
for root, _directories, files in os.walk(harness_content):
    for name in files:
        if name.lower().endswith((".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")):
            physical_packages.append(os.path.realpath(os.path.join(root, name)))
if len(physical_packages) != EXPECTED_PACKAGE_COUNT:
    fail("Harness does not contain exactly four package files: " + repr(physical_packages))

source_snapshot = {}
staged_snapshot_before = {}
for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    row = staged_rows[package]
    source_file = os.path.realpath(row.get("source", ""))
    staged_file = os.path.realpath(row.get("staged", ""))
    target_file = os.path.realpath(row.get("target", ""))
    if is_under(source_file, target_root):
        fail("Receipt source unexpectedly points inside target: " + package)
    if not is_under(staged_file, harness_content):
        fail("Staged asset escapes harness Content: " + package)
    if target_file.lower() == staged_file.lower() or is_under(target_file, harness_content):
        fail("Canonical target path unexpectedly points into harness: " + package)
    for label, path in (("source", source_file), ("staged", staged_file)):
        if not os.path.isfile(path):
            fail("{} asset is absent: {}".format(label, package))
        if os.path.getsize(path) != expected["length"] or sha256(path) != expected["sha256"]:
            fail("{} asset differs from frozen baseline: {}".format(label, package))
    if os.path.exists(target_file):
        fail("Target visual collision appeared before migration: " + package)
    source_snapshot[package] = {
        "length": os.path.getsize(source_file),
        "sha256": sha256(source_file),
    }
    staged_snapshot_before[package] = {
        "length": os.path.getsize(staged_file),
        "sha256": sha256(staged_file),
    }
if fingerprint(source_snapshot) != EXPECTED_SOURCE_FINGERPRINT:
    fail("Source visual fingerprint differs")
if fingerprint(staged_snapshot_before) != EXPECTED_SOURCE_FINGERPRINT:
    fail("Staged visual fingerprint differs")

legacy = receipt.get("legacy_blueprint_reference", {})
legacy_source = os.path.realpath(legacy.get("source", ""))
legacy_target = os.path.realpath(legacy.get("target", ""))
if (
    not os.path.isfile(legacy_source)
    or os.path.getsize(legacy_source) != 53280
    or sha256(legacy_source)
    != "7EDF9F4A24D14F03AF2AE3F6A111696CF4AAC79052C225BCE90429D06935D016"
):
    fail("Legacy DoorToLevel read-only reference differs from the frozen baseline")
if os.path.exists(legacy_target):
    fail("Target DoorToLevel Blueprint appeared before the UE 5.8 rebuild")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    [
        "/Game/Calysto/Dungeon/Demo/LowPoly",
        "/Game/Calysto/Dungeon/Mesh/DungeonMesh",
    ],
    True,
)
registry.wait_for_completion()
options = dependency_options()

registered = {}
dependencies = {}
loaded = []
for package in LOAD_ORDER:
    expected = EXPECTED[package]
    primary_assets = [
        asset
        for asset in registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
        if class_name(asset) == expected["class"]
    ]
    if len(primary_assets) != 1:
        fail("Expected one registered {} for {}".format(expected["class"], package))
    asset_data = primary_assets[0]
    actual_dependencies = sorted(
        {str(value) for value in registry.get_dependencies(package, options)}
    )
    if set(actual_dependencies) != expected["dependencies"]:
        fail("Frozen dependency set differs for {}: {!r}".format(package, actual_dependencies))
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None or asset.get_class().get_name() != expected["class"]:
        fail("Read-only UE 5.7 load/class gate failed for " + package)
    registered[package] = {
        "object_path": package + "." + package.rsplit("/", 1)[-1],
        "class": class_name(asset_data),
    }
    dependencies[package] = actual_dependencies
    loaded.append(
        {
            "package": package,
            "object_path": asset.get_path_name(),
            "class": asset.get_class().get_name(),
        }
    )

staged_snapshot_after = {}
for package in sorted(EXPECTED):
    path = os.path.realpath(staged_rows[package]["staged"])
    staged_snapshot_after[package] = {
        "length": os.path.getsize(path),
        "sha256": sha256(path),
    }
if staged_snapshot_after != staged_snapshot_before:
    fail("Read-only UE 5.7 load changed staged package bytes")
for package in sorted(EXPECTED):
    source_file = os.path.realpath(staged_rows[package]["source"])
    if (
        os.path.getsize(source_file) != EXPECTED[package]["length"]
        or sha256(source_file) != EXPECTED[package]["sha256"]
    ):
        fail("Source package changed during UE 5.7 validation: " + package)

payload = {
    "schema_version": 2,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_DOOR_VISUAL_READ_ONLY_LOAD_PASS",
    "run_id": RUN_ID,
    "engine_version": engine_version,
    "harness_project": os.path.realpath(unreal.Paths.get_project_file_path()),
    "harness_content": harness_content,
    "run_root": run_root,
    "receipt": receipt_path,
    "receipt_sha256": receipt_sha256,
    "pre_stage_gate": pre_stage_path,
    "pre_stage_gate_sha256": pre_stage_sha256,
    "package_count": EXPECTED_PACKAGE_COUNT,
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "class_counts": {"Material": 2, "StaticMesh": 1, "Texture2D": 1},
    "registered_assets": registered,
    "dependencies": dependencies,
    "loaded_assets": loaded,
    "source_assets_unchanged": True,
    "staged_assets_unchanged": True,
    "source_tree_mounted": False,
    "legacy_blueprint_loaded": False,
    "legacy_blueprint_staged": False,
    "asset_save_requested": False,
    "packages_saved": 0,
    "target_content_writes": 0,
    "pre_stage_source_protected_gate": "PASS",
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DOOR_VISUAL57_VALIDATION_PASS: " + evidence_path)
