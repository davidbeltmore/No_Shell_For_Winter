"""Compile the V4 clothing fixture Blueprints without saving any asset."""

import json
import os
from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir()).resolve()
REPORT = Path(
    os.environ.get(
        "CODEX_EF_CLOTHING_V4_BP_REPORT",
        PROJECT_DIR / "Saved" / "ClothingMorphV4QA" / "BlueprintCompile.json",
    )
).resolve()

BLUEPRINTS = (
    "/Game/FullSample/Player.Player",
    "/Game/_Game/Clothes/UnderWear01/Bra.Bra",
    "/Game/_Game/Clothes/UnderWear01/Panty.Panty",
    "/Game/_Game/Clothes/Bikini01/Bikini.Bikini",
    "/Game/_Game/Clothes/Rags01/RagShirt.RagShirt",
    "/Game/_Game/Clothes/Rags01/RagPants.RagPants",
    "/Game/_Game/Clothes/Rags01/RagFootWrap.RagFootWrap",
)


def fail(message):
    unreal.log_error("EF_CLOTHING_MORPH_V4_BLUEPRINT_FAIL: " + message)
    raise RuntimeError(message)


def load_blueprint(asset_path):
    """Resolve either a Blueprint asset or the generated class UE may return."""
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if isinstance(asset, unreal.Blueprint):
        return asset

    package_path = asset_path.rsplit(".", 1)[0]
    generated_class = unreal.EditorAssetLibrary.load_blueprint_class(package_path)
    if generated_class is not None:
        try:
            generated_by = generated_class.get_editor_property("class_generated_by")
            if isinstance(generated_by, unreal.Blueprint):
                return generated_by
        except Exception:
            pass

    actual_class = (
        asset.get_class().get_path_name() if asset is not None else "None"
    )
    fail(
        "Asset is not a resolvable Blueprint: {} (loaded class: {})".format(
            asset_path, actual_class
        )
    )


compiled = []
for asset_path in BLUEPRINTS:
    blueprint = load_blueprint(asset_path)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    status = str(blueprint.get_editor_property("status"))
    normalized = "".join(character for character in status.upper() if character.isalnum())
    if "UPTODATE" not in normalized:
        fail("Compile status is {} for {}".format(status, asset_path))
    compiled.append({"asset": asset_path, "status": status})

payload = {
    "schema_version": 1,
    "status": "UE58_EF_CLOTHING_MORPH_V4_BLUEPRINT_COMPILE_PASS",
    "compiled_blueprint_count": len(compiled),
    "compiled_blueprints": compiled,
    "saved_assets": False,
}
REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
unreal.log("EF_CLOTHING_MORPH_V4_BLUEPRINT_PASS: " + str(REPORT))
