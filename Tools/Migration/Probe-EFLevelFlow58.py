"""Read-only UE 5.8 structural probe for EFLevelFlow."""

import datetime
import json
import os

import unreal


OUTPUT_PATH = os.path.realpath(
    os.path.join(
        unreal.Paths.project_dir(),
        "Saved",
        "Migration",
        "Phase3",
        "EFLevelFlowReadOnlyProbe58.json",
    )
)
SETTINGS_PATH = "/Script/EFLevelFlowRuntime.EFLevelFlowSettings"
SUBSYSTEM_PATH = "/Script/EFLevelFlowRuntime.EFLevelFlowSubsystem"
WIDGET_PATH = (
    "/AscentCombatFramework/UITools/Widgets/"
    "ANS_LoadingScreen_WB.ANS_LoadingScreen_WB_C"
)
REDIRECT_LINES = (
    '+ClassRedirects=(OldName="/Script/CalystoLevelFlowRuntime.'
    'CalystoLevelFlowSubsystem",NewName="/Script/EFLevelFlowRuntime.'
    'EFLevelFlowSubsystem")',
    '+ClassRedirects=(OldName="/Script/CalystoLevelFlowRuntime.'
    'CalystoLevelFlowSettings",NewName="/Script/EFLevelFlowRuntime.'
    'EFLevelFlowSettings")',
)


def fail(message):
    unreal.log_error("CODEX_EFLEVELFLOW_PROBE_FAIL: " + message)
    raise RuntimeError(message)


def object_path(value):
    return value.get_path_name() if value is not None else ""


def get_property(value, *names):
    errors = []
    for name in names:
        try:
            return value.get_editor_property(name)
        except Exception as exception:
            errors.append("{}: {}".format(name, exception))
    fail("Could not read any of {} ({})".format(names, "; ".join(errors)))


def require_class(path):
    value = unreal.load_class(None, path)
    if value is None:
        fail("Class did not load: " + path)
    return value


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

settings_class = require_class(SETTINGS_PATH)
subsystem_class = require_class(SUBSYSTEM_PATH)
settings_cdo = unreal.get_default_object(settings_class)
subsystem_cdo = unreal.get_default_object(subsystem_class)
if settings_cdo is None:
    fail("EFLevelFlowSettings CDO is missing")
if subsystem_cdo is None:
    fail("EFLevelFlowSubsystem CDO is missing")

game_instance_subsystem_class = require_class(
    "/Script/Engine.GameInstanceSubsystem"
)
if not unreal.MathLibrary.class_is_child_of(
    subsystem_class, game_instance_subsystem_class
):
    fail("EFLevelFlowSubsystem is not a GameInstanceSubsystem subclass")

delayed_maps = [
    str(value)
    for value in get_property(
        settings_cdo, "DelayedSpawnMapNames", "delayed_spawn_map_names"
    )
]
max_attempts = int(
    get_property(settings_cdo, "MaxResolveAttempts", "max_resolve_attempts")
)
poll_interval = float(
    get_property(
        settings_cdo,
        "LoadingPollIntervalSeconds",
        "loading_poll_interval_seconds",
    )
)
minimum_seconds = float(
    get_property(
        settings_cdo,
        "MinimumLoadingScreenSeconds",
        "minimum_loading_screen_seconds",
    )
)
freeze_pawn = bool(
    get_property(
        settings_cdo,
        "bFreezePawnDuringLoading",
        "freeze_pawn_during_loading",
    )
)
block_input = bool(
    get_property(
        settings_cdo,
        "bBlockPlayerInputDuringLoading",
        "block_player_input_during_loading",
    )
)

widget_class = require_class(WIDGET_PATH)
user_widget_class = require_class("/Script/UMG.UserWidget")
if not unreal.MathLibrary.class_is_child_of(widget_class, user_widget_class):
    fail("Loading widget is not a UUserWidget subclass: " + object_path(widget_class))

configured_widget = get_property(
    settings_cdo,
    "LoadingScreenWidgetClass",
    "loading_screen_widget_class",
)
configured_widget_path = object_path(configured_widget)

if delayed_maps != ["DungeonGeneration"]:
    fail("Unexpected DelayedSpawnMapNames: {}".format(delayed_maps))
if max_attempts != 900:
    fail("Unexpected MaxResolveAttempts: {}".format(max_attempts))
if abs(poll_interval - 0.1) > 0.0001:
    fail("Unexpected LoadingPollIntervalSeconds: {}".format(poll_interval))
if abs(minimum_seconds - 10.0) > 0.0001:
    fail("Unexpected MinimumLoadingScreenSeconds: {}".format(minimum_seconds))
if not freeze_pawn:
    fail("bFreezePawnDuringLoading must be true")
if not block_input:
    fail("bBlockPlayerInputDuringLoading must be true")
if configured_widget_path != WIDGET_PATH:
    fail("Unexpected configured loading widget: " + configured_widget_path)

default_engine_path = os.path.realpath(
    os.path.join(unreal.Paths.project_config_dir(), "DefaultEngine.ini")
)
with open(default_engine_path, "r", encoding="utf-8-sig") as handle:
    default_engine_text = handle.read()
for redirect_line in REDIRECT_LINES:
    if redirect_line not in default_engine_text:
        fail("Configured redirect line is absent: " + redirect_line)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "READ_ONLY_STRUCTURAL_PROBE_PASS",
    "engine_version": engine_version,
    "classes": {
        "settings": object_path(settings_class),
        "settings_cdo": object_path(settings_cdo),
        "subsystem": object_path(subsystem_class),
        "subsystem_cdo": object_path(subsystem_cdo),
        "is_game_instance_subsystem": True,
    },
    "effective_settings": {
        "delayed_spawn_map_names": delayed_maps,
        "max_resolve_attempts": max_attempts,
        "loading_poll_interval_seconds": poll_interval,
        "minimum_loading_screen_seconds": minimum_seconds,
        "freeze_pawn_during_loading": freeze_pawn,
        "block_player_input_during_loading": block_input,
        "loading_screen_widget_class": configured_widget_path,
    },
    "loading_widget": {
        "class": object_path(widget_class),
        "cdo": object_path(unreal.get_default_object(widget_class)),
        "is_user_widget": True,
    },
    "redirects": {
        "configured_lines": list(REDIRECT_LINES),
        "destination_classes_loaded": [SUBSYSTEM_PATH, SETTINGS_PATH],
        "serialized_reference_resolution": "PENDING_ASSET_LOAD",
    },
    "map_load_requested": False,
    "asset_save_requested": False,
}

os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFLEVELFLOW_PROBE_PASS: " + OUTPUT_PATH)
