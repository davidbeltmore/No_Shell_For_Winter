"""Organize migrated animations under /Game/_Game/Animations with UE 5.8.

All moves use Unreal's asset rename API so references are rewritten. The three
already-modern intimacy assets remain authoritative; their unreferenced legacy
duplicates are removed only after live replacement and referencer checks.
"""

import datetime
import json
import os

import unreal


SOURCE_ROOT = "/Game/ExportedAnimations"
DESTINATION_ROOT = "/Game/_Game/Animations"
EXPECTED_LEGACY_COUNT = 661
EXPECTED_MOVED_COUNT = 658
UEFN_SKELETON = (
    "/Game/FullSample/GASP/UEFN_Mannequin/Meshes/"
    "SK_UEFN_Mannequin.SK_UEFN_Mannequin"
)
FEMALE_MESH = "/Game/DazToUnreal/Female/Female.Female"
MALE_MESH = "/Game/DazToUnreal/Male/Male.Male"
AUTHORITATIVE_REPLACEMENTS = {
    "/Game/ExportedAnimations/Together/0001Scene": (
        "/Game/_Game/Animations/Intimacy/Scenes/BP_IntimacyScene_0001"
    ),
    "/Game/ExportedAnimations/SexAnimations/AS_DoggyClassic_1_Female": (
        "/Game/_Game/Animations/Intimacy/Female/AS_DoggyClassic_1_Female"
    ),
    "/Game/ExportedAnimations/M_SexAnimations/AS_DoggyClassic_1_Male_Corrected": (
        "/Game/_Game/Animations/Intimacy/Male/AS_DoggyClassic_1_Male_Corrected"
    ),
}


def fail(message):
    unreal.log_error("CODEX_ORGANIZE_ANIMATIONS58_FAIL: " + message)
    raise RuntimeError(message)


def object_path(value):
    if value is None:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def normalized_status(blueprint):
    raw = str(blueprint.get_editor_property("status"))
    return raw, "".join(character for character in raw.upper() if character.isalnum())


def clear_transient_native_cdo_animation_references():
    """Prevent AssetRenameManager's interactive native-CDO warning.

    The values are changed only in this commandlet process and are never saved.
    A fresh process reconstructs the CDOs from the newly compiled defaults.
    """

    specifications = (
        (
            "/Script/EFProjectSystemsGameplay.ProjectEmoteComponent",
            {
                "action_interactions": [],
                "object_interactions": [],
                "menu_data_asset": None,
            },
        ),
        (
            "/Script/EFProjectSystemsGameplay.ProjectLocomotionOverrideComponent",
            {
                "walk_loop_animation": None,
                "walk_pivot_animation": None,
                "walk_idle_animation": None,
                "crawl_entry_animation": None,
                "crawl_exit_animation": None,
                "crawl_idle_animation": None,
                "crawl_forward_animation": None,
            },
        ),
        (
            "/Script/ACFTrainingSystem.ACFTrainingSettings",
            {"training_definitions": []},
        ),
    )
    rows = []
    for class_path, values in specifications:
        native_class = unreal.load_class(None, class_path)
        if native_class is None:
            fail("Could not load native class for transient CDO gate: " + class_path)
        cdo = unreal.get_default_object(native_class)
        if cdo is None:
            fail("Could not get native CDO for transient gate: " + class_path)
        for property_name, value in values.items():
            cdo.set_editor_property(property_name, value)
        rows.append({"class": class_path, "cleared_properties": sorted(values)})
    return rows


def root_destination(name):
    lowered = name.lower()
    if "crawl" in lowered:
        return DESTINATION_ROOT + "/Locomotion/Crawl"
    if any(token in lowered for token in ("swim", "diving", "flutterkick")):
        return DESTINATION_ROOT + "/Locomotion/Swim"
    if any(token in lowered for token in ("walk", "run", "jog", "pivot")):
        return DESTINATION_ROOT + "/Locomotion/Ground"
    if any(token in lowered for token in ("jump", "fall", "landing")):
        return DESTINATION_ROOT + "/Locomotion/Air"
    if "dance" in lowered:
        return DESTINATION_ROOT + "/Emotes/Dance"
    if any(token in lowered for token in ("sit", "seiza")):
        return DESTINATION_ROOT + "/Emotes/Sit"
    if any(
        token in lowered
        for token in (
            "idle",
            "look",
            "speak",
            "sleep",
            "lay",
            "lying",
            "bow",
            "clap",
            "wave",
            "cheer",
            "cry",
            "laugh",
            "angry",
            "happy",
            "sad",
        )
    ):
        return DESTINATION_ROOT + "/Emotes/General"
    return DESTINATION_ROOT + "/Kawaii/Female"


def destination_for(package):
    relative = package[len(SOURCE_ROOT) + 1 :]
    parts = relative.split("/")
    name = parts[-1]
    if len(parts) == 1:
        folder = root_destination(name)
    elif parts[0] == "M_AdultAnimations":
        folder = DESTINATION_ROOT + "/Intimacy/Male/Adult"
    elif parts[0] == "M_SexAnimations":
        folder = DESTINATION_ROOT + "/Intimacy/Male"
    elif parts[0] == "SexAnimations":
        folder = DESTINATION_ROOT + "/Intimacy/Female"
    elif parts[0] == "Together":
        folder = DESTINATION_ROOT + "/Intimacy/Scenes"
    else:
        fail("Unknown legacy animation folder: " + package)
    return folder + "/" + name


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if os.path.basename(unreal.Paths.get_project_file_path()).lower() != "noshellforwinter.uproject":
    fail("This script may run only in NoShellForWinter")

evidence_path = os.path.realpath(
    os.environ.get("CODEX_ANIMATION58_ORGANIZE_EVIDENCE", "").strip()
)
project_saved = os.path.realpath(
    os.path.join(unreal.Paths.project_saved_dir(), "Migration")
)
if not evidence_path or not evidence_path.lower().startswith(project_saved.lower() + os.sep):
    fail("Evidence path must remain below target Saved/Migration")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([SOURCE_ROOT, DESTINATION_ROOT], True)
registry.wait_for_completion()
legacy_data = list(
    registry.get_assets_by_path(
        SOURCE_ROOT, recursive=True, include_only_on_disk_assets=True
    )
)
legacy_packages = sorted({str(row.package_name) for row in legacy_data})
organized_before_data = list(
    registry.get_assets_by_path(
        DESTINATION_ROOT, recursive=True, include_only_on_disk_assets=True
    )
)
organized_before_packages = {str(row.package_name) for row in organized_before_data}
authoritative_packages = set(AUTHORITATIVE_REPLACEMENTS.values())
already_moved_packages = organized_before_packages - authoritative_packages
if len(legacy_packages) + len(already_moved_packages) != EXPECTED_LEGACY_COUNT - 3:
    fail(
        "Legacy/resumed package accounting differs: legacy={} moved={}".format(
            len(legacy_packages), len(already_moved_packages)
        )
    )
if authoritative_packages - organized_before_packages:
    fail("An authoritative intimacy replacement is missing")

replacement_checks = []
for legacy_path, modern_path in AUTHORITATIVE_REPLACEMENTS.items():
    modern = unreal.EditorAssetLibrary.load_asset(modern_path)
    if modern is None:
        fail("Authoritative replacement failed to load: " + modern_path)
    legacy = unreal.EditorAssetLibrary.load_asset(legacy_path)
    row = {
        "legacy": legacy_path,
        "modern": modern_path,
        "legacy_present": legacy is not None,
        "legacy_class": legacy.get_class().get_name() if legacy else "",
        "modern_class": modern.get_class().get_name(),
    }
    if legacy and (
        legacy_path.endswith("AS_DoggyClassic_1_Female") or legacy_path.endswith(
        "AS_DoggyClassic_1_Male_Corrected"
        )
    ):
        if legacy.get_class().get_name() != modern.get_class().get_name():
            fail("Animation replacement class differs: " + legacy_path)
        legacy_skeleton = object_path(legacy.get_editor_property("skeleton"))
        modern_skeleton = object_path(modern.get_editor_property("skeleton"))
        legacy_length = float(legacy.get_play_length())
        modern_length = float(modern.get_play_length())
        if (
            legacy_skeleton != UEFN_SKELETON
            or modern_skeleton != UEFN_SKELETON
            or abs(legacy_length - modern_length) > 0.0001
        ):
            fail("Animation replacement compatibility differs: " + legacy_path)
        row.update(
            {
                "skeleton": legacy_skeleton,
                "legacy_length": legacy_length,
                "modern_length": modern_length,
            }
        )
    replacement_checks.append(row)

# Delete the obsolete scene first; it is the only expected referencer of the
# obsolete paired sequences. Every delete is guarded by a fresh live query.
deleted_duplicates = []
for legacy_path in (
    "/Game/ExportedAnimations/Together/0001Scene",
    "/Game/ExportedAnimations/SexAnimations/AS_DoggyClassic_1_Female",
    "/Game/ExportedAnimations/M_SexAnimations/AS_DoggyClassic_1_Male_Corrected",
):
    if not unreal.EditorAssetLibrary.does_asset_exist(legacy_path):
        continue
    referencers = list(
        unreal.EditorAssetLibrary.find_package_referencers_for_asset(
            legacy_path, load_assets_to_confirm=True
        )
    )
    if referencers:
        fail(
            "Guarded legacy duplicate still has referencers: {} <- {}".format(
                legacy_path, referencers
            )
        )
    if not unreal.EditorAssetLibrary.delete_asset(legacy_path):
        fail("Could not remove guarded legacy duplicate: " + legacy_path)
    deleted_duplicates.append(legacy_path)

move_plan = []
for package in legacy_packages:
    if package in AUTHORITATIVE_REPLACEMENTS:
        continue
    destination = destination_for(package)
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        fail("Destination collision: " + destination)
    move_plan.append((package, destination))
if len(move_plan) != EXPECTED_MOVED_COUNT:
    if len(move_plan) + len(already_moved_packages) != EXPECTED_MOVED_COUNT:
        fail(
            "Unexpected resumed move-plan count: remaining={} already={}".format(
                len(move_plan), len(already_moved_packages)
            )
        )
if len({destination.lower() for _source, destination in move_plan}) != len(move_plan):
    fail("Move plan contains destination collisions")

cdo_clear_rows = clear_transient_native_cdo_animation_references()

folders = sorted({destination.rsplit("/", 1)[0] for _source, destination in move_plan})
for folder in folders:
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        if not unreal.EditorAssetLibrary.make_directory(folder):
            fail("Could not create destination folder: " + folder)

moved_rows = []
for index, (source, destination) in enumerate(move_plan, start=1):
    if index == 1 or index % 25 == 0 or index == len(move_plan):
        unreal.log(
            "CODEX_ORGANIZE_ANIMATIONS58_PROGRESS: {} / {}".format(
                index, len(move_plan)
            )
        )
    if not unreal.EditorAssetLibrary.rename_asset(source, destination):
        fail("Unreal rename failed: {} -> {}".format(source, destination))
    moved_rows.append({"source": source, "destination": destination})

registry.scan_paths_synchronous([SOURCE_ROOT, DESTINATION_ROOT], True)
registry.wait_for_completion()

load_failures = []
blueprint_rows = []
class_counts = {}
organized_data = list(
    registry.get_assets_by_path(
        DESTINATION_ROOT, recursive=True, include_only_on_disk_assets=True
    )
)
for row in organized_data:
    package = str(row.package_name)
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        load_failures.append(package)
        continue
    class_name = asset.get_class().get_name()
    class_counts[class_name] = class_counts.get(class_name, 0) + 1
    if isinstance(asset, unreal.Blueprint):
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        raw_status, status = normalized_status(asset)
        blueprint_rows.append({"package": package, "status": raw_status})
        if "UPTODATE" not in status:
            fail("Blueprint did not compile UP_TO_DATE: {} ({})".format(package, raw_status))
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
            fail("Could not save compiled Blueprint: " + package)
if load_failures:
    fail("Organized assets failed to load: " + repr(load_failures[:20]))

expected_key_assets = {
    "walk": DESTINATION_ROOT + "/Locomotion/Ground/Anim_KA_Walk04",
    "walk_pivot": DESTINATION_ROOT + "/Locomotion/Ground/Anim_KA_Walk04_Pivot",
    "walk_idle": DESTINATION_ROOT + "/Emotes/General/Anim_KA_Idle01_breathing",
    "crawl_entry": DESTINATION_ROOT + "/Locomotion/Crawl/Anim_KA_Crawling_Baby_Entry",
    "crawl_exit": DESTINATION_ROOT + "/Locomotion/Crawl/Anim_KA_Crawling_Baby_Exit",
    "crawl_idle": DESTINATION_ROOT + "/Locomotion/Crawl/Anim_KA_Crawling_Baby_Idle",
    "crawl_forward": DESTINATION_ROOT + "/Locomotion/Crawl/Anim_KA_Crawling_Baby_Walk_Fwd",
    "dance": DESTINATION_ROOT + "/Emotes/Dance/Anim_KA_Idle63_Dance05_Loop",
    "sit_training": DESTINATION_ROOT + "/Emotes/Sit/Anim_KA_Idle53_Seiza_Loop1",
    "looking_back": DESTINATION_ROOT + "/Emotes/General/Anim_KA_Idle11_LookingBack",
    "private_solo": DESTINATION_ROOT + "/Intimacy/Female/P_INT_Solo_Private01",
}
key_asset_rows = {}
for key, path in expected_key_assets.items():
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        fail("Required organized animation is absent: " + path)
    skeleton = object_path(asset.get_editor_property("skeleton"))
    preview_mesh = ""
    try:
        preview_mesh = object_path(
            asset.get_editor_property("preview_skeletal_mesh")
        )
    except Exception as error:
        unreal.log_warning(
            "Preview mesh property unavailable for {}: {}".format(path, error)
        )
    if skeleton != UEFN_SKELETON:
        fail("Required animation uses the wrong skeleton: " + path)
    if preview_mesh and preview_mesh not in (FEMALE_MESH, MALE_MESH):
        fail("Required animation uses the wrong preview mesh: " + path)
    key_asset_rows[key] = {
        "path": path,
        "class": asset.get_class().get_name(),
        "skeleton": skeleton,
        "preview_mesh": preview_mesh,
        "length": float(asset.get_play_length()),
    }

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_ORGANIZED_ANIMATIONS_PASS",
    "engine_version": engine_version,
    "source_root": SOURCE_ROOT,
    "destination_root": DESTINATION_ROOT,
    "legacy_package_count_before": len(legacy_packages),
    "already_moved_package_count": len(already_moved_packages),
    "moved_package_count": len(moved_rows),
    "deleted_duplicate_count": len(deleted_duplicates),
    "deleted_duplicates": deleted_duplicates,
    "transient_cdo_reference_clear": cdo_clear_rows,
    "authoritative_replacement_checks": replacement_checks,
    "organized_asset_count": len(organized_data),
    "class_counts": class_counts,
    "compiled_blueprint_count": len(blueprint_rows),
    "blueprints": blueprint_rows,
    "key_assets": key_asset_rows,
    "folders": folders,
    "moves": moved_rows,
}
os.makedirs(os.path.dirname(evidence_path), exist_ok=True)
with open(evidence_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
unreal.log("CODEX_ORGANIZE_ANIMATIONS58_PASS: " + evidence_path)
