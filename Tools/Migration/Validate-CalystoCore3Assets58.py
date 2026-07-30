"""Fresh-process, no-save UE 5.8 validation for three Calysto assets.

Required environment:
  CODEX_CALYSTO_CORE3_REMIGRATE_EVIDENCE
  CODEX_CALYSTO_CORE3_VALIDATION_EVIDENCE
"""

import datetime
import hashlib
import json
import os

import unreal


REMIGRATE_VALUE = os.environ.get(
    "CODEX_CALYSTO_CORE3_REMIGRATE_EVIDENCE", ""
).strip()
EVIDENCE_VALUE = os.environ.get(
    "CODEX_CALYSTO_CORE3_VALIDATION_EVIDENCE", ""
).strip()
PACKAGES = (
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController",
    "/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto",
)
EXPECTED_PARENTS = {
    PACKAGES[0]: "/Script/Engine.Actor",
    PACKAGES[1]: "/Script/Engine.PlayerController",
    PACKAGES[2]: "/Script/Engine.PrimaryDataAsset",
}
SUPPORT_LOAD_ORDER = (
    "/Game/Calysto/Shared/Data/Structure/Enum_SpawnType",
    "/Game/Calysto/Shared/Data/Structure/ST_CalystoScatterBlueprint",
    "/Game/Calysto/Shared/Data/Structure/ST_CalystoScatter",
    "/Game/Calysto/Shared/Data/Structure/PDA_CalystoScatter",
    "/Game/Calysto/Shared/Data/Structure/Enum_ScatterMode",
    "/Game/Calysto/Shared/Data/Structure/ST_SmartScatter",
    "/Game/Calysto/Shared/Data/Structure/ST_PlaceOfInterest",
    "/Game/Calysto/Shared/Data/Structure/ST_Vegetation",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor",
    "/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor",
    "/Game/Calysto/Shared/Data/Structure/ST_Spawner",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon",
    "/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme",
    "/Game/Calysto/Shared/Data/Structure/PDA_Spawner",
    "/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece",
    "/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize",
    "/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/PCG/PCG_DungeonEditor",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
    "/Game/Calysto/Shared/Input/IMC_Default",
)
KNOWN_MISSING_CONTROLLER_DEPENDENCY = (
    "/Game/Calysto/World/Blueprint/Legacy/BP_Biome3_0"
)


def fail(message):
    unreal.log_error("CODEX_CALYSTO_CORE3_VALIDATE58_FAIL: " + message)
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


def package_file(content_root, package):
    return os.path.realpath(
        os.path.join(
            content_root,
            package[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


def snapshot(path):
    if not os.path.isfile(path):
        fail("Required target package is absent: " + path)
    stat = os.stat(path)
    return {
        "file": os.path.realpath(path),
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


def normalized_status(blueprint):
    raw = str(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in raw.upper() if character.isalnum())
    return raw, normalized


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not REMIGRATE_VALUE or not EVIDENCE_VALUE:
    fail("Both documented Calysto Core3 validation environment variables are required")
if len(PACKAGES) != 3 or len(set(PACKAGES)) != 3:
    fail("Guarded validation cohort is not exactly three unique packages")
if any(package.lower().startswith("/game/exportedanimations") for package in PACKAGES):
    fail("ExportedAnimations entered the Core3 validation cohort")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
saved_root = os.path.realpath(os.path.join(project_root, "Saved", "Migration"))
remigrate_path = os.path.realpath(REMIGRATE_VALUE)
evidence_path = os.path.realpath(EVIDENCE_VALUE)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if not is_under(remigrate_path, saved_root) or not os.path.isfile(remigrate_path):
    fail("Core3 remigration evidence is absent or escapes Saved/Migration")
if not is_under(evidence_path, saved_root) or evidence_path.lower() == remigrate_path.lower():
    fail("Core3 validation evidence path is invalid")

with open(remigrate_path, "r", encoding="utf-8-sig") as handle:
    remigrate = json.load(handle)
if remigrate.get("status") != "ASSETTOOLS_EXACT_CALYSTO_CORE3_OVERWRITE57_PASS":
    fail("Core3 remigration evidence status is not PASS")
if tuple(str(value) for value in remigrate.get("packages", [])) != PACKAGES:
    fail("Core3 remigration evidence package cohort differs")
if os.path.realpath(str(remigrate.get("target_root", ""))).lower() != project_root.lower():
    fail("Core3 remigration evidence belongs to a different target")
if (
    remigrate.get("package_count") != 3
    or remigrate.get("target_delta_exact") is not True
    or remigrate.get("harness_unchanged") is not True
    or remigrate.get("ignore_dependencies") is not True
    or remigrate.get("asset_conflict") != "OVERWRITE"
    or remigrate.get("exported_animations_excluded") is not True
):
    fail("Core3 remigration invariants are incomplete")

recorded_target = remigrate.get("target_after", {})
if set(recorded_target) != set(PACKAGES):
    fail("Core3 target_after evidence is not the exact three-package cohort")
disk_before = {}
target_after_checks = []
for package in PACKAGES:
    current = snapshot(package_file(content_root, package))
    recorded = recorded_target.get(package, {})
    bytes_match = (
        current["length"] == int(recorded.get("length", -1))
        and current["sha256"] == str(recorded.get("sha256", "")).upper()
    )
    target_after_checks.append({"package": package, "bytes_match": bytes_match})
    if not bytes_match:
        fail("Current bytes differ from Core3 remigration target_after: " + package)
    disk_before[package] = current

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Calysto"], True)
registry.wait_for_completion()
support_rows = []
support_failures = []
for package in SUPPORT_LOAD_ORDER:
    try:
        asset = unreal.EditorAssetLibrary.load_asset(package)
    except Exception as exc:
        asset = None
        error = repr(exc)
    else:
        error = ""
    support_rows.append(
        {
            "package": package,
            "loaded": asset is not None,
            "asset_class": asset.get_class().get_name() if asset is not None else "",
            "error": error,
        }
    )
    if asset is None:
        support_failures.append(
            {"package": package, "reason": "SUPPORT_ASSET_LOAD_FAILED", "error": error}
        )

missing_dependency_file = package_file(
    content_root, KNOWN_MISSING_CONTROLLER_DEPENDENCY
)
controller_dependency_present = os.path.isfile(missing_dependency_file)
validation_rows = []
failures = list(support_failures)
for package in PACKAGES:
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(package)
        if blueprint is None or blueprint.get_class().get_name() != "Blueprint":
            raise RuntimeError("Asset is not a Blueprint")
        parent = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(blueprint)
        parent_path = object_path(parent)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        status, normalized = normalized_status(blueprint)
        asset_name = package.rsplit("/", 1)[-1]
        generated = unreal.load_class(None, package + "." + asset_name + "_C")
        expected_parent = unreal.load_class(None, EXPECTED_PARENTS[package])
        ancestry = (
            generated is not None
            and expected_parent is not None
            and unreal.MathLibrary.class_is_child_of(generated, expected_parent)
        )
    except Exception as exc:
        failures.append(
            {"package": package, "reason": "LOAD_OR_COMPILE_EXCEPTION", "error": repr(exc)}
        )
        continue
    row = {
        "package": package,
        "compile_status": status,
        "direct_parent": parent_path,
        "expected_parent": EXPECTED_PARENTS[package],
        "generated_class": object_path(generated),
        "generated_class_has_expected_ancestry": bool(ancestry),
    }
    validation_rows.append(row)
    if parent_path != EXPECTED_PARENTS[package]:
        failures.append(
            {
                "package": package,
                "reason": "DIRECT_PARENT_MISMATCH",
                "actual": parent_path,
                "expected": EXPECTED_PARENTS[package],
            }
        )
    if "UPTODATE" not in normalized:
        failures.append(
            {"package": package, "reason": "BLUEPRINT_NOT_UP_TO_DATE", "status": status}
        )
    if not ancestry:
        failures.append(
            {"package": package, "reason": "GENERATED_CLASS_ANCESTRY_MISMATCH"}
        )

disk_after = {package: snapshot(row["file"]) for package, row in disk_before.items()}
changed_hashes = [
    package
    for package in PACKAGES
    if disk_after[package]["length"] != disk_before[package]["length"]
    or disk_after[package]["sha256"] != disk_before[package]["sha256"]
]
for package in changed_hashes:
    failures.append({"package": package, "reason": "PACKAGE_BYTES_CHANGED_DURING_VALIDATION"})

passed = len(validation_rows) == 3 and not failures and not changed_hashes
payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": (
        "UE58_CALYSTO_CORE3_FRESH_READ_ONLY_PASS"
        if passed
        else "UE58_CALYSTO_CORE3_FRESH_READ_ONLY_FAIL"
    ),
    "engine_version": engine_version,
    "project": project_file,
    "remigration_evidence": remigrate_path,
    "remigration_evidence_sha256": sha256(remigrate_path),
    "package_count": 3,
    "packages": list(PACKAGES),
    "support_loads": support_rows,
    "known_controller_dependency": KNOWN_MISSING_CONTROLLER_DEPENDENCY,
    "known_controller_dependency_present": controller_dependency_present,
    "target_after_checks": target_after_checks,
    "loaded_compiled_validated_count": len(validation_rows),
    "validations": validation_rows,
    "failures": failures,
    "package_files_before": disk_before,
    "package_files_after": disk_after,
    "changed_package_hashes": changed_hashes,
    "package_hashes_unchanged": not changed_hashes,
    "asset_save_operations": [],
    "disk_asset_write_operations": [],
    "exported_animations_excluded": True,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
temporary = evidence_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, evidence_path)
if not passed:
    fail(
        "Core3 fresh validation failed: validated={}/3, failures={}, changed={}".format(
            len(validation_rows), len(failures), len(changed_hashes)
        )
    )
unreal.log("CODEX_CALYSTO_CORE3_VALIDATE58_PASS: " + evidence_path)
