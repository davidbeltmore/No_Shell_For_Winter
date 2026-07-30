"""Import the audited character-background PNG as a packaged UE 5.8 Texture2D."""

import datetime
import hashlib
import json
import os

import unreal


PROJECT_ROOT = os.path.realpath(unreal.Paths.project_dir())
SOURCE_PNG = os.path.realpath(
    os.path.join(PROJECT_ROOT, "Content", "_Game", "Images", "preview.png")
)
DESTINATION_PATH = "/Game/_Game/Images"
ASSET_NAME = "T_ProjectCharacterBackgroundPreview"
PACKAGE_NAME = DESTINATION_PATH + "/" + ASSET_NAME
OBJECT_PATH = PACKAGE_NAME + "." + ASSET_NAME
EVIDENCE_PATH = os.path.join(
    PROJECT_ROOT,
    "Saved",
    "Migration",
    "Phase4",
    "CharacterBackgroundPreviewImport58.json",
)
EXPECTED_LENGTH = 749769
EXPECTED_SHA256 = (
    "6B4075152BB866EB6B05AB8E24AD68A1138756AA0899681B348FA38F8DE288D3"
)


def fail(message):
    unreal.log_error("CODEX_BACKGROUND_PREVIEW58_FAIL: " + message)
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
if not os.path.isfile(SOURCE_PNG):
    fail("Audited PNG is absent: " + SOURCE_PNG)
if os.path.getsize(SOURCE_PNG) != EXPECTED_LENGTH:
    fail("Audited PNG length changed")
if sha256(SOURCE_PNG) != EXPECTED_SHA256:
    fail("Audited PNG hash changed")
if unreal.EditorAssetLibrary.does_asset_exist(OBJECT_PATH):
    fail("Refusing to replace pre-existing asset: " + OBJECT_PATH)

task = unreal.AssetImportTask()
task.set_editor_property("filename", SOURCE_PNG)
task.set_editor_property("destination_path", DESTINATION_PATH)
task.set_editor_property("destination_name", ASSET_NAME)
task.set_editor_property("automated", True)
task.set_editor_property("replace_existing", False)
task.set_editor_property("replace_existing_settings", False)
task.set_editor_property("save", True)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
imported_paths = [str(path) for path in task.get_editor_property("imported_object_paths")]
if imported_paths != [OBJECT_PATH]:
    fail("Unexpected imported object set: " + repr(imported_paths))

asset = unreal.EditorAssetLibrary.load_asset(OBJECT_PATH)
if asset is None or asset.get_class().get_name() != "Texture2D":
    fail("Imported object is not a Texture2D: " + OBJECT_PATH)
if not unreal.EditorAssetLibrary.save_asset(OBJECT_PATH, only_if_is_dirty=False):
    fail("Texture2D save failed: " + OBJECT_PATH)

physical_path = os.path.realpath(
    os.path.join(
        unreal.Paths.project_content_dir(),
        "_Game",
        "Images",
        ASSET_NAME + ".uasset",
    )
)
if not os.path.isfile(physical_path):
    fail("Imported package file is absent: " + physical_path)

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_ASSETTOOLS_TEXTURE_IMPORT_PASS",
    "engine_version": engine_version,
    "source_png": {
        "file": SOURCE_PNG,
        "length": os.path.getsize(SOURCE_PNG),
        "sha256": sha256(SOURCE_PNG),
    },
    "texture": {
        "package": PACKAGE_NAME,
        "object": OBJECT_PATH,
        "class": asset.get_class().get_name(),
        "file": physical_path,
        "length": os.path.getsize(physical_path),
        "sha256": sha256(physical_path),
    },
    "replace_existing": False,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_BACKGROUND_PREVIEW58_PASS: " + EVIDENCE_PATH)
