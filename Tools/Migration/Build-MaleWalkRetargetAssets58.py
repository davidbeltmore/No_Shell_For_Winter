"""Build and verify the project-owned Male animation set used by Walk N."""

import datetime
import hashlib
import json
import os
import traceback

import unreal


PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
EVIDENCE = os.path.realpath(
    os.environ.get(
        "CODEX_MALE_WALK_RETARGET_EVIDENCE",
        os.path.join(
            PROJECT_DIR,
            "Saved",
            "Migration",
            "AnimationMigration",
            "20260719",
            "UE58_MaleWalkRetargetBuild.json",
        ),
    )
)
MALE_MESH = "/Game/DazToUnreal/Male/Male.Male"
SHARED_SKELETON = (
    "/Game/FullSample/GASP/UEFN_Mannequin/Meshes/"
    "SK_UEFN_Mannequin.SK_UEFN_Mannequin"
)
EXPECTED = (
    "/Game/_Game/Animations/Retarget/Male/WalkN/Anim_KA_Walk04_Male",
    "/Game/_Game/Animations/Retarget/Male/WalkN/Anim_KA_Walk04_Pivot_Male",
    "/Game/_Game/Animations/Retarget/Male/WalkN/Anim_KA_Idle01_breathing_Male",
)
SOURCE = (
    "/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04",
    "/Game/_Game/Animations/Locomotion/Ground/Anim_KA_Walk04_Pivot",
    "/Game/_Game/Animations/Emotes/General/Anim_KA_Idle01_breathing",
)


def package_file(package):
    return os.path.join(
        unreal.Paths.project_content_dir(),
        package[len("/Game/") :].replace("/", os.sep) + ".uasset",
    )


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "UE58_MALE_WALK_RETARGET_BUILD_FAIL",
    "project": os.path.realpath(unreal.Paths.get_project_file_path()),
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "source_assets": list(SOURCE),
    "expected_outputs": list(EXPECTED),
    "errors": [],
}

try:
    if os.path.basename(payload["project"]).lower() != "noshellforwinter.uproject":
        raise RuntimeError("This builder is restricted to NoShellForWinter.uproject")
    if not payload["engine_version"].startswith("5.8."):
        raise RuntimeError("Expected UE 5.8")

    payload["source_hashes_before"] = {
        package: sha256(package_file(package)) for package in SOURCE
    }

    raw_result = (
        unreal.ProjectAnimationDiagnosticsLibrary.build_male_walk_retarget_assets()
    )
    build_result = json.loads(str(raw_result))
    payload["build_result"] = build_result
    if not build_result.get("success"):
        raise RuntimeError(build_result.get("error", "Native Male retarget build failed"))

    validations = []
    for package in EXPECTED:
        animation = unreal.load_asset(package)
        if animation is None or animation.get_class().get_name() != "AnimSequence":
            raise RuntimeError("Missing Male AnimSequence: " + package)
        snapshot = json.loads(
            str(
                unreal.ProjectAnimationDiagnosticsLibrary.snapshot_anim_sequence_asset(
                    animation
                )
            )
        )
        row = {
            "package": package,
            "class": animation.get_class().get_name(),
            "skeleton": snapshot.get("skeleton", ""),
            "preview_mesh_for_asset": snapshot.get("preview_mesh_for_asset", ""),
            "play_length": snapshot.get("play_length", 0.0),
            "track_count": snapshot.get("track_count", 0),
            "enable_root_motion": snapshot.get("enable_root_motion", False),
            "force_root_lock": snapshot.get("force_root_lock", False),
            "root_motion_root_lock": snapshot.get("root_motion_root_lock", ""),
            "file_sha256": sha256(package_file(package)),
        }
        row["pass"] = (
            row["skeleton"] == SHARED_SKELETON
            and row["preview_mesh_for_asset"] == MALE_MESH
            and row["track_count"] > 0
        )
        validations.append(row)

    payload["validations"] = validations
    payload["source_hashes_after"] = {
        package: sha256(package_file(package)) for package in SOURCE
    }
    payload["source_assets_unchanged"] = (
        payload["source_hashes_before"] == payload["source_hashes_after"]
    )
    if not payload["source_assets_unchanged"]:
        raise RuntimeError("The Female/source Walk N assets changed during retarget")
    if not all(row["pass"] for row in validations):
        raise RuntimeError("One or more Male retarget outputs failed validation")

    payload["status"] = "UE58_MALE_WALK_RETARGET_BUILD_PASS"
except Exception as exc:
    payload["errors"].append(str(exc))
    payload["traceback"] = traceback.format_exc()
finally:
    os.makedirs(os.path.dirname(EVIDENCE), exist_ok=True)
    with open(EVIDENCE, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False, sort_keys=True)
        handle.write("\n")
    unreal.log("[MaleWalkRetargetBuild58] " + payload["status"])
    unreal.log("[MaleWalkRetargetBuild58] evidence=" + EVIDENCE)
    unreal.SystemLibrary.quit_editor()
