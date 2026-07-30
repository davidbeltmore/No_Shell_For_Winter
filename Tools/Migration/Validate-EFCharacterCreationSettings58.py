"""Validate the effective target-safe EFCharacterCreation settings in UE 5.8."""

import datetime
import json
import os

import unreal


EVIDENCE_PATH = os.path.realpath(
    os.path.join(
        unreal.Paths.project_dir(),
        "Saved",
        "Migration",
        "Phase3",
        "EFCharacterCreationSettings58.json",
    )
)
EXPECTED_BODY_MESH = "/Game/DazToUnreal/Female/Female.Female"
EXPECTED_GENDER_MESHES = {
    "Male": "/Game/DazToUnreal/Male/Male.Male",
    "Female": "/Game/DazToUnreal/Female/Female.Female",
}
EXPECTED_ROOT_CLASS = (
    "/EFCharacterCreation/UI/WBP_EFCharacterCreationRoot."
    "WBP_EFCharacterCreationRoot_C"
)
EXPECTED_SLIDER_CLASS = (
    "/EFCharacterCreation/UI/WBP_EFMorphSlider.WBP_EFMorphSlider_C"
)


def fail(message):
    unreal.log_error("CODEX_EFCC_CDO_FAIL: " + message)
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


def enum_name(value):
    text = str(value).lower()
    for candidate in ("not_applicable", "female", "male"):
        if candidate in text:
            return candidate
    return text


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

settings_class = unreal.load_class(
    None, "/Script/EFCharacterCreationRuntime.EFCharacterCreationSettings"
)
if settings_class is None:
    fail("Settings class is missing")

cdo = unreal.get_default_object(settings_class)
if cdo is None:
    fail("Settings CDO is missing")
if get_property(cdo, "bAutoEnterTestingMap", "auto_enter_testing_map") is not False:
    fail("bAutoEnterTestingMap must be false")
if get_property(
    cdo,
    "bAutoOpenOnCompatibleMainPawn",
    "auto_open_on_compatible_main_pawn",
) is not False:
    fail("bAutoOpenOnCompatibleMainPawn must be false")

options = list(get_property(cdo, "BodyMeshOptions", "body_mesh_options"))
if len(options) != 1:
    fail("Expected exactly one BodyMeshOptions entry, got {}".format(len(options)))
option = options[0]
display_name = str(get_property(option, "DisplayName", "display_name"))
body_mesh_path = object_path(get_property(option, "SkeletalMesh", "skeletal_mesh"))
if display_name != "Female":
    fail("Body mesh display name is not Female: " + display_name)
if body_mesh_path != EXPECTED_BODY_MESH:
    fail("Unexpected effective body mesh: " + body_mesh_path)

default_name = str(get_property(cdo, "DefaultCharacterName", "default_character_name"))
max_name_length = int(get_property(cdo, "MaxCharacterNameLength", "max_character_name_length"))
default_gender_value = get_property(cdo, "DefaultGender", "default_gender")
default_gender = enum_name(default_gender_value)
if default_name != "Player":
    fail("DefaultCharacterName must be Player: " + default_name)
if max_name_length != 32:
    fail("MaxCharacterNameLength must be 32, got {}".format(max_name_length))
if default_gender != "female":
    fail("DefaultGender must be Female: " + str(default_gender_value))

gender_options = list(get_property(cdo, "GenderMeshOptions", "gender_mesh_options"))
if len(gender_options) != 2:
    fail("Expected exactly two GenderMeshOptions entries, got {}".format(len(gender_options)))
resolved_gender_options = {}
for gender_option in gender_options:
    gender_name = str(get_property(gender_option, "DisplayName", "display_name"))
    raw_gender_value = get_property(gender_option, "Gender", "gender")
    gender_value = enum_name(raw_gender_value)
    gender_mesh_path = object_path(get_property(gender_option, "SkeletalMesh", "skeletal_mesh"))
    if gender_name not in EXPECTED_GENDER_MESHES:
        fail("Unexpected visible gender option: " + gender_name)
    if gender_value != gender_name.lower():
        fail("Gender enum does not match display name: {} / {}".format(raw_gender_value, gender_name))
    if gender_mesh_path != EXPECTED_GENDER_MESHES[gender_name]:
        fail("Unexpected {} gender mesh: {}".format(gender_name, gender_mesh_path))
    resolved_gender_options[gender_name] = gender_mesh_path
if resolved_gender_options != EXPECTED_GENDER_MESHES:
    fail("Gender options must contain exactly Male and Female")

root_class_path = object_path(
    get_property(cdo, "RootWidgetClass", "root_widget_class")
)
slider_class_path = object_path(
    get_property(cdo, "MorphSliderWidgetClass", "morph_slider_widget_class")
)
if root_class_path != EXPECTED_ROOT_CLASS:
    fail("Unexpected root widget class: " + root_class_path)
if slider_class_path != EXPECTED_SLIDER_CLASS:
    fail("Unexpected morph slider widget class: " + slider_class_path)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "EFFECTIVE_CONFIG_PASS",
    "engine_version": engine_version,
    "auto_enter_testing_map": False,
    "auto_open_on_compatible_main_pawn": False,
    "body_mesh_options": [
        {"display_name": display_name, "skeletal_mesh": body_mesh_path}
    ],
    "default_character_name": default_name,
    "max_character_name_length": max_name_length,
    "default_gender": default_gender,
    "gender_mesh_options": resolved_gender_options,
    "root_widget_class": root_class_path,
    "morph_slider_widget_class": slider_class_path,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFCC_CDO_PASS: " + EVIDENCE_PATH)
