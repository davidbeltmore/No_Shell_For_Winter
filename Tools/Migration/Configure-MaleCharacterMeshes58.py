"""Assign the authoritative UE 5.8 Male mesh to every organized Male character BP."""

import json
import os
from pathlib import Path

import unreal


MALE_MESH_PATH = "/Game/DazToUnreal/Male/Male.Male"
MALE_BLUEPRINT_PATHS = (
    "/Game/_Game/Characters/Male/ACFBaseCompanionBPMale",
    "/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale",
    "/Game/_Game/Characters/Male/ACFRangedCompanionBPMale",
    "/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale",
    "/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale",
    "/Game/_Game/Characters/Male/ACFDummyEnemyBPMale",
    "/Game/_Game/Characters/Male/ACFGunEnemyBPMale",
    "/Game/_Game/Characters/Male/ACFMageEnemyBPMale",
    "/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale",
    "/Game/_Game/Characters/Male/ACFMMEnemyBPMale",
    "/Game/_Game/Characters/Male/ACFRangedEnemyBPMale",
)
OUTPUT_PATH = Path(
    os.environ.get(
        "CODEX_MALE_MESH_RESULT",
        str(
            Path(unreal.Paths.project_dir())
            / "Saved"
            / "Migration"
            / "Phase5"
            / "Assets"
            / "ConfigureMaleCharacterMeshes58.json"
        ),
    )
)


def object_path(value):
    return value.get_path_name() if value else ""


def get_component_mesh(component):
    try:
        return component.get_skeletal_mesh_asset()
    except Exception:
        return component.get_editor_property("skeletal_mesh_asset")


def configure_blueprint(asset_path, male_mesh):
    blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not blueprint:
        raise RuntimeError(f"Unable to load {asset_path}")
    generated_class = blueprint.generated_class()
    if not generated_class:
        raise RuntimeError(f"Generated class is unavailable for {asset_path}")
    cdo = unreal.get_default_object(generated_class)
    mesh_components = list(cdo.get_components_by_class(unreal.SkeletalMeshComponent))
    primary = next(
        (component for component in mesh_components if component.get_name() == "CharacterMesh0"),
        mesh_components[0] if mesh_components else None,
    )
    if not primary:
        raise RuntimeError(f"No SkeletalMeshComponent exists on {asset_path}")

    before = object_path(get_component_mesh(primary))
    primary.set_skeletal_mesh(male_mesh)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False):
        raise RuntimeError(f"Unable to save {asset_path}")

    generated_class = blueprint.generated_class()
    cdo = unreal.get_default_object(generated_class)
    primary = next(
        (
            component
            for component in cdo.get_components_by_class(unreal.SkeletalMeshComponent)
            if component.get_name() == "CharacterMesh0"
        ),
        None,
    )
    after = object_path(get_component_mesh(primary)) if primary else ""
    if after != MALE_MESH_PATH:
        raise RuntimeError(f"Mesh assignment did not persist in memory for {asset_path}: {after}")
    return {"asset": asset_path, "component": primary.get_name(), "before": before, "after": after}


def main():
    male_mesh = unreal.EditorAssetLibrary.load_asset(MALE_MESH_PATH)
    if not male_mesh:
        raise RuntimeError(f"Unable to load authoritative Male mesh {MALE_MESH_PATH}")
    rows = [configure_blueprint(asset_path, male_mesh) for asset_path in MALE_BLUEPRINT_PATHS]
    payload = {
        "success": len(rows) == len(MALE_BLUEPRINT_PATHS),
        "male_mesh": MALE_MESH_PATH,
        "count": len(rows),
        "characters": rows,
    }
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    unreal.log(f"[ConfigureMaleCharacterMeshes58] PASS count={len(rows)} output={OUTPUT_PATH}")


main()
