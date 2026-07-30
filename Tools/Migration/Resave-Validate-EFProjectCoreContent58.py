"""Compile, contract-check, and resave the Phase 4 core-content batch in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


PROJECT_ROOT = os.path.realpath(unreal.Paths.project_dir())
CONTENT_ROOT = os.path.realpath(unreal.Paths.project_content_dir())
EVIDENCE_PATH = os.path.join(
    PROJECT_ROOT,
    "Saved",
    "Migration",
    "Phase4",
    "EFProjectCoreContent58Resave.json",
)

DATA_TABLES = (
    "/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses",
    "/Game/Data/CharacterBackground/DT_ProjectBackstories",
    "/Game/Data/CharacterBackground/DT_ProjectProfessions",
)
REGISTRY_ASSET = (
    "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry"
)
WIDGETS = {
    "/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget": (
        "/Script/EFProjectSystemsGameplay."
        "ProjectCharacterBackgroundCreationWidget"
    ),
    "/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget": (
        "/Script/EFProjectSystemsGameplay.ProjectKnockoutStruggleWidget"
    ),
}
TEXTURES = (
    "/Game/_Game/Icons/Bleeding",
    "/Game/_Game/Icons/Dirt",
    "/Game/_Game/Icons/Dizzy",
    "/Game/_Game/Icons/Exhausted",
    "/Game/_Game/Icons/extremepain_transparent",
    "/Game/_Game/Icons/Fear",
    "/Game/_Game/Icons/frenzy_transparent",
    "/Game/_Game/Icons/gracestep_transparent",
    "/Game/_Game/Icons/Hungry",
    "/Game/_Game/Icons/knockedout_transparent",
    "/Game/_Game/Icons/Orgasm",
    "/Game/_Game/Icons/SleepDeprived",
    "/Game/_Game/Icons/Thirst",
)
EXPECTED_PACKAGES = (*DATA_TABLES, REGISTRY_ASSET, *WIDGETS, *TEXTURES)


def fail(message):
    unreal.log_error("CODEX_EFCORE_CONTENT58_RESAVE_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def package_file(package_name):
    relative = package_name[len("/Game/") :].replace("/", os.sep)
    return os.path.realpath(os.path.join(CONTENT_ROOT, relative + ".uasset"))


def object_path(value):
    return value.get_path_name() if value is not None else ""


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)
if os.path.realpath(CONTENT_ROOT).lower() != os.path.realpath(
    os.path.join(PROJECT_ROOT, "Content")
).lower():
    fail("Project Content invariant failed")

preview_path = os.path.realpath(
    os.path.join(PROJECT_ROOT, "Content", "_Game", "Images", "preview.png")
)
if not os.path.isfile(preview_path):
    fail("Required raw preview sidecar is absent: " + preview_path)
if os.path.getsize(preview_path) != 749769:
    fail("preview.png length differs from the audited source")
if sha256(preview_path) != (
    "6B4075152BB866EB6B05AB8E24AD68A1138756AA0899681B348FA38F8DE288D3"
):
    fail("preview.png hash differs from the audited source")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    [
        "/Game/_Game/Data/Survival",
        "/Game/_Game/FoodSystem/Food/Data",
        "/Game/_Game/Icons",
        "/Game/Data/CharacterBackground",
        "/Game/UI/CharacterBackground",
        "/Game/UI/Defeat/Struggle",
    ],
    True,
)
registry.wait_for_completion()

physical_paths = {package_file(package) for package in EXPECTED_PACKAGES}
if len(physical_paths) != 19:
    fail("Expected 19 unique package files")
for path in physical_paths:
    if not os.path.isfile(path):
        fail("Migrated package file is absent: " + path)

results = []
for package_name in EXPECTED_PACKAGES:
    physical_path = package_file(package_name)
    before_hash = sha256(physical_path)
    asset = unreal.EditorAssetLibrary.load_asset(package_name)
    if asset is None:
        fail("Asset could not load: " + package_name)

    asset_class = asset.get_class().get_name()
    row = {
        "package": package_name,
        "asset_class": asset_class,
        "file": physical_path,
        "sha256_before_resave": before_hash,
    }

    if package_name in DATA_TABLES:
        if asset_class != "DataTable":
            fail(
                "Unexpected DataTable class for {}: {}".format(
                    package_name, asset_class
                )
            )
        row_struct = asset.get_editor_property("row_struct")
        row_struct_path = object_path(row_struct)
        if not row_struct_path.startswith(
            "/Script/EFProjectSystemsGameplay."
        ):
            fail(
                "Unexpected row struct for {}: {}".format(
                    package_name, row_struct_path
                )
            )
        row["row_struct"] = row_struct_path
        row["row_names"] = [
            str(name) for name in unreal.DataTableFunctionLibrary.get_data_table_row_names(asset)
        ]
    elif package_name == REGISTRY_ASSET:
        if asset_class != "ProjectSurvivalConsumableRegistry":
            fail("Unexpected consumable registry class: " + asset_class)
    elif package_name in WIDGETS:
        if asset_class != "WidgetBlueprint":
            fail("Unexpected widget asset class: " + asset_class)
        # compile_blueprint is a void Python API. Validate the resulting
        # Blueprint status plus the generated-class contract below.
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        blueprint_status = str(asset.get_editor_property("status"))
        normalized_status = "".join(
            character
            for character in blueprint_status.upper()
            if character.isalnum()
        )
        if "UPTODATE" not in normalized_status:
            fail(
                "Blueprint compilation did not reach an up-to-date state for "
                "{}: {}".format(package_name, blueprint_status)
            )
        generated_class_path = (
            package_name
            + "."
            + package_name.rsplit("/", 1)[-1]
            + "_C"
        )
        generated_class = unreal.load_class(None, generated_class_path)
        expected_parent = unreal.load_class(None, WIDGETS[package_name])
        if generated_class is None or expected_parent is None:
            fail("Widget generated/native class could not load: " + package_name)
        if not unreal.MathLibrary.class_is_child_of(
            generated_class, expected_parent
        ):
            fail(
                "Widget {} is not a child of {}".format(
                    generated_class_path, WIDGETS[package_name]
                )
            )
        row["compiled"] = True
        row["blueprint_status"] = blueprint_status
        row["generated_class"] = object_path(generated_class)
        row["expected_native_parent"] = WIDGETS[package_name]
    elif package_name in TEXTURES:
        if asset_class != "Texture2D":
            fail(
                "Unexpected icon class for {}: {}".format(
                    package_name, asset_class
                )
            )

    if not unreal.EditorAssetLibrary.save_asset(
        package_name, only_if_is_dirty=False
    ):
        fail("UE 5.8 save failed: " + package_name)
    row["length"] = os.path.getsize(physical_path)
    row["sha256_after_resave"] = sha256(physical_path)
    results.append(row)

redirectors = []
for path in (
    "/Game/_Game",
    "/Game/Data/CharacterBackground",
    "/Game/UI/CharacterBackground",
    "/Game/UI/Defeat/Struggle",
):
    for asset_data in registry.get_assets_by_path(
        path, recursive=True, include_only_on_disk_assets=True
    ):
        asset_class_path = str(
            getattr(asset_data, "asset_class_path", "")
        )
        if "ObjectRedirector" in asset_class_path:
            redirectors.append(str(asset_data.package_name))
if redirectors:
    fail("Redirectors found in migrated roots: " + repr(sorted(redirectors)))

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(
        datetime.timezone.utc
    ).isoformat(),
    "status": "UE58_LOAD_COMPILE_CONTRACT_RESAVE_PASS",
    "engine_version": engine_version,
    "project": unreal.Paths.get_project_file_path(),
    "package_count": len(results),
    "packages": results,
    "raw_preview": {
        "file": preview_path,
        "length": os.path.getsize(preview_path),
        "sha256": sha256(preview_path),
    },
    "redirectors": [],
    "map_load_requested": False,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFCORE_CONTENT58_RESAVE_PASS: " + EVIDENCE_PATH)
