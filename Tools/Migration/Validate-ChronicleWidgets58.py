"""Compile the project-owned Chronicle Widget Blueprints without saving them."""

import json
import os
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
ALLOW_LIST = PROJECT_DIR / "Docs" / "Migration" / "Evidence" / "Chronicle_WBP_AllowList.txt"
REPORT = Path(
    os.environ.get(
        "CODEX_CHRONICLE_WBP_REPORT",
        PROJECT_DIR / "Saved" / "Migration" / "Phase5" / "ChronicleWBPCompile.json",
    )
)


def fail(message):
    unreal.log_error("CODEX_CHRONICLE_WBP_FAIL: " + message)
    raise RuntimeError(message)


if not ALLOW_LIST.is_file():
    fail("Allow-list is missing: " + str(ALLOW_LIST))

asset_paths = [
    line.strip()
    for line in ALLOW_LIST.read_text(encoding="utf-8").splitlines()
    if line.strip() and not line.lstrip().startswith("#")
]
if not asset_paths:
    fail("Allow-list is empty")

compiled = []
for object_path in asset_paths:
    asset = unreal.EditorAssetLibrary.load_asset(object_path)
    if asset is None:
        fail("Widget Blueprint did not load: " + object_path)
    if asset.get_class().get_name() != "WidgetBlueprint":
        fail("Asset is not a WidgetBlueprint: " + object_path)
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    status = str(asset.get_editor_property("status"))
    normalized_status = "".join(character for character in status.upper() if character.isalnum())
    if "UPTODATE" not in normalized_status:
        fail("Compile status is {} for {}".format(status, object_path))
    compiled.append({"asset": object_path, "status": status})

payload = {
    "schema_version": 1,
    "status": "PASS",
    "compiled_widget_count": len(compiled),
    "compiled_widgets": compiled,
    "saved_assets": False,
}
REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
unreal.log("CODEX_CHRONICLE_WBP_PASS: " + str(REPORT))
