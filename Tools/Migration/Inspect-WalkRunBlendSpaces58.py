import json
import os

import unreal


ASSETS = [
    "/Game/_Game/Animations/Locomotion/Ground/BS_MM_WalkRun",
    "/Game/_Game/Animations/Locomotion/Ground/BS_MF_Unarmed_WalkRun",
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/Animations/Manny/BS_MM_WalkRun",
    "/Game/KawaiiAnimations/Demo/Characters/Mannequins/Animations/Quinn/BS_MF_Unarmed_WalkRun",
]


def read_sample(sample):
    animation = sample.get_editor_property("animation")
    sample_value = sample.get_editor_property("sample_value")
    return {
        "animation": animation.get_path_name() if animation else None,
        "sample_value": {
            "x": sample_value.x,
            "y": sample_value.y,
            "z": sample_value.z,
        },
        "rate_scale": sample.get_editor_property("rate_scale"),
    }


report = {"assets": []}
for path in ASSETS:
    asset = unreal.load_asset(path)
    entry = {"path": path, "loaded": bool(asset)}
    if asset:
        try:
            entry["samples"] = [
                read_sample(sample)
                for sample in asset.get_editor_property("sample_data")
            ]
        except Exception as exc:
            entry["sample_error"] = str(exc)
    report["assets"].append(entry)

output_path = os.path.join(
    unreal.Paths.project_saved_dir(),
    "Migration",
    "AnimationMigration",
    "20260719",
    "UE58_WalkRunBlendSpaceInspection.json",
)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8") as output_file:
    json.dump(report, output_file, indent=2)

unreal.log("UE58_WALK_RUN_BLENDSPACE_INSPECTION_COMPLETE")
