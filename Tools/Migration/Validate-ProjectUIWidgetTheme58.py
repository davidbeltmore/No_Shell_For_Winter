"""Compile project-owned UI WBPs and run non-mutating CodeWidgetDesignerBridge gates."""

import datetime
import json
import os

import unreal


REPORT_PATH = os.path.realpath(
    os.path.join(
        unreal.Paths.project_saved_dir(),
        "Migration",
        "Phase3",
        "ProjectUIWidgetTheme58.json",
    )
)

BRIDGE_ROOTS = (
    (
        "/Script/EFProjectSystemsGameplay.ProjectEmoteMenuWidget",
        "/Game/_Game/Widgets/Y/Main/WBP_ProjectEmoteMenu",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectGameplayDebugMenuWidget",
        "/Game/_Game/Widgets/Debug/Main/WBP_ProjectGameplayDebugMenu",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectActivityFeedWidget",
        "/Game/_Game/Widgets/Chronicle/Main/WBP_ProjectChronicleWidget",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectSurvivalStatusWidget",
        "/Game/_Game/Widgets/Status/Main/WBP_ProjectSurvivalStatusWidget",
    ),
    (
        "/Script/EFProjectSystemsGameplay.ProjectSinfulAscensionExchangeMenuWidget",
        "/Game/_Game/Widgets/SinfulAscensionAltar/Main/WBP_ProjectSinfulAscensionExchangeMenu",
    ),
)


def fail(message):
    unreal.log_error("CODEX_PROJECT_UI_WIDGET_FAIL: " + message)
    raise RuntimeError(message)


def normalized_blueprint_status(blueprint):
    status = str(blueprint.get_editor_property("status"))
    return status, "".join(character for character in status.upper() if character.isalnum())


def unpack_bridge_result(result, operation, class_path):
    if not isinstance(result, tuple) or not result:
        fail("{} returned an unexpected result for {}: {!r}".format(operation, class_path, result))
    passed = bool(result[0])
    validation = next(
        (item for item in result[1:] if hasattr(item, "get_editor_property")),
        None,
    )
    report_text = next((str(item) for item in result[1:] if isinstance(item, str)), "")
    validation_passed = bool(validation.get_editor_property("passed")) if validation is not None else passed
    if not passed or not validation_passed:
        fail("{} failed for {}: {}".format(operation, class_path, report_text))
    return {
        "operation": operation,
        "passed": passed,
        "validation_passed": validation_passed,
        "report": report_text,
    }


compiled_widgets = []
for asset_path in unreal.EditorAssetLibrary.list_assets(
    "/Game/_Game/Widgets", recursive=True, include_folder=False
):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None or asset.get_class().get_name() != "WidgetBlueprint":
        continue
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    status, normalized_status = normalized_blueprint_status(asset)
    if "UPTODATE" not in normalized_status:
        fail("WidgetBlueprint compile status is {} for {}".format(status, asset_path))
    compiled_widgets.append({"asset": asset_path, "status": status})

if not compiled_widgets:
    fail("No project-owned WidgetBlueprints were compiled")

bridge_results = []
mode = unreal.CodeWidgetDesignerGenerationMode.PRESERVE_MANUAL
for class_path, target_path in BRIDGE_ROOTS:
    native_class = unreal.load_class(None, class_path)
    if native_class is None:
        fail("Native widget class did not load: " + class_path)
    if not unreal.EditorAssetLibrary.does_asset_exist(target_path):
        fail("Expected manual WidgetBlueprint is missing: " + target_path)

    preflight_result = unreal.CodeWidgetToWBPBridgeLibrary.preflight_widget_blueprint_conversion(
        native_class,
        target_path,
        mode,
        False,
    )
    preflight = unpack_bridge_result(preflight_result, "preflight", class_path)

    validation_result = unreal.CodeWidgetToWBPBridgeLibrary.validate_widget_blueprint_conversion(
        native_class,
        target_path,
    )
    validation = unpack_bridge_result(validation_result, "validation", class_path)
    bridge_results.append(
        {
            "native_class": class_path,
            "target_asset": target_path,
            "preflight": preflight,
            "validation": validation,
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "PROJECT_UI_WIDGET_THEME_PASS",
    "compiled_widget_count": len(compiled_widgets),
    "compiled_widgets": compiled_widgets,
    "bridge_roots": bridge_results,
    "generation_mode": "PreserveManual",
    "designer_roots_replaced": False,
}
os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_PROJECT_UI_WIDGET_PASS: " + REPORT_PATH)
