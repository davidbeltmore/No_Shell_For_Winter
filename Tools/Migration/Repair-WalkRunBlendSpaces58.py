import json
import os

import unreal


BLEND_SPACES = {
    "/Game/_Game/Animations/Locomotion/Ground/BS_MM_WalkRun": {
        500: "/Game/_Game/Animations/Locomotion/Ground/MM_Run_Fwd",
        230: "/Game/_Game/Animations/Locomotion/Ground/MM_Walk_Fwd",
        0: "/Game/_Game/Animations/Locomotion/Ground/MM_Walk_InPlace",
    },
    "/Game/_Game/Animations/Locomotion/Ground/BS_MF_Unarmed_WalkRun": {
        500: "/Game/_Game/Animations/Locomotion/Ground/MF_Run_Fwd",
        230: "/Game/_Game/Animations/Locomotion/Ground/MF_Walk_Fwd",
        0: "/Game/_Game/Animations/Locomotion/Ground/MM_Walk_InPlace",
    },
}


def object_path(asset_path):
    leaf = asset_path.rsplit("/", 1)[-1]
    return f"{asset_path}.{leaf}"


report = {
    "status": "UE58_WALK_RUN_BLENDSPACE_REPAIR_FAIL",
    "assets": [],
}

all_valid = True
for blend_space_path, animation_by_speed in BLEND_SPACES.items():
    blend_space = unreal.load_asset(blend_space_path)
    entry = {
        "path": blend_space_path,
        "loaded": bool(blend_space),
        "samples": [],
        "saved": False,
    }
    if not blend_space:
        all_valid = False
        report["assets"].append(entry)
        continue

    samples = list(blend_space.get_editor_property("sample_data"))
    for sample in samples:
        speed = int(round(sample.get_editor_property("sample_value").x))
        expected_path = animation_by_speed.get(speed)
        animation = unreal.load_asset(expected_path) if expected_path else None
        if not animation:
            all_valid = False
        else:
            sample.set_editor_property("animation", animation)
        entry["samples"].append(
            {
                "speed": speed,
                "expected": object_path(expected_path) if expected_path else None,
                "assigned": animation.get_path_name() if animation else None,
            }
        )

    blend_space.set_editor_property("sample_data", samples)
    blend_space.modify()
    entry["saved"] = unreal.EditorAssetLibrary.save_asset(
        blend_space_path,
        only_if_is_dirty=False,
    )
    if not entry["saved"]:
        all_valid = False
    report["assets"].append(entry)

for entry in report["assets"]:
    asset = unreal.load_asset(entry["path"])
    entry["validated_samples"] = []
    if not asset:
        all_valid = False
        continue
    for sample in asset.get_editor_property("sample_data"):
        animation = sample.get_editor_property("animation")
        entry["validated_samples"].append(
            animation.get_path_name() if animation else None
        )
        if not animation:
            all_valid = False

if all_valid:
    report["status"] = "UE58_WALK_RUN_BLENDSPACE_REPAIR_PASS"

output_path = os.path.join(
    unreal.Paths.project_saved_dir(),
    "Migration",
    "AnimationMigration",
    "20260719",
    "UE58_WalkRunBlendSpaceRepair.json",
)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8") as output_file:
    json.dump(report, output_file, indent=2)

unreal.log(report["status"])
if not all_valid:
    raise RuntimeError(report["status"])
