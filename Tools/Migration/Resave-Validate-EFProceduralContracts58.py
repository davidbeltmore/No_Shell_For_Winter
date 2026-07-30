"""Load, compile, dependency-check, and resave the exact batch in UE 5.8."""

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

PROJECT_ROOT = os.path.realpath(unreal.Paths.project_dir())
CONTENT_ROOT = os.path.realpath(unreal.Paths.project_content_dir())
MIGRATION_EVIDENCE = os.path.join(
    PROJECT_ROOT,
    "Saved",
    "Migration",
    "Phase4",
    "EFProceduralContracts57Migration.json",
)
EVIDENCE_PATH = os.path.join(
    PROJECT_ROOT,
    "Saved",
    "Migration",
    "Phase4",
    "EFProceduralContracts58Resave.json",
)


def fail(message):
    unreal.log_error("CODEX_PROCEDURAL_CONTRACTS58_RESAVE_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def fingerprint(rows, hash_key):
    values = [rows[package][hash_key].upper() for package in sorted(rows)]
    return hashlib.sha256("\n".join(values).encode("utf-8")).hexdigest().upper()


def package_file(package_name):
    relative = package_name[len("/Game/") :].replace("/", os.sep)
    return os.path.realpath(os.path.join(CONTENT_ROOT, relative + ".uasset"))


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


if set(LOAD_ORDER) != set(EXPECTED_PACKAGES) or len(LOAD_ORDER) != 20:
    raise RuntimeError("Internal load order differs from the exact allowlist")

engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if os.path.basename(unreal.Paths.get_project_file_path()).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if CONTENT_ROOT.lower() != os.path.realpath(os.path.join(PROJECT_ROOT, "Content")).lower():
    fail("Project Content invariant failed")
if not os.path.isfile(MIGRATION_EVIDENCE):
    fail("UE 5.7 migration evidence is absent")

with open(MIGRATION_EVIDENCE, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if (
    migration.get("status")
    != "ASSETTOOLS_EXACT_PROCEDURAL_CONTRACTS_MIGRATION_PASS"
    or migration.get("package_count") != EXPECTED_PACKAGE_COUNT
    or migration.get("created_package_count") != EXPECTED_PACKAGE_COUNT
    or migration.get("source_bytes") != EXPECTED_SOURCE_BYTES
    or migration.get("source_fingerprint") != EXPECTED_SOURCE_FINGERPRINT
    or migration.get("class_counts") != EXPECTED_CLASS_COUNTS
    or migration.get("ignore_dependencies") is not True
    or migration.get("unexpected_game_dependencies")
):
    fail("UE 5.7 migration evidence does not match the frozen baseline")
migration_rows = {row["package"]: row for row in migration.get("packages", [])}
if set(migration_rows) != set(EXPECTED_PACKAGES) or len(migration_rows) != 20:
    fail("UE 5.7 migration package set differs from the exact allowlist")

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

registered_data = {}
class_counts = {}
redirectors = []
for package in EXPECTED_PACKAGES:
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    for item in assets:
        if class_name(item) == "ObjectRedirector":
            redirectors.append(package)
    primary = [item for item in assets if class_name(item) == EXPECTED_CLASS[package]]
    if len(primary) != 1:
        fail("Expected one registered primary target asset for " + package)
    registered_data[package] = primary[0]
    value = class_name(primary[0])
    class_counts[value] = class_counts.get(value, 0) + 1
if class_counts != EXPECTED_CLASS_COUNTS:
    fail("Unexpected UE 5.8 class counts: " + repr(class_counts))
if redirectors:
    fail("Redirectors found in the exact migrated package set: " + repr(redirectors))

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
if unexpected_game_dependencies:
    fail(
        "Unexpected UE 5.8 /Game dependencies: "
        + repr(sorted(unexpected_game_dependencies))
    )

for package in EXPECTED_PACKAGES:
    path = package_file(package)
    prior = migration_rows[package]
    if not os.path.isfile(path):
        fail("Migrated target package is absent: " + package)
    if os.path.realpath(prior["file"]).lower() != path.lower():
        fail("Migration evidence target path differs for " + package)
    if os.path.getsize(path) != prior["length"] or sha256(path) != prior["sha256"]:
        fail("Target bytes changed before UE 5.8 resave: " + package)

results = {}
compiled_blueprints = []
parent_counts = {}
for package in LOAD_ORDER:
    path = package_file(package)
    before_hash = sha256(path)
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Asset did not load in UE 5.8: " + package)
    actual_class = asset.get_class().get_name()
    if actual_class != EXPECTED_CLASS[package]:
        fail(
            "Loaded UE 5.8 class differs for {}: {} != {}".format(
                package, actual_class, EXPECTED_CLASS[package]
            )
        )

    blueprint_row = None
    if actual_class == "Blueprint":
        expected_parent_path = EXPECTED_BLUEPRINT_PARENT[package]
        parent_path = native_parent_path(registered_data[package])
        if parent_path != expected_parent_path:
            fail(
                "Native parent differs for {}: {} != {}".format(
                    package, parent_path, expected_parent_path
                )
            )
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        status = str(asset.get_editor_property("status"))
        normalized_status = "".join(
            character for character in status.upper() if character.isalnum()
        )
        if "UPTODATE" not in normalized_status:
            fail("Blueprint status is {} for {}".format(status, package))
        object_name = package.rsplit("/", 1)[-1]
        generated_class = unreal.load_class(
            None, package + "." + object_name + "_C"
        )
        expected_parent = unreal.load_class(None, expected_parent_path)
        if generated_class is None or expected_parent is None:
            fail("Generated or expected parent class did not load for " + package)
        if not unreal.MathLibrary.class_is_child_of(
            generated_class, expected_parent
        ):
            fail("Generated class does not derive from expected parent for " + package)
        parent_counts[parent_path] = parent_counts.get(parent_path, 0) + 1
        blueprint_row = {
            "package": package,
            "status": status,
            "generated_class": generated_class.get_path_name(),
            "native_parent": parent_path,
        }
        compiled_blueprints.append(blueprint_row)

    if not unreal.EditorAssetLibrary.save_asset(
        package, only_if_is_dirty=False
    ):
        fail("UE 5.8 save failed: " + package)
    results[package] = {
        "package": package,
        "class": actual_class,
        "file": path,
        "length": os.path.getsize(path),
        "sha256_before_resave": before_hash,
        "sha256_after_resave": sha256(path),
        "blueprint": blueprint_row,
    }

if len(compiled_blueprints) != 6:
    fail("Expected six compiled Blueprints")
if parent_counts != {
    "/Script/Engine.PrimaryDataAsset": 5,
    "/Script/Engine.Actor": 1,
}:
    fail("UE 5.8 Blueprint parent counts differ: " + repr(parent_counts))

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_PROCEDURAL_CONTRACTS_LOAD_COMPILE_RESAVE_PASS",
    "engine_version": engine_version,
    "project": unreal.Paths.get_project_file_path(),
    "package_count": len(results),
    "source_bytes": EXPECTED_SOURCE_BYTES,
    "source_fingerprint": EXPECTED_SOURCE_FINGERPRINT,
    "target_fingerprint_after_resave": fingerprint(
        results, "sha256_after_resave"
    ),
    "class_counts": class_counts,
    "packages": [results[package] for package in sorted(results)],
    "compiled_blueprints": compiled_blueprints,
    "compiled_blueprint_count": len(compiled_blueprints),
    "blueprint_parent_counts": parent_counts,
    "dependencies": dependencies,
    "unexpected_game_dependencies": [],
    "redirectors": [],
    "map_load_requested": False,
    "pie_requested": False,
    "excluded_runtime_assets_touched": False,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_PROCEDURAL_CONTRACTS58_RESAVE_PASS: " + EVIDENCE_PATH)
