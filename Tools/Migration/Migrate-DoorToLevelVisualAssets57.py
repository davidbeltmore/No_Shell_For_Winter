"""Migrate exactly four DoorToLevel visual dependencies through UE 5.7 AssetTools."""

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
        "relative": "calysto/dungeon/demo/lowpoly/material/m_basematerial.uasset",
        "dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette",
            "/Script/Engine",
        },
    },
    "/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal": {
        "class": "Material",
        "length": 19454,
        "sha256": "8722C616F22B81315E266A471785B4169F45F2E9CCEF5229F08E27A6ED824B23",
        "relative": "calysto/dungeon/demo/lowpoly/material/m_metal.uasset",
        "dependencies": {
            "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette",
            "/Script/Engine",
        },
    },
    "/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette": {
        "class": "Texture2D",
        "length": 100800,
        "sha256": "515E0A851F19035D612620101F32133243442FFAEF0F0DC93FA049F0C931D26B",
        "relative": "calysto/dungeon/demo/lowpoly/texture/t_palette.uasset",
        "dependencies": {"/Script/Engine"},
    },
    "/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors": {
        "class": "StaticMesh",
        "length": 54199,
        "sha256": "40AF92CF0E91C356B13AB171065C17A649CD874BBB6724A29793A7B91EB8A3A7",
        "relative": "calysto/dungeon/mesh/dungeonmesh/sm_squaredarchedwoodendoors.uasset",
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
PACKAGE_EXTENSIONS = (".uasset", ".umap", ".uexp", ".ubulk", ".uptnl")

DESTINATION = os.environ.get("CODEX_DOOR57_DESTINATION", "").strip()
EVIDENCE_PATH = os.environ.get("CODEX_DOOR57_MIGRATION_EVIDENCE", "").strip()
RECEIPT_PATH = os.environ.get("CODEX_DOOR57_RECEIPT", "").strip()
VALIDATION_PATH = os.environ.get("CODEX_DOOR57_VALIDATION_EVIDENCE", "").strip()
PRE_MIGRATION_GATE_PATH = os.environ.get(
    "CODEX_DOOR57_PRE_MIGRATION_GATE", ""
).strip()
RUN_ID = os.environ.get("CODEX_DOOR_RUN_ID", "").strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_DOOR_VISUAL57_MIGRATION_FAIL: " + message)
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


def snapshot_package_files(content_root):
    rows = {}
    for root, _directories, files in os.walk(content_root):
        for name in files:
            if not name.lower().endswith(PACKAGE_EXTENSIONS):
                continue
            path = os.path.realpath(os.path.join(root, name))
            relative = os.path.relpath(path, content_root).replace(os.sep, "/").lower()
            stat = os.stat(path)
            rows[relative] = {
                "length": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
                "sha256": sha256(path),
            }
    return rows


def snapshot_fingerprint(rows):
    lines = [
        "{}|{}|{}".format(name, rows[name]["length"], rows[name]["sha256"])
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


def dirty_packages():
    result = {}
    for label, method in (
        ("content", unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages),
        ("maps", unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages),
    ):
        try:
            packages = method()
        except Exception as exc:
            fail("Dirty-{} probe failed closed: {!r}".format(label, exc))
        names = []
        for package in packages:
            try:
                names.append(package.get_name())
            except Exception as exc:
                fail("Could not name a dirty {} package: {!r}".format(label, exc))
        result[label] = sorted(set(names))
    return result


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (
        DESTINATION,
        EVIDENCE_PATH,
        RECEIPT_PATH,
        VALIDATION_PATH,
        PRE_MIGRATION_GATE_PATH,
        RUN_ID,
        EXPECTED_TARGET_ROOT,
        EXPECTED_HARNESS_CONTENT,
    )
):
    fail("All DoorToLevel UE 5.7 migration environment variables are required")
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
destination = os.path.realpath(DESTINATION)
target_content = os.path.realpath(os.path.join(target_root, "Content"))
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if destination.lower() != target_content.lower():
    fail("Destination is not the canonical target Content root")
if not is_under(destination, target_root):
    fail("Canonical target Content resolves outside the target project root")
if not os.path.isfile(os.path.join(target_root, "NoShellForWinter.uproject")):
    fail("Destination is not the audited target project")
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("Commandlet is not running in the audited isolated harness")
if not is_under(harness_content, run_root):
    fail("Harness Content escapes the immutable DoorToLevel run root")
if is_under(harness_content, destination):
    fail("Harness Content unexpectedly lives under live target Content")

receipt_path = os.path.realpath(RECEIPT_PATH)
validation_path = os.path.realpath(VALIDATION_PATH)
pre_migration_gate_path = os.path.realpath(PRE_MIGRATION_GATE_PATH)
evidence_path = os.path.realpath(EVIDENCE_PATH)
expected_paths = {
    "receipt": os.path.realpath(
        os.path.join(run_root, "DoorToLevel57HarnessReceipt.json")
    ),
    "validation": os.path.realpath(
        os.path.join(run_root, "DoorToLevelVisualAssets57Validation.json")
    ),
    "pre_migration_gate": os.path.realpath(
        os.path.join(run_root, "Gates", "PRE_MIGRATION57_SafetyGate.json")
    ),
    "migration": os.path.realpath(
        os.path.join(run_root, "DoorToLevelVisualAssets57Migration.json")
    ),
}
for label, actual in (
    ("receipt", receipt_path),
    ("validation", validation_path),
    ("pre_migration_gate", pre_migration_gate_path),
    ("migration", evidence_path),
):
    if actual.lower() != expected_paths[label].lower():
        fail("{} path is not the immutable run path".format(label))
if os.path.exists(evidence_path):
    fail("Migration evidence already exists for this immutable run")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
with open(validation_path, "r", encoding="utf-8-sig") as handle:
    validation = json.load(handle)
with open(pre_migration_gate_path, "r", encoding="utf-8-sig") as handle:
    pre_migration_gate = json.load(handle)
receipt_sha256 = sha256(receipt_path)
validation_sha256 = sha256(validation_path)
pre_migration_gate_sha256 = sha256(pre_migration_gate_path)
if (
    receipt.get("status") != "ISOLATED_DOOR_VISUAL57_HARNESS_PASS"
    or receipt.get("run_id") != RUN_ID
    or receipt.get("package_count") != EXPECTED_PACKAGE_COUNT
    or receipt.get("source_bytes") != EXPECTED_SOURCE_BYTES
    or receipt.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
):
    fail("Harness receipt does not match the frozen four-package contract")
if (
    os.path.realpath(receipt.get("target_root", "")).lower() != target_root.lower()
    or os.path.realpath(receipt.get("run_root", "")).lower() != run_root.lower()
    or os.path.realpath(receipt.get("harness_content", "")).lower()
    != harness_content.lower()
):
    fail("Harness receipt roots differ from the current immutable run")
if (
    validation.get("status") != "UE57_DOOR_VISUAL_READ_ONLY_LOAD_PASS"
    or validation.get("run_id") != RUN_ID
    or validation.get("package_count") != EXPECTED_PACKAGE_COUNT
    or validation.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or validation.get("source_assets_unchanged") is not True
    or validation.get("staged_assets_unchanged") is not True
    or validation.get("legacy_blueprint_loaded") is not False
    or validation.get("asset_save_requested") is not False
    or validation.get("packages_saved") != 0
    or validation.get("receipt_sha256") != receipt_sha256
):
    fail("UE 5.7 read-only validation evidence is not PASS")
if os.path.realpath(validation.get("harness_content", "")).lower() != harness_content.lower():
    fail("UE 5.7 validation belongs to a different harness")
if (
    pre_migration_gate.get("status")
    != "DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS"
    or pre_migration_gate.get("stage") != "PRE_MIGRATION57"
    or pre_migration_gate.get("run_id") != RUN_ID
):
    fail("PRE_MIGRATION57 gate is not a PASS for the current run")
for label, node_name in (
    ("source", "source_read_only"),
    ("protected", "protected_invariants"),
):
    node = pre_migration_gate.get(node_name, {})
    nested_path = os.path.realpath(node.get("evidence", ""))
    if (
        not os.path.isfile(nested_path)
        or node.get("evidence_sha256") != sha256(nested_path)
    ):
        fail("PRE_MIGRATION57 nested {} evidence hash differs".format(label))
pre_stage_path = os.path.realpath(
    receipt.get("pre_stage_safety", {}).get("evidence", "")
)
if (
    not os.path.isfile(pre_stage_path)
    or receipt.get("pre_stage_safety", {}).get("evidence_sha256")
    != sha256(pre_stage_path)
    or validation.get("pre_stage_gate_sha256") != sha256(pre_stage_path)
):
    fail("PRE_STAGE evidence chain differs")
pre_migration_chain = pre_migration_gate.get("input_chain", {})
if (
    pre_migration_chain.get("receipt", {}).get("sha256") != receipt_sha256
    or pre_migration_chain.get("validation", {}).get("sha256")
    != validation_sha256
    or pre_migration_chain.get("pre_stage_gate", {}).get("sha256")
    != sha256(pre_stage_path)
):
    fail("PRE_MIGRATION57 chained hashes differ from the current run inputs")

staged_rows = {row["package"]: row for row in receipt.get("staged_assets", [])}
if set(staged_rows) != set(EXPECTED):
    fail("Harness receipt package set differs from the visual allowlist")

physical_package_files = set()
for root, _directories, files in os.walk(harness_content):
    for name in files:
        if name.lower().endswith(PACKAGE_EXTENSIONS):
            physical_package_files.add(os.path.realpath(os.path.join(root, name)).lower())
expected_staged_files = {
    os.path.realpath(row.get("staged", "")).lower() for row in staged_rows.values()
}
if physical_package_files != expected_staged_files:
    fail(
        "Harness physical package inventory is not the exact four-file allowlist: "
        + repr(sorted(physical_package_files))
    )

for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    row = staged_rows[package]
    source_file = os.path.realpath(row.get("source", ""))
    staged_file = os.path.realpath(row.get("staged", ""))
    target_file = os.path.realpath(row.get("target", ""))
    expected_target = os.path.realpath(os.path.join(destination, expected["relative"]))
    if target_file.lower() != expected_target.lower():
        fail("Receipt target path differs for " + package)
    for label, path in (("source", source_file), ("staged", staged_file)):
        if not os.path.isfile(path):
            fail(label + " file is absent for " + package)
        if os.path.getsize(path) != expected["length"] or sha256(path) != expected["sha256"]:
            fail(label + " file differs from the frozen baseline for " + package)
    if os.path.exists(target_file):
        fail("Refusing to overwrite a pre-existing target visual asset: " + package)

legacy_target = os.path.realpath(
    receipt.get("legacy_blueprint_reference", {}).get("target", "")
)
expected_legacy_target = os.path.realpath(
    os.path.join(destination, "Procedural", "DoorToLevel.uasset")
)
if legacy_target.lower() != expected_legacy_target.lower():
    fail("Receipt legacy target Blueprint path differs")
if os.path.exists(legacy_target):
    fail("Refusing to overwrite a pre-existing target DoorToLevel Blueprint")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    [
        "/Game/Calysto/Dungeon/Demo/LowPoly",
        "/Game/Calysto/Dungeon/Mesh/DungeonMesh",
    ],
    True,
)
registry.wait_for_completion()
options_dependencies = dependency_options()
registry_rows = {}
for package in LOAD_ORDER:
    expected = EXPECTED[package]
    package_assets = list(
        registry.get_assets_by_package_name(
            package, include_only_on_disk_assets=True
        )
    )
    if len(package_assets) != 1 or class_name(package_assets[0]) != expected["class"]:
        fail("Expected exactly one registered {} for {}".format(
            expected["class"], package
        ))
    actual_dependencies = sorted(
        {str(value) for value in registry.get_dependencies(package, options_dependencies)}
    )
    if set(actual_dependencies) != expected["dependencies"]:
        fail("Frozen dependency set differs for {}: {!r}".format(
            package, actual_dependencies
        ))
    registry_rows[package] = {
        "object_path": package + "." + package.rsplit("/", 1)[-1],
        "class": class_name(package_assets[0]),
        "dependencies": actual_dependencies,
    }

before = snapshot_package_files(destination)
before_fingerprint = snapshot_fingerprint(before)
expected_relatives = sorted(value["relative"] for value in EXPECTED.values())
if any(relative in before for relative in expected_relatives):
    fail("Target visual collision appeared before AssetTools migration")
if "procedural/doortolevel.uasset" in before:
    fail("Target DoorToLevel Blueprint appeared before UE 5.8 rebuild")

options = unreal.MigrationOptions()
options.set_editor_property("prompt", False)
options.set_editor_property("ignore_dependencies", True)
options.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
options.set_editor_property("orphan_folder", "")

dirty_immediately_before_migration = dirty_packages()
if any(dirty_immediately_before_migration.values()):
    fail(
        "AssetTools would globally save dirty content packages; refusing migration: "
        + repr(dirty_immediately_before_migration)
    )
unreal.log(
    "CODEX_DOOR_VISUAL57_MIGRATION_BEGIN: packages=" + repr(list(LOAD_ORDER))
)
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
    list(LOAD_ORDER), destination, options
)
dirty_immediately_after_migration = dirty_packages()
if any(dirty_immediately_after_migration.values()):
    fail(
        "AssetTools left dirty content packages after migration: "
        + repr(dirty_immediately_after_migration)
    )

after = snapshot_package_files(destination)
after_fingerprint = snapshot_fingerprint(after)
created = sorted(set(after) - set(before))
removed = sorted(set(before) - set(after))
modified_existing = sorted(
    name for name in set(before).intersection(after) if before[name] != after[name]
)
if created != expected_relatives:
    fail("AssetTools target delta is not exactly four visual assets: " + repr(created))
if removed:
    fail("AssetTools removed target package files: " + repr(removed))
if modified_existing:
    fail("AssetTools modified pre-existing target package files: " + repr(modified_existing))
if os.path.exists(legacy_target):
    fail("UE 5.7 AssetTools unexpectedly created the legacy DoorToLevel Blueprint")

package_rows = []
for package in sorted(EXPECTED):
    expected = EXPECTED[package]
    row = staged_rows[package]
    source_file = os.path.realpath(row["source"])
    staged_file = os.path.realpath(row["staged"])
    target_file = os.path.realpath(row["target"])
    if not os.path.isfile(target_file):
        fail("AssetTools did not create " + package)
    for extension in (".uexp", ".ubulk", ".uptnl"):
        if os.path.exists(os.path.splitext(target_file)[0] + extension):
            fail("AssetTools created an unexpected sidecar for " + package)
    if os.path.getsize(source_file) != expected["length"] or sha256(source_file) != expected["sha256"]:
        fail("Source bytes changed during migration: " + package)
    if os.path.getsize(staged_file) != expected["length"] or sha256(staged_file) != expected["sha256"]:
        fail("Staged bytes changed during migration: " + package)
    target_length = os.path.getsize(target_file)
    target_sha256 = sha256(target_file)
    package_rows.append(
        {
            "package": package,
            "class": expected["class"],
            "file": target_file,
            "length": target_length,
            "sha256": target_sha256,
            "source_length": expected["length"],
            "source_sha256": expected["sha256"],
            "bytes_match_source": (
                target_length == expected["length"]
                and target_sha256 == expected["sha256"]
            ),
        }
    )

payload = {
    "schema_version": 2,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "ASSETTOOLS_EXACT_DOOR_VISUAL_MIGRATION_PASS",
    "run_id": RUN_ID,
    "engine_version": engine_version,
    "harness_project": os.path.realpath(unreal.Paths.get_project_file_path()),
    "harness_content": harness_content,
    "run_root": run_root,
    "receipt": receipt_path,
    "receipt_sha256": receipt_sha256,
    "validation": validation_path,
    "validation_sha256": validation_sha256,
    "pre_migration_gate": pre_migration_gate_path,
    "pre_migration_gate_sha256": pre_migration_gate_sha256,
    "destination": destination,
    "package_count": EXPECTED_PACKAGE_COUNT,
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "packages": package_rows,
    "asset_registry_complete_before_migration": True,
    "asset_registry_exact_inventory": registry_rows,
    "dirty_content_packages_immediately_before_migration": dirty_immediately_before_migration["content"],
    "dirty_map_packages_immediately_before_migration": dirty_immediately_before_migration["maps"],
    "dirty_content_packages_immediately_after_migration": dirty_immediately_after_migration["content"],
    "dirty_map_packages_immediately_after_migration": dirty_immediately_after_migration["maps"],
    "global_dirty_save_gate": "PASS_EMPTY_CONTENT_AND_MAPS",
    "ignore_dependencies": True,
    "asset_conflict": "SKIP_AFTER_ABSENCE_GATE",
    "target_inventory_algorithm": "lowercase relative package path|length|sha256; mtime_ns retained as auxiliary evidence",
    "target_package_file_count_before": len(before),
    "target_package_file_count_after": len(after),
    "target_inventory_fingerprint_before": before_fingerprint,
    "target_inventory_fingerprint_after": after_fingerprint,
    "created_package_files": created,
    "removed_package_files": removed,
    "modified_existing_package_files": modified_existing,
    "target_delta_exact": True,
    "source_tree_mounted": False,
    "raw_target_asset_copy_requested": False,
    "source_assets_unchanged": True,
    "staged_assets_unchanged": True,
    "legacy_blueprint_migrated": False,
    "legacy_blueprint_target_absent": True,
    "post_migration_source_protected_gate": "PENDING_EXTERNAL_GATE",
}
os.makedirs(run_root, exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DOOR_VISUAL57_MIGRATION_PASS: " + evidence_path)
