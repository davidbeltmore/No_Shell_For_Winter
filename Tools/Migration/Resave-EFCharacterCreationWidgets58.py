"""Load, contract-check, and resave migrated EFCharacterCreation widgets in UE 5.8."""

import datetime
import hashlib
import json
import os

import unreal


TARGET_ROOT = os.path.realpath(unreal.Paths.project_dir())
CONTENT_ROOT = os.path.realpath(
    os.path.join(TARGET_ROOT, "Plugins", "EFCharacterCreation", "Content")
)
EVIDENCE_PATH = os.path.realpath(
    os.path.join(
        TARGET_ROOT,
        "Saved",
        "Migration",
        "Phase3",
        "EFCharacterCreationWidgets58Resave.json",
    )
)
ASSETS = (
    (
        "/EFCharacterCreation/UI/WBP_EFCharacterCreationRoot",
        "/Script/EFCharacterCreationRuntime.EFCharacterCreationRootWidget",
        "UI/WBP_EFCharacterCreationRoot.uasset",
    ),
    (
        "/EFCharacterCreation/UI/WBP_EFMorphSlider",
        "/Script/EFCharacterCreationRuntime.EFMorphSliderWidget",
        "UI/WBP_EFMorphSlider.uasset",
    ),
)


def fail(message):
    unreal.log_error("CODEX_EFCC_WIDGET_RESAVE_FAIL: " + message)
    raise RuntimeError(message)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/EFCharacterCreation"], True)
registry.wait_for_completion()

physical_packages = []
for root, _directories, files in os.walk(CONTENT_ROOT):
    physical_packages.extend(
        os.path.realpath(os.path.join(root, name))
        for name in files
        if name.lower().endswith((".uasset", ".umap"))
    )
if len(physical_packages) != 2:
    fail("Expected exactly two plugin packages but found " + repr(physical_packages))

results = []
for asset_path, expected_parent_path, relative_file in ASSETS:
    physical_path = os.path.realpath(os.path.join(CONTENT_ROOT, relative_file))
    if physical_path not in physical_packages:
        fail("Expected package file is absent: " + physical_path)

    before_hash = sha256(physical_path)
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        fail("Could not load " + asset_path)
    if asset.get_class().get_name() != "WidgetBlueprint":
        fail(
            "Unexpected asset class for {}: {}".format(
                asset_path, asset.get_class().get_name()
            )
        )

    generated_class = unreal.load_class(None, asset_path + "." + asset_path.rsplit("/", 1)[-1] + "_C")
    if generated_class is None:
        fail("Generated class could not load for " + asset_path)
    expected_parent_class = unreal.load_class(None, expected_parent_path)
    if expected_parent_class is None:
        fail("Expected native parent class could not load: " + expected_parent_path)
    if not unreal.MathLibrary.class_is_child_of(generated_class, expected_parent_class):
        fail(
            "Generated class for {} is not a child of {}".format(
                asset_path, expected_parent_path
            )
        )

    if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
        fail("UE 5.8 save failed for " + asset_path)

    results.append(
        {
            "asset_path": asset_path,
            "asset_class": asset.get_class().get_name(),
            "expected_native_parent_class": expected_parent_path,
            "generated_class": generated_class.get_path_name(),
            "file": physical_path,
            "length": os.path.getsize(physical_path),
            "sha256_before_resave": before_hash,
            "sha256_after_resave": sha256(physical_path),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_LOAD_CONTRACT_RESAVE_PASS",
    "engine_version": engine_version,
    "project": unreal.Paths.get_project_file_path(),
    "packages": results,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_EFCC_WIDGET_RESAVE_PASS: " + EVIDENCE_PATH)
