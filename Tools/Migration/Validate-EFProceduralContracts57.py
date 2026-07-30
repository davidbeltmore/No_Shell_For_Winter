"""Read-only UE 5.7 validation for the exact procedural-contract harness."""

import datetime
import hashlib
import json
import os

import unreal


EXPECTED_PACKAGE_COUNT = 20
EXPECTED_SOURCE_BYTES = 223283
EXPECTED_SOURCE_FINGERPRINT = (
    "30A8ACBD998EBD41242A3BD850C8CBB3E7E6A19D63D72E632D0BB897917FA006"
)
EXPECTED_CLASS_COUNTS = {
    "Blueprint": 6,
    "UserDefinedStruct": 12,
    "UserDefinedEnum": 2,
}
START_POINT = "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint"
DIRECT_CONTRACT_ROOTS = (
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
)
EXPECTED_PACKAGES = (
    START_POINT,
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
)
EXPECTED_CLASS = {
    package: (
        "Blueprint"
        if package == START_POINT or package.rsplit("/", 1)[-1].startswith("PDA_")
        else "UserDefinedEnum"
        if "/Data/Enumerator/" in package
        else "UserDefinedStruct"
    )
    for package in EXPECTED_PACKAGES
}
EXPECTED_BLUEPRINT_PARENT = {
    package: (
        "/Script/Engine.Actor"
        if package == START_POINT
        else "/Script/Engine.PrimaryDataAsset"
    )
    for package in EXPECTED_PACKAGES
    if EXPECTED_CLASS[package] == "Blueprint"
}
LOAD_ORDER = (
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType",
    "/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    START_POINT,
)

RECEIPT_PATH = os.environ.get(
    "CODEX_PROCEDURAL_CONTRACTS_RECEIPT", ""
).strip()
OUTPUT_PATH = os.environ.get(
    "CODEX_PROCEDURAL_CONTRACTS_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_TARGET_ROOT = os.environ.get("CODEX_EXPECTED_TARGET_ROOT", "").strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()


def fail(message):
    unreal.log_error("CODEX_PROCEDURAL_CONTRACTS57_VALIDATION_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def fingerprint(rows):
    values = [rows[package]["sha256"].upper() for package in sorted(rows)]
    return hashlib.sha256("\n".join(values).encode("utf-8")).hexdigest().upper()


def class_name(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def native_parent_path(asset_data):
    try:
        value = str(asset_data.get_tag_value("NativeParentClass") or "")
    except Exception:
        value = ""
    if "'" in value:
        pieces = value.split("'")
        if len(pieces) >= 2:
            value = pieces[1]
    return value


def is_under(path, root):
    normalized_path = os.path.realpath(path).lower()
    normalized_root = os.path.realpath(root).rstrip(os.sep).lower() + os.sep
    return normalized_path.startswith(normalized_root)


if set(LOAD_ORDER) != set(EXPECTED_PACKAGES) or len(LOAD_ORDER) != 20:
    raise RuntimeError("Internal load order differs from the exact allowlist")

engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all(
    (
        RECEIPT_PATH,
        OUTPUT_PATH,
        EXPECTED_TARGET_ROOT,
        EXPECTED_HARNESS_CONTENT,
    )
):
    fail("All procedural-contract validation environment variables are required")

target_root = os.path.realpath(EXPECTED_TARGET_ROOT)
harness_content = os.path.realpath(unreal.Paths.project_content_dir())
if harness_content.lower() != os.path.realpath(EXPECTED_HARNESS_CONTENT).lower():
    fail("Commandlet is not running inside the audited isolated harness")
if not is_under(harness_content, os.path.join(target_root, "Saved", "Migration", "Phase4")):
    fail("Harness Content escapes target Saved/Migration/Phase4")
if is_under(harness_content, os.path.join(target_root, "Content")):
    fail("Harness Content unexpectedly lives below target Content")

receipt_path = os.path.realpath(RECEIPT_PATH)
if not is_under(receipt_path, os.path.join(target_root, "Saved", "Migration", "Phase4")):
    fail("Harness receipt escapes target Saved/Migration/Phase4")
with open(receipt_path, "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if (
    receipt.get("status") != "ISOLATED_PROCEDURAL_CONTRACTS_HARNESS_PASS"
    or receipt.get("expected_package_count") != EXPECTED_PACKAGE_COUNT
    or receipt.get("expected_source_bytes") != EXPECTED_SOURCE_BYTES
    or receipt.get("expected_source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
):
    fail("Harness receipt does not match the frozen 20-package baseline")
if harness_content.lower() != os.path.realpath(receipt.get("harness_content", "")).lower():
    fail("Harness Content differs from its receipt")

receipt_rows = {
    row["package"]: row for row in receipt.get("staged_assets", [])
}
if set(receipt_rows) != set(EXPECTED_PACKAGES) or len(receipt_rows) != 20:
    fail("Harness receipt package set differs from the exact allowlist")
staged_rows = {}
for package in EXPECTED_PACKAGES:
    row = receipt_rows[package]
    path = os.path.realpath(row["staged"])
    if not is_under(path, harness_content) or not os.path.isfile(path):
        fail("Staged file is absent or escapes harness Content: " + package)
    current_length = os.path.getsize(path)
    current_hash = sha256(path)
    if current_length != row["length"] or current_hash != row["sha256"]:
        fail("Staged package differs from its frozen receipt: " + package)
    staged_rows[package] = {
        "file": path,
        "length": current_length,
        "sha256": current_hash,
    }
if sum(row["length"] for row in staged_rows.values()) != EXPECTED_SOURCE_BYTES:
    fail("Staged byte total differs from the frozen baseline")
if fingerprint(staged_rows) != EXPECTED_SOURCE_FINGERPRINT:
    fail("Staged hash fingerprint differs from the frozen baseline")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    [
        "/Game/Calysto/Dungeon/Blueprint/Utility",
        "/Game/Calysto/Dungeon/Data",
        "/Game/Calysto/Shared/Data",
    ],
    True,
)
registry.wait_for_completion()

registered = {}
registered_data = {}
class_counts = {}
for package in EXPECTED_PACKAGES:
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    primary = [item for item in assets if class_name(item) == EXPECTED_CLASS[package]]
    if len(primary) != 1:
        fail("Expected one registered primary asset for " + package)
    registered[package] = class_name(primary[0])
    registered_data[package] = primary[0]
    class_counts[registered[package]] = class_counts.get(registered[package], 0) + 1
if class_counts != EXPECTED_CLASS_COUNTS:
    fail("UE 5.7 class counts differ: " + repr(class_counts))

dependency_options = unreal.AssetRegistryDependencyOptions()
for property_name in (
    "include_hard_package_references",
    "include_soft_package_references",
    "include_hard_management_references",
    "include_soft_management_references",
):
    try:
        dependency_options.set_editor_property(property_name, True)
    except Exception:
        pass
try:
    dependency_options.set_editor_property("include_searchable_names", False)
except Exception:
    pass

dependencies = {}
unexpected_game_dependencies = set()
script_dependencies = set()
for package in EXPECTED_PACKAGES:
    direct = sorted(
        {
            str(value)
            for value in registry.get_dependencies(package, dependency_options)
        }
    )
    dependencies[package] = direct
    unexpected_game_dependencies.update(
        value
        for value in direct
        if value.startswith("/Game/") and value not in EXPECTED_PACKAGES
    )
    script_dependencies.update(
        value for value in direct if value.startswith("/Script/")
    )
if unexpected_game_dependencies:
    fail(
        "Unexpected /Game dependencies: "
        + repr(sorted(unexpected_game_dependencies))
    )

contract_packages = set(EXPECTED_PACKAGES) - {START_POINT}
closure = set(DIRECT_CONTRACT_ROOTS)
queue = list(DIRECT_CONTRACT_ROOTS)
while queue:
    package = queue.pop(0)
    for dependency in dependencies[package]:
        if not dependency.startswith("/Game/"):
            continue
        if dependency not in contract_packages:
            fail("Contract closure escaped the exact 19-package set: " + dependency)
        if dependency not in closure:
            closure.add(dependency)
            queue.append(dependency)
if closure != contract_packages:
    fail(
        "Nine contract roots do not close to exactly nineteen packages; missing="
        + repr(sorted(contract_packages - closure))
    )

compiled_blueprints = []
parent_counts = {}
loaded_rows = []
for package in LOAD_ORDER:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Asset did not load: " + package)
    actual_class = asset.get_class().get_name()
    if actual_class != EXPECTED_CLASS[package]:
        fail(
            "Loaded class differs for {}: {} != {}".format(
                package, actual_class, EXPECTED_CLASS[package]
            )
        )
    row = {"package": package, "class": actual_class}
    if actual_class == "Blueprint":
        parent_path = native_parent_path(registered_data[package])
        expected_parent_path = EXPECTED_BLUEPRINT_PARENT[package]
        if parent_path != expected_parent_path:
            fail(
                "Native parent differs for {}: {} != {}".format(
                    package, parent_path, expected_parent_path
                )
            )
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        object_name = package.rsplit("/", 1)[-1]
        generated_class = unreal.load_class(
            None, package + "." + object_name + "_C"
        )
        parent_class = unreal.load_class(None, expected_parent_path)
        if generated_class is None or parent_class is None:
            fail("Generated or expected parent class did not load for " + package)
        if not unreal.MathLibrary.class_is_child_of(generated_class, parent_class):
            fail("Generated class does not derive from expected parent for " + package)
        parent_counts[parent_path] = parent_counts.get(parent_path, 0) + 1
        compiled_blueprints.append(
            {
                "package": package,
                "generated_class": generated_class.get_path_name(),
                "native_parent": parent_path,
                "compile_requested": True,
            }
        )
        row["native_parent"] = parent_path
    loaded_rows.append(row)

if len(compiled_blueprints) != 6:
    fail("Expected six compiled Blueprints")
if parent_counts != {
    "/Script/Engine.PrimaryDataAsset": 5,
    "/Script/Engine.Actor": 1,
}:
    fail("Blueprint parent counts differ: " + repr(parent_counts))

output_path = os.path.realpath(OUTPUT_PATH)
if not is_under(output_path, os.path.join(target_root, "Saved", "Migration")):
    fail("Validation evidence escapes target Saved/Migration")
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_PROCEDURAL_CONTRACTS_READ_ONLY_LOAD_COMPILE_PASS",
    "engine_version": engine_version,
    "harness_project": unreal.Paths.get_project_file_path(),
    "package_count": len(loaded_rows),
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "class_counts": class_counts,
    "packages": loaded_rows,
    "compiled_blueprints": compiled_blueprints,
    "compiled_blueprint_count": len(compiled_blueprints),
    "blueprint_parent_counts": parent_counts,
    "direct_contract_root_count": len(DIRECT_CONTRACT_ROOTS),
    "contract_closure_count": len(closure),
    "dependencies": dependencies,
    "script_dependencies": sorted(script_dependencies),
    "unexpected_game_dependencies": [],
    "packages_loaded": len(loaded_rows),
    "asset_save_requested": False,
    "packages_saved": 0,
    "map_load_requested": False,
    "source_tree_mounted": False,
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_PROCEDURAL_CONTRACTS57_VALIDATION_PASS: " + output_path)
