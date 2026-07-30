import json
import os

import unreal


REDIRECTOR_PATHS = [
    "/Game/_Game/Characters/ACFRangedEnemyBPMale",
    "/Game/_Game/Characters/ACFBaseCompanionBPMale",
]


def main():
    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    results = []
    for path in REDIRECTOR_PATHS:
        exists_before_delete = unreal.EditorAssetLibrary.does_asset_exist(path)
        deleted = False
        if exists_before_delete:
            object_path = path + "." + path.rsplit("/", 1)[-1]
            data = asset_registry.get_asset_by_object_path(object_path)
            if data and str(data.asset_class_path.asset_name) == "ObjectRedirector":
                deleted = unreal.EditorAssetLibrary.delete_asset(path)
        results.append(
            {
                "path": path,
                "existed_after_fixup": exists_before_delete,
                "deleted": deleted,
                # The registry keeps the deleted entry until the commandlet exits.
                # Physical and fresh-registry absence is verified by the caller.
                "exists_final": None,
            }
        )

    output_path = os.path.join(
        unreal.Paths.project_saved_dir(),
        "Migration",
        "Phase5",
        "EnemyCharacterRedirectorFixup.json",
    )
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as handle:
        json.dump({"results": results}, handle, indent=2)

main()
