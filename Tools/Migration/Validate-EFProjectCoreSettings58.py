"""Validate effective UE 5.8 settings for the migrated EFProjectSystems core batch."""

import datetime
import hashlib
import json
import os

import unreal


PROJECT_ROOT = os.path.realpath(unreal.Paths.project_dir())
OUTPUT_PATH = os.path.join(
    PROJECT_ROOT,
    "Saved",
    "Migration",
    "Phase4",
    "EFProjectCoreSettings58.json",
)


def fail(message):
    unreal.log_error("CODEX_EFCORE_SETTINGS58_FAIL: " + message)
    raise RuntimeError(message)


def require_cdo(class_path):
    cls = unreal.load_class(None, class_path)
    if cls is None:
        fail("Class did not load: " + class_path)
    cdo = unreal.get_default_object(cls)
    if cdo is None:
        fail("CDO did not load: " + class_path)
    return cdo


def get_property(value, *names):
    errors = []
    for name in names:
        try:
            return value.get_editor_property(name)
        except Exception as exception:
            errors.append("{}: {}".format(name, exception))
    fail("Could not read {} ({})".format(names, "; ".join(errors)))


def object_path(value):
    if value is None:
        return ""
    exporter = getattr(value, "export_text", None)
    if exporter:
        exported = exporter()
        if exported:
            return exported
    soft_path_getter = getattr(value, "get_asset_path_name", None)
    if soft_path_getter:
        return str(soft_path_getter())
    getter = getattr(value, "get_path_name", None)
    return getter() if getter else str(value)


def require_path(actual, expected, label):
    if actual != expected:
        fail("{} resolved to {}, expected {}".format(label, actual, expected))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

survival = require_cdo(
    "/Script/EFProjectSystemsCore.EFProjectSurvivalSettings"
)
defeat = require_cdo(
    "/Script/EFProjectSystemsGameplay.ProjectDefeatFlowSettings"
)
background = require_cdo(
    "/Script/EFProjectSystemsGameplay.ProjectCharacterBackgroundSettings"
)

survival_registry = object_path(
    get_property(survival, "ConsumableRegistry", "consumable_registry")
)
require_path(
    survival_registry,
    "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry.DA_FoodConsumableRegistry",
    "ConsumableRegistry",
)
bootstrap_needs = bool(
    get_property(survival, "bBootstrapNeedsHud", "bootstrap_needs_hud")
)
bootstrap_status = bool(
    get_property(survival, "bBootstrapStatusHud", "bootstrap_status_hud")
)
if not bootstrap_needs or not bootstrap_status:
    fail("Survival HUD bootstrap settings are not both enabled")

pain_scalar = float(
    get_property(defeat, "PainPerAppliedDamage", "pain_per_applied_damage")
)
if abs(pain_scalar - 0.01) > 0.0001:
    fail("PainPerAppliedDamage is not the release value: {}".format(pain_scalar))
advanced_defeat = bool(
    get_property(
        defeat, "bEnableAdvancedDefeatFlow", "enable_advanced_defeat_flow"
    )
)
if not advanced_defeat:
    fail("Advanced defeat flow is disabled")
defeat_widget = object_path(
    get_property(
        defeat,
        "KnockoutStruggleWidgetClass",
        "knockout_struggle_widget_class",
    )
)
require_path(
    defeat_widget,
    "/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget.WBP_ProjectKnockoutStruggleWidget_C",
    "KnockoutStruggleWidgetClass",
)

background_paths = {
    "backstory_data_table": object_path(
        get_property(background, "BackstoryDataTable", "backstory_data_table")
    ),
    "profession_data_table": object_path(
        get_property(
            background, "ProfessionDataTable", "profession_data_table"
        )
    ),
    "creation_widget_class": object_path(
        get_property(background, "CreationWidgetClass", "creation_widget_class")
    ),
    "preview_image_texture": object_path(
        get_property(background, "PreviewImageTexture", "preview_image_texture")
    ),
}
expected_background_paths = {
    "backstory_data_table": "/Game/Data/CharacterBackground/DT_ProjectBackstories.DT_ProjectBackstories",
    "profession_data_table": "/Game/Data/CharacterBackground/DT_ProjectProfessions.DT_ProjectProfessions",
    "creation_widget_class": "/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget.WBP_ProjectCharacterBackgroundCreationWidget_C",
    "preview_image_texture": "/Game/_Game/Images/T_ProjectCharacterBackgroundPreview.T_ProjectCharacterBackgroundPreview",
}
for label, expected in expected_background_paths.items():
    require_path(background_paths[label], expected, label)

story_maps = [
    str(value)
    for value in get_property(
        background, "StorySelectionMapNames", "story_selection_map_names"
    )
]
if story_maps != ["StorySelection"]:
    fail("Unexpected story selection maps: " + repr(story_maps))
preview_png_path = str(
    get_property(background, "PreviewImagePngPath", "preview_image_png_path")
)
if preview_png_path != "Content/_Game/Images/preview.png":
    fail("Unexpected preview PNG fallback: " + preview_png_path)

preview_png = os.path.join(
    PROJECT_ROOT, "Content", "_Game", "Images", "preview.png"
)
if not os.path.isfile(preview_png):
    fail("Preview PNG fallback is absent")
if os.path.getsize(preview_png) != 749769 or sha256(preview_png) != (
    "6B4075152BB866EB6B05AB8E24AD68A1138756AA0899681B348FA38F8DE288D3"
):
    fail("Preview PNG fallback differs from the audited source")

preview_asset = unreal.EditorAssetLibrary.load_asset(
    expected_background_paths["preview_image_texture"]
)
if preview_asset is None or preview_asset.get_class().get_name() != "Texture2D":
    fail("Configured preview Texture2D did not load")

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_EFFECTIVE_CORE_SETTINGS_PASS",
    "engine_version": engine_version,
    "survival": {
        "consumable_registry": survival_registry,
        "bootstrap_needs_hud": bootstrap_needs,
        "bootstrap_status_hud": bootstrap_status,
    },
    "defeat": {
        "advanced_flow": advanced_defeat,
        "pain_per_applied_damage": pain_scalar,
        "knockout_widget": defeat_widget,
    },
    "character_background": {
        **background_paths,
        "story_selection_map_names": story_maps,
        "preview_png_path": preview_png_path,
        "preview_png_sha256": sha256(preview_png),
    },
    "asset_saves": 0,
    "map_load_requested": False,
}
os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFCORE_SETTINGS58_PASS: " + OUTPUT_PATH)
