"""Read-only UE 5.7 load/compile validation for the exact modern UI harness."""

import datetime
import json
import os

import unreal


RECEIPT_PATH = os.environ.get("CODEX_MODERN_UI_RECEIPT", "").strip()
EVIDENCE_PATH = os.environ.get(
    "CODEX_MODERN_UI_VALIDATION_EVIDENCE", ""
).strip()
EXPECTED_HARNESS_CONTENT = os.environ.get(
    "CODEX_EXPECTED_HARNESS_CONTENT", ""
).strip()
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
    unreal.log_error("CODEX_MODERN_UI57_VALIDATION_FAIL: " + message)
    raise RuntimeError(message)


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
if not engine_version.startswith("5.7."):
    fail("Expected UE 5.7 but found " + engine_version)
if not all((RECEIPT_PATH, EVIDENCE_PATH, EXPECTED_HARNESS_CONTENT)):
    fail("All modern UI validation environment variables are required")
if os.path.realpath(unreal.Paths.project_content_dir()).lower() != os.path.realpath(
    EXPECTED_HARNESS_CONTENT
).lower():
    fail("Commandlet is not running inside the audited harness")

with open(os.path.realpath(RECEIPT_PATH), "r", encoding="utf-8-sig") as handle:
    receipt = json.load(handle)
if receipt.get("status") != "ISOLATED_MODERN_UI_HARNESS_PASS":
    fail("Harness receipt status is not PASS")
packages = tuple(
    sorted(row["package"] for row in receipt.get("staged_assets", []))
)
if len(packages) != 127 or len(set(packages)) != 127:
    fail("Harness receipt does not contain 127 unique packages")

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
    fail("Harness receipt root counts differ from the audited baseline")

actual_class_counts = {}
for value in registered.values():
    actual_class_counts[value] = actual_class_counts.get(value, 0) + 1
if actual_class_counts != EXPECTED_CLASS_COUNTS:
    fail("Unexpected modern UI class counts: " + repr(actual_class_counts))

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
unexpected_game_dependencies = set()
for package in packages:
    unexpected_game_dependencies.update(
        item
        for item in {str(value) for value in registry.get_dependencies(package, options)}
        if item.startswith("/Game/") and item not in packages
    )
if unexpected_game_dependencies:
    fail(
        "Unexpected /Game dependencies: "
        + repr(sorted(unexpected_game_dependencies))
    )

user_widget_class = unreal.load_class(None, "/Script/UMG.UserWidget")
if user_widget_class is None:
    fail("UMG.UserWidget did not load")

rows = []
compiled_blueprints = []
native_parents = set()
for package in packages:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        fail("Asset did not load: " + package)
    actual_class = asset.get_class().get_name()
    if actual_class != registered[package]:
        fail("Loaded class differs from AssetRegistry for " + package)
    if actual_class == "WidgetBlueprint":
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
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
            fail("Unexpected native parent {} for {}".format(parent_path, package))
        parent_class = unreal.load_class(None, parent_path)
        if parent_class is None or not unreal.MathLibrary.class_is_child_of(
            generated_class, parent_class
        ):
            fail("Generated class does not derive from {} for {}".format(parent_path, package))
        native_parents.add(parent_path)
        compiled_blueprints.append(
            {
                "package": package,
                "compile_requested": True,
                "generated_class": generated_class.get_path_name(),
                "native_parent": parent_path,
            }
        )
    rows.append({"package": package, "class": actual_class})

if len(compiled_blueprints) != 66:
    fail("Expected 66 compiled WidgetBlueprints")
if len(native_parents) != 52:
    fail("Expected 52 unique native parent classes but found {}".format(len(native_parents)))

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE57_MODERN_UI_READ_ONLY_LOAD_COMPILE_PASS",
    "engine_version": engine_version,
    "package_count": len(rows),
    "root_counts": actual_root_counts,
    "class_counts": actual_class_counts,
    "packages": rows,
    "compiled_widget_blueprints": compiled_blueprints,
    "compiled_widget_blueprint_count": len(compiled_blueprints),
    "unique_native_parent_count": len(native_parents),
    "unique_native_parents": sorted(native_parents),
    "unexpected_game_dependencies": [],
    "asset_save_requested": False,
}
evidence_path = os.path.realpath(EVIDENCE_PATH)
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_MODERN_UI57_VALIDATION_PASS: " + evidence_path)
