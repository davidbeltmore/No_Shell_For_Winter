"""Load, compile, dependency-check, and resave the exact modern UI batch in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


PROJECT_ROOT = os.path.realpath(unreal.Paths.project_dir())
CONTENT_ROOT = os.path.realpath(unreal.Paths.project_content_dir())
MIGRATION_EVIDENCE = os.path.join(
    PROJECT_ROOT, "Saved", "Migration", "Phase4", "ModernUI57Migration.json"
)
EVIDENCE_PATH = os.path.join(
    PROJECT_ROOT, "Saved", "Migration", "Phase4", "ModernUI58Resave.json"
)
ROOT_COUNTS = {
    "/Game/_Game/Widgets/Chronicle": 23,
    "/Game/_Game/Widgets/InnerState": 24,
    "/Game/_Game/Widgets/Status": 17,
    "/Game/_Game/Widgets/Attributes": 27,
    "/Game/_Game/Widgets/SinfulAscensionAltar": 36,
}
EXPECTED_CLASS_COUNTS = {
    "WidgetBlueprint": 66,
    "Texture2D": 54,
    "FontFace": 7,
}


def fail(message):
    unreal.log_error("CODEX_MODERN_UI58_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def package_file(package_name):
    return os.path.realpath(
        os.path.join(
            CONTENT_ROOT,
            package_name[len("/Game/") :].replace("/", os.sep) + ".uasset",
        )
    )


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
        parts = value.split("'")
        if len(parts) >= 2:
            value = parts[1]
    return value


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if os.path.basename(unreal.Paths.get_project_file_path()).lower() != "noshellforwinter.uproject":
    fail("Commandlet is not running against NoShellForWinter.uproject")
if not os.path.isfile(MIGRATION_EVIDENCE):
    fail("UE 5.7 migration evidence is absent")
with open(MIGRATION_EVIDENCE, "r", encoding="utf-8-sig") as handle:
    migration = json.load(handle)
if migration.get("status") != "ASSETTOOLS_EXACT_MODERN_UI_MIGRATION_PASS":
    fail("UE 5.7 modern UI migration status is not PASS")
packages = tuple(sorted(row["package"] for row in migration.get("packages", [])))
if len(packages) != 127 or len(set(packages)) != 127:
    fail("UE 5.7 evidence does not enumerate 127 unique packages")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(list(ROOT_COUNTS), True)
registry.wait_for_completion()
registered = {}
registered_asset_data = {}
for package in packages:
    assets = registry.get_assets_by_package_name(
        package, include_only_on_disk_assets=True
    )
    primary_assets = [
        asset
        for asset in assets
        if class_name(asset) in EXPECTED_CLASS_COUNTS
    ]
    if len(primary_assets) != 1:
        fail("Expected exactly one registered primary asset for " + package)
    registered[package] = class_name(primary_assets[0])
    registered_asset_data[package] = primary_assets[0]
actual_root_counts = {
    root: sum(1 for package in packages if package.startswith(root + "/"))
    for root in ROOT_COUNTS
}
if actual_root_counts != ROOT_COUNTS:
    fail("UE 5.7 evidence root counts differ from the audited baseline")

actual_class_counts = {}
for value in registered.values():
    actual_class_counts[value] = actual_class_counts.get(value, 0) + 1
if actual_class_counts != EXPECTED_CLASS_COUNTS:
    fail("Unexpected target class counts: " + repr(actual_class_counts))

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
dependencies = {}
unexpected_game_dependencies = set()
for package in packages:
    direct = sorted({str(item) for item in registry.get_dependencies(package, options)})
    dependencies[package] = direct
    unexpected_game_dependencies.update(
        item for item in direct if item.startswith("/Game/") and item not in packages
    )
if unexpected_game_dependencies:
    fail(
        "Unexpected /Game dependencies after migration: "
        + repr(sorted(unexpected_game_dependencies))
    )

user_widget_class = unreal.load_class(None, "/Script/UMG.UserWidget")
if user_widget_class is None:
    fail("UMG.UserWidget did not load")

rows = []
blueprints = []
for package in packages:
    path = package_file(package)
    if not os.path.isfile(path):
        fail("Package file is absent: " + path)
    before_hash = sha256(path)
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Asset did not load: " + package)
    actual_class = asset.get_class().get_name()
    expected_class = registered[package]
    if actual_class != expected_class:
        fail(
            "Loaded class mismatch for {}: {} != {}".format(
                package, actual_class, expected_class
            )
        )

    blueprint_record = None
    if actual_class == "WidgetBlueprint":
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        status = str(asset.get_editor_property("status"))
        normalized_status = "".join(
            character for character in status.upper() if character.isalnum()
        )
        if "UPTODATE" not in normalized_status:
            fail("WidgetBlueprint compile status is {} for {}".format(status, package))
        object_name = package.rsplit("/", 1)[-1]
        generated_class = unreal.load_class(
            None, package + "." + object_name + "_C"
        )
        if generated_class is None or not unreal.MathLibrary.class_is_child_of(
            generated_class, user_widget_class
        ):
            fail("Generated class is not a UUserWidget for " + package)
        parent_path = native_parent_path(registered_asset_data[package])
        if not parent_path.startswith("/Script/EFProjectSystemsGameplay."):
            fail("Unexpected WidgetBlueprint parent {} for {}".format(parent_path, package))
        parent_class = unreal.load_class(None, parent_path)
        if parent_class is None or not unreal.MathLibrary.class_is_child_of(
            generated_class, parent_class
        ):
            fail("Generated class does not derive from {} for {}".format(parent_path, package))
        blueprint_record = {
            "package": package,
            "status": status,
            "generated_class": generated_class.get_path_name(),
            "parent_class": parent_path,
        }
        blueprints.append(blueprint_record)

    if not unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        fail("UE 5.8 save failed: " + package)
    rows.append(
        {
            "package": package,
            "class": actual_class,
            "file": path,
            "length": os.path.getsize(path),
            "sha256_before_resave": before_hash,
            "sha256_after_resave": sha256(path),
        }
    )

if len(blueprints) != 66:
    fail("Expected 66 compiled WidgetBlueprints but found {}".format(len(blueprints)))

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_MODERN_UI_LOAD_COMPILE_RESAVE_PASS",
    "engine_version": engine_version,
    "package_count": len(rows),
    "root_counts": actual_root_counts,
    "class_counts": actual_class_counts,
    "packages": rows,
    "compiled_widget_blueprints": blueprints,
    "compiled_widget_blueprint_count": len(blueprints),
    "dependencies": dependencies,
    "unexpected_game_dependencies": [],
    "redirectors": [],
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_MODERN_UI58_PASS: " + EVIDENCE_PATH)
