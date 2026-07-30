"""Load, compile, and resave the migrated TattooShop BP_TSChar closure in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


MIGRATION_VALUE = os.environ.get("CODEX_TATTOOSHOP_MIGRATION_EVIDENCE", "").strip()
OUTPUT_VALUE = os.environ.get("CODEX_TATTOOSHOP_58_EVIDENCE", "").strip()
SAVE_VALUE = os.environ.get("CODEX_TATTOOSHOP_SAVE_58", "1").strip()
EXPECTED_COUNT = 50
REQUIRED_PACKAGES = (
    "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar",
    "/Game/TattooShop/Blueprints/Widget/WBP_TattooShop",
    "/Game/TattooShop/Blueprints/Widget/WBP_AssetPreviewer",
    "/Game/TattooShop/Blueprints/Widget/WBP_TattooCard",
    "/Game/TattooShop/Texture/T_Heart",
    "/Game/TattooShop/Texture/T_Bunny",
    "/Game/_Game/AutomaticTattoo/DT_AutomaticTattoos",
    "/Game/_Game/Input/IMC_TattooShop_LADS",
    "/Game/_Game/Input/IMC_TattooShopOpen_LADS",
)


def fail(message):
    unreal.log_error("CODEX_TATTOOSHOP58_VALIDATE_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def is_under(path, root):
    try:
        return (
            os.path.commonpath([os.path.realpath(path), os.path.realpath(root)]).lower()
            == os.path.realpath(root).lower()
        )
    except ValueError:
        return False


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        try:
            options.set_editor_property(name, True)
        except Exception:
            pass
    try:
        options.set_editor_property("include_searchable_names", False)
    except Exception:
        pass
    return options


def package_filename(content_root, package):
    return os.path.realpath(
        os.path.join(content_root, package[len("/Game/") :].replace("/", os.sep) + ".uasset")
    )


def class_path(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def blueprint_status(blueprint):
    try:
        return str(blueprint.get_editor_property("status"))
    except Exception as exc:
        return "unavailable: {!r}".format(exc)


def blueprint_generated_class(blueprint):
    try:
        value = blueprint.generated_class
        if callable(value):
            value = value()
        if value:
            return value
    except Exception:
        pass
    try:
        return blueprint.get_editor_property("generated_class")
    except Exception:
        return None


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if not MIGRATION_VALUE or not OUTPUT_VALUE:
    fail("Migration and output evidence paths are required")

project_file = os.path.realpath(unreal.Paths.get_project_file_path())
project_root = os.path.realpath(unreal.Paths.project_dir())
content_root = os.path.realpath(unreal.Paths.project_content_dir())
migration_path = os.path.realpath(MIGRATION_VALUE)
output_path = os.path.realpath(OUTPUT_VALUE)
if os.path.basename(project_file).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if not is_under(migration_path, os.path.join(project_root, "Saved", "Migration")):
    fail("Migration evidence escapes Saved/Migration")
if not is_under(output_path, os.path.join(project_root, "Saved", "Migration")):
    fail("Output evidence escapes Saved/Migration")

with open(migration_path, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if migration.get("status") != "ASSETTOOLS_EXACT_TATTOOSHOP_BP_TSCHAR57_MIGRATION_PASS":
    fail("UE 5.7 TattooShop migration evidence is not PASS")
if int(migration.get("package_count", -1)) != EXPECTED_COUNT:
    fail("Migration package count differs")

packages = [str(row["package"]) for row in migration.get("packages", [])]
if len(packages) != EXPECTED_COUNT or len(set(packages)) != EXPECTED_COUNT:
    fail("Package list count/uniqueness differs")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/TattooShop", "/Game/_Game"], True)
registry.wait_for_completion()

missing_required = []
loaded_rows = []
for package in sorted(set(packages).union(REQUIRED_PACKAGES)):
    assets = list(registry.get_assets_by_package_name(package, include_only_on_disk_assets=True))
    asset = unreal.EditorAssetLibrary.load_asset(package)
    row = {
        "package": package,
        "registry_asset_count": len(assets),
        "asset_classes": sorted({class_path(asset_data) for asset_data in assets}),
        "loaded": asset is not None,
    }
    if asset is None:
        missing_required.append(package)
    loaded_rows.append(row)

if missing_required:
    fail("Required TattooShop package did not load: " + ", ".join(sorted(missing_required)))

blueprint_rows = []
for package in packages:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if not isinstance(asset, unreal.Blueprint):
        continue
    before = blueprint_status(asset)
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    after = blueprint_status(asset)
    if "BS_Error" in after or "Error" in after:
        fail("Blueprint compile status is error for {}: {}".format(package, after))
    blueprint_rows.append(
        {
            "package": package,
            "status_before": before,
            "status_after": after,
            "generated_class": blueprint_generated_class(asset).get_path_name()
            if blueprint_generated_class(asset)
            else "",
        }
    )

bp_tschar_class = unreal.load_class(
    None, "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar.BP_TSChar_C"
)
player_class = unreal.load_class(None, "/Game/FullSample/Player.Player_C")
if bp_tschar_class is None:
    fail("BP_TSChar generated class did not load in UE 5.8")
if player_class is None:
    fail("Target /Game/FullSample/Player.Player_C did not load")
if not unreal.MathLibrary.class_is_child_of(bp_tschar_class, player_class):
    fail("BP_TSChar is not a child of the target /Game/FullSample/Player")

options = dependency_options()
missing_game_dependencies = []
dependency_rows = []
for package in packages:
    dependencies = sorted({str(value) for value in registry.get_dependencies(package, options)})
    missing = []
    for dependency in dependencies:
        if not dependency.startswith("/Game/"):
            continue
        if not list(registry.get_assets_by_package_name(dependency, include_only_on_disk_assets=True)):
            missing.append(dependency)
    if missing:
        missing_game_dependencies.extend(missing)
    dependency_rows.append({"package": package, "missing_game_dependencies": missing})

saved_packages = []
if SAVE_VALUE not in ("0", "false", "False"):
    for package in packages:
        if unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
            saved_packages.append(package)

disk_rows = []
for package in packages:
    filename = package_filename(content_root, package)
    if not os.path.isfile(filename):
        fail("Migrated package file is absent on disk: " + package)
    disk_rows.append({"package": package, "file": filename, "length": os.path.getsize(filename), "sha256": sha256(filename)})

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_TATTOOSHOP_BP_TSCHAR_COMPILE_PASS"
    if not missing_game_dependencies
    else "UE58_TATTOOSHOP_BP_TSCHAR_DEPENDENCY_GAP",
    "engine_version": engine_version,
    "project": project_file,
    "migration_evidence": migration_path,
    "migration_evidence_sha256": sha256(migration_path),
    "package_count": len(packages),
    "loaded_assets": loaded_rows,
    "compiled_blueprints": blueprint_rows,
    "bp_tschar_generated_class": bp_tschar_class.get_path_name(),
    "bp_tschar_parent_contract": "/Game/FullSample/Player.Player_C",
    "bp_tschar_is_child_of_target_player": True,
    "dependency_rows": dependency_rows,
    "unresolved_game_dependencies": sorted(set(missing_game_dependencies)),
    "saved_packages": saved_packages,
    "disk_packages_after_save": disk_rows,
    "legacy_save_slots_migrated": False,
}
os.makedirs(os.path.dirname(output_path), exist_ok=True)
temporary = output_path + ".tmp"
with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
os.replace(temporary, output_path)
if missing_game_dependencies:
    fail("TattooShop has unresolved /Game dependencies; evidence: " + output_path)
unreal.log("CODEX_TATTOOSHOP58_VALIDATE_PASS: " + output_path)
