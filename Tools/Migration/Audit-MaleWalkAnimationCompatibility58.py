"""Read-only UE 5.8 audit of the assets used by custom Walk (N).

This script intentionally performs no saves. It records the actual Skeleton,
preview mesh, and diagnostic snapshots for Female, Male, and the three Walk N
animation sequences, then exits the editor process.
"""

import datetime
import json
import os
import traceback

import unreal


PROJECT = os.path.realpath(unreal.Paths.get_project_file_path())
PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
OUTPUT = os.path.realpath(
    os.environ.get(
        "CODEX_MALE_WALK_AUDIT_EVIDENCE",
        os.path.join(
            PROJECT_DIR,
            "Saved",
            "Migration",
            "AnimationMigration",
            "20260719",
            "UE58_MaleWalkAnimationCompatibilityAudit.json",
        ),
    )
)

MESHES = {
    "female": "/Game/DazToUnreal/Female/Female",
    "male": "/Game/DazToUnreal/Male/Male",
}
ANIMATIONS = {
    "walk": "/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04",
    "pivot": "/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04_Pivot",
    "idle": "/Game/_Game/Animations/Emotes/General/Anim_KA_Idle01_breathing",
}


def object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def editor_property(value, property_name):
    try:
        return value.get_editor_property(property_name)
    except Exception:
        return None


def reference_skeleton_row(skeleton):
    if skeleton is None:
        return {"bone_count": 0, "bones": []}
    try:
        reference = skeleton.get_reference_skeleton()
        bone_count = int(reference.get_num())
        bones = [str(reference.get_bone_name(index)) for index in range(bone_count)]
        return {"bone_count": bone_count, "bones": bones}
    except Exception as exc:
        return {"bone_count": -1, "bones": [], "error": repr(exc)}


def mesh_row(package):
    mesh = unreal.load_asset(package)
    if mesh is None:
        raise RuntimeError("Unable to load mesh: " + package)
    skeleton = editor_property(mesh, "skeleton")
    row = {
        "package": package,
        "object": object_path(mesh),
        "class": mesh.get_class().get_name(),
        "skeleton": object_path(skeleton),
    }
    row.update(reference_skeleton_row(skeleton))
    return row


def animation_row(package):
    animation = unreal.load_asset(package)
    if animation is None:
        raise RuntimeError("Unable to load animation: " + package)
    skeleton = editor_property(animation, "skeleton")
    preview_mesh = editor_property(animation, "preview_skeletal_mesh")
    row = {
        "package": package,
        "object": object_path(animation),
        "class": animation.get_class().get_name(),
        "skeleton": object_path(skeleton),
        "preview_skeletal_mesh": object_path(preview_mesh),
    }
    row.update(reference_skeleton_row(skeleton))
    try:
        snapshot = unreal.ProjectAnimationDiagnosticsLibrary.snapshot_anim_sequence_asset(
            animation
        )
        row["native_snapshot"] = json.loads(str(snapshot))
    except Exception as exc:
        row["native_snapshot_error"] = repr(exc)
    return row


payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_MALE_WALK_ANIMATION_COMPATIBILITY_AUDIT_FAIL",
    "project": PROJECT,
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "mutated_assets": [],
    "saved_assets": [],
    "meshes": {},
    "animations": {},
    "errors": [],
}

try:
    if os.path.basename(PROJECT).lower() != "noshellforwinter.uproject":
        raise RuntimeError("Audit is restricted to NoShellForWinter.uproject")
    if not payload["engine_version"].startswith("5.8."):
        raise RuntimeError("Expected UE 5.8: " + payload["engine_version"])

    payload["meshes"] = {
        label: mesh_row(package) for label, package in MESHES.items()
    }
    payload["animations"] = {
        label: animation_row(package) for label, package in ANIMATIONS.items()
    }

    female_skeleton = payload["meshes"]["female"]["skeleton"]
    male_skeleton = payload["meshes"]["male"]["skeleton"]
    animation_skeletons = sorted(
        {row["skeleton"] for row in payload["animations"].values()}
    )
    payload["compatibility"] = {
        "female_and_male_share_skeleton": bool(
            female_skeleton and female_skeleton == male_skeleton
        ),
        "animation_skeletons": animation_skeletons,
        "all_walk_assets_match_female": animation_skeletons == [female_skeleton],
        "all_walk_assets_match_male": animation_skeletons == [male_skeleton],
    }
    payload["status"] = "UE58_MALE_WALK_ANIMATION_COMPATIBILITY_AUDIT_PASS"
except Exception as exc:
    payload["errors"].append(str(exc))
    payload["traceback"] = traceback.format_exc()
finally:
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False, sort_keys=True)
        handle.write("\n")
    unreal.log("[MaleWalkAnimationCompatibilityAudit58] " + payload["status"])
    unreal.log("[MaleWalkAnimationCompatibilityAudit58] evidence=" + OUTPUT)
    unreal.SystemLibrary.quit_editor()
