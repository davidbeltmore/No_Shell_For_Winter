"""Read-only UE 5.8 probe for protected Daz skeletal-mesh metadata."""

import datetime
import json
import os

import unreal


OFFICIAL_DEFORMER = (
    "/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth."
    "DG_DualQuatSkin_Morph_Cloth"
)
PROJECT_COLLECTION = (
    "/Game/DazToUnreal/Deformers/MDC_DazDualQuatMorph."
    "MDC_DazDualQuatMorph"
)
MESHES = (
    "/Game/DazToUnreal/Female/Female.Female",
    "/Game/DazToUnreal/Multiple/Multiple.Multiple",
    "/Game/DazToUnreal/Male/Male.Male",
)
EVIDENCE_PATH = os.path.realpath(
    os.path.join(
        unreal.Paths.project_dir(),
        "Saved",
        "Migration",
        "Phase3",
        "EFCharacterCreationDazBridgeReadOnlyProbe58.json",
    )
)


def fail(message):
    unreal.log_error("CODEX_DAZ_BRIDGE_PROBE_FAIL: " + message)
    raise RuntimeError(message)


def object_path(value):
    return value.get_path_name() if value is not None else ""


def optional_property(value, *names):
    for name in names:
        try:
            return value.get_editor_property(name)
        except Exception:
            pass
    return None


engine_version = unreal.SystemLibrary.get_engine_version()
if not engine_version.startswith("5.8."):
    fail("Expected UE 5.8 but found " + engine_version)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/DazToUnreal", "/DeformerGraph"], True)
registry.wait_for_completion()

official_deformer = unreal.EditorAssetLibrary.load_asset(OFFICIAL_DEFORMER)
if official_deformer is None:
    fail("Official UE 5.8 DeformerGraph asset did not load: " + OFFICIAL_DEFORMER)
if unreal.EditorAssetLibrary.does_asset_exist(PROJECT_COLLECTION):
    fail("Protected target unexpectedly contains project Daz collection: " + PROJECT_COLLECTION)

records = []
for mesh_path in MESHES:
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        fail("Protected skeletal mesh did not load: " + mesh_path)
    if mesh.get_class().get_name() != "SkeletalMesh":
        fail("Unexpected class for {}: {}".format(mesh_path, mesh.get_class().get_name()))

    skeleton = optional_property(mesh, "Skeleton", "skeleton")
    physics_asset = optional_property(mesh, "PhysicsAsset", "physics_asset")
    if skeleton is None:
        fail("Skeletal mesh has no skeleton: " + mesh_path)
    if physics_asset is None:
        fail("Skeletal mesh has no physics asset: " + mesh_path)

    post_process = optional_property(
        mesh, "PostProcessAnimBlueprint", "post_process_anim_blueprint"
    )
    default_deformer = optional_property(
        mesh, "DefaultMeshDeformer", "default_mesh_deformer"
    )
    if default_deformer is None and hasattr(mesh, "get_default_mesh_deformer"):
        default_deformer = mesh.get_default_mesh_deformer()
    target_deformers = optional_property(
        mesh, "TargetMeshDeformers", "target_mesh_deformers"
    )
    if target_deformers is None and hasattr(mesh, "get_target_mesh_deformers"):
        target_deformers = mesh.get_target_mesh_deformers()
    records.append(
        {
            "mesh": mesh_path,
            "skeleton": object_path(skeleton),
            "physics_asset": object_path(physics_asset),
            "post_process_anim_blueprint": object_path(post_process),
            "default_mesh_deformer": object_path(default_deformer),
            "target_mesh_deformer_collection": object_path(target_deformers),
        }
    )

payload = {
    "schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "READ_ONLY_METADATA_PROBE_PASS",
    "engine_version": engine_version,
    "official_deformer": object_path(official_deformer),
    "project_collection_absent": True,
    "assets_saved": False,
    "meshes": records,
}
os.makedirs(os.path.dirname(EVIDENCE_PATH), exist_ok=True)
with open(EVIDENCE_PATH, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")

unreal.log("CODEX_DAZ_BRIDGE_PROBE_PASS: " + EVIDENCE_PATH)
