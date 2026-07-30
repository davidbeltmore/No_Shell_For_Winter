"""Create only missing Status manifest WBPs while preserving every existing Designer root."""

import unreal


native_class = unreal.load_class(
    None, "/Script/EFProjectSystemsGameplay.ProjectSurvivalStatusWidget"
)
if native_class is None:
    raise RuntimeError("ProjectSurvivalStatusWidget did not load")

result = unreal.CodeWidgetToWBPBridgeLibrary.create_or_update_widget_blueprints_from_manifest(
    native_class,
    "/Game/_Game/Widgets/Status/Main/WBP_ProjectSurvivalStatusWidget",
    unreal.CodeWidgetDesignerGenerationMode.PRESERVE_MANUAL,
    True,
)
result_text = str(result)
if not result_text or "Validation passed for 'Status'" not in result_text:
    unreal.log_error("CODEX_STATUS_WBP_PRESERVE_FAIL: {!r}".format(result))
    raise RuntimeError("PreserveManual Status manifest generation failed")

missing_after = [
    path
    for path in (
        "/Game/_Game/Widgets/Status/Slots/WBP_ProjectSurvivalStatus_TiredSlot",
        "/Game/_Game/Widgets/Status/Slots/WBP_ProjectSurvivalStatus_SweatySlot",
    )
    if not unreal.EditorAssetLibrary.does_asset_exist(path)
]
if missing_after:
    raise RuntimeError("Expected Status WBPs remain missing: {!r}".format(missing_after))

unreal.log("CODEX_STATUS_WBP_PRESERVE_PASS")
