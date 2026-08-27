"""Compile and validate the EF Clothing Morph V2 benchmark in UE 5.8.

UnderWearPanty is only the integration benchmark.  All fitting behavior lives
in the generic EFClothingMorph compiler/runtime code.  This commandlet script
never saves any input asset and proves that with before/after package hashes.
"""

import datetime
import hashlib
import json
import os
import traceback

import unreal


PROJECT_FILE = os.path.realpath(unreal.Paths.get_project_file_path())
PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
SAVED_DIR = os.path.realpath(os.path.join(PROJECT_DIR, "Saved"))
DEFAULT_RECEIPT_PATH = os.path.join(
    SAVED_DIR,
    "ClothingMorphV2QA",
    "compiler_receipt.json",
)
RECEIPT_PATH = os.path.realpath(
    os.environ.get("EF_CLOTHING_V2_RECEIPT", DEFAULT_RECEIPT_PATH)
)
OUTPUT_ROOT = "/Game/_Generated/EFClothingMorphV2"
PREVIEW_REST_ONLY = os.environ.get(
    "EF_CLOTHING_V2_PREVIEW_REST_ONLY", "0"
).strip().lower() in {"1", "true", "yes"}
INPUT_PACKAGES = {
    "source_garment": "/Game/DazToUnreal/UnderWearPanty/UnderWearPanty",
    "body_surface": "/Game/DazToUnreal/Female/Female",
    "compatibility_reference": "/Game/DazToUnreal/Multiple/Multiple",
}
COMPILE_OPTIONS = {
    "output_root": OUTPUT_ROOT,
    "minimum_clearance_cm": 0.45,
    "maximum_push_cm": 2.5,
    "smoothing_iterations": 4,
    "maximum_influences": 8,
    "transfer_missing_body_morphs": True,
    "compile_body_morph_bindings": True,
    "maximum_transferred_morphs": 96,
    "minimum_transferred_morph_delta_cm": 0.02,
    "morph_clearance_sample_count": 8,
    "maximum_morph_repair_cm": 20.0,
    "morph_pair_grid_resolution": 4,
    "morph_pair_probe_count_per_axis": 3,
    "morph_activation_epsilon": 0.0,
    "copy_body_deformer_to_derived": True,
}
MORPH_PAIR_REQUESTS = (
    ("Body Fitness Mass", "Body Heavy"),
)
if PREVIEW_REST_ONLY:
    # Publish a safe manual-QA milestone quickly. All geometrically relevant
    # body morphs remain monitored, but without a binding any non-zero value is
    # suppressed fail-closed by runtime. Rest pose, skeletal animation, weight
    # transfer and automatic clearance remain fully certified.
    COMPILE_OPTIONS["transfer_missing_body_morphs"] = False
    COMPILE_OPTIONS["compile_body_morph_bindings"] = False
    COMPILE_OPTIONS["maximum_transferred_morphs"] = 0
    MORPH_PAIR_REQUESTS = ()


def _write_receipt(payload):
    receipt_dir = os.path.dirname(RECEIPT_PATH)
    os.makedirs(receipt_dir, exist_ok=True)
    temporary_path = RECEIPT_PATH + ".tmp"
    with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(
            payload,
            handle,
            indent=2,
            ensure_ascii=False,
            sort_keys=True,
            default=str,
        )
        handle.write("\n")
    os.replace(temporary_path, RECEIPT_PATH)


def _get_property(value, name):
    try:
        return value.get_editor_property(name)
    except Exception:
        return getattr(value, name)


def _object_path(value):
    return "" if value is None else str(value.get_path_name())


def _package_name_from_object(value):
    object_path = _object_path(value)
    return object_path.split(".", 1)[0]


def _package_file(package_name):
    if not package_name.startswith("/Game/"):
        raise RuntimeError("Protected package must remain under /Game: " + package_name)
    relative_name = package_name[len("/Game/") :].replace("/", os.sep)
    return os.path.realpath(
        os.path.join(unreal.Paths.project_content_dir(), relative_name + ".uasset")
    )


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def _capture_hashes(protected_files):
    hashes = {}
    for label, path in sorted(protected_files.items()):
        if not os.path.isfile(path):
            raise RuntimeError("Protected package file is absent: {}={}".format(label, path))
        hashes[label] = {
            "file": path,
            "sha256": _sha256(path),
            "size_bytes": os.path.getsize(path),
        }
    return hashes


def _profile_metrics(profile):
    property_names = (
        "compiler_version",
        "source_vertex_count",
        "adjusted_vertex_count",
        "penetrating_vertex_count_before",
        "penetrating_vertex_count_after",
        "minimum_signed_gap_before_cm",
        "minimum_signed_gap_after_cm",
        "excluded_body_surface_triangle_count",
        "transferred_morph_count",
        "remapped_weighted_bone_count",
        "reconciled_split_vertex_count",
        "certified_skin_weight_vertex_count",
        "clearance_validated_morph_count",
        "clearance_repaired_morph_count",
        "minimum_sampled_morph_gap_cm",
        "morph_clearance_sample_count",
        "generated_morph_sample_count",
        "maximum_morph_samples_per_binding",
        "stepped_morph_interval_count",
        "identity_morph_sample_count",
        "certified_morph_pair_count",
        "generated_pair_cell_morph_count",
        "pair_body_probe_count",
        "pair_offset_evaluation_count",
        "minimum_sampled_pair_gap_cm",
        "compiled_morph_activation_epsilon",
        "compiled_minimum_clearance_cm",
        "compiled_clearance_reserve_cm",
        "compiled_max_push_cm",
        "compiled_maximum_morph_repair_cm",
        "compiled_maximum_morph_displacement_cm",
        "certified_clearance_multiplier_min",
        "certified_clearance_multiplier_max",
        "certified_clearance_tier_count",
        "minimum_certified_offset_gap_cm",
        "compiled_morph_threshold_position_cm",
        "post_threshold_altered_delta_count",
        "compiled_concurrent_bounds_expansion_cm",
        "compiled_concurrent_sphere_expansion_cm",
    )
    metrics = {name: _get_property(profile, name) for name in property_names}
    metrics["morph_binding_count"] = len(_get_property(profile, "morph_bindings"))
    metrics["morph_pair_certificate_count"] = len(
        _get_property(profile, "morph_pair_certificates")
    )
    metrics["monitored_body_morph_names"] = [
        str(value) for value in _get_property(profile, "monitored_body_morph_names")
    ]
    metrics["required_weighted_bones"] = [
        str(value) for value in _get_property(profile, "required_weighted_bones")
    ]
    metrics["excluded_body_surface_material_slots"] = [
        str(value)
        for value in _get_property(profile, "excluded_body_surface_material_slots")
    ]
    metrics["excluded_body_bone_branches"] = [
        str(value) for value in _get_property(profile, "excluded_body_bone_branches")
    ]
    metrics["excluded_body_morph_prefixes"] = [
        str(value) for value in _get_property(profile, "excluded_body_morph_prefixes")
    ]
    build_guid = _get_property(profile, "build_guid")
    try:
        metrics["build_guid"] = str(build_guid.to_string())
    except Exception:
        try:
            metrics["build_guid"] = str(build_guid.export_text())
        except Exception:
            metrics["build_guid"] = str(build_guid)
    return metrics


def _make_payload():
    return {
        "schema_version": 5,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_EF_CLOTHING_MORPH_V2_COMPILE_FAIL",
        "success": False,
        "project": PROJECT_FILE,
        "engine_version": str(unreal.SystemLibrary.get_engine_version()),
        "inputs": dict(INPUT_PACKAGES),
        "output_root": OUTPUT_ROOT,
        "profile_mode": "preview_rest_only" if PREVIEW_REST_ONLY else "full_catalog",
        "settings": {
            **COMPILE_OPTIONS,
            "morph_pair_requests": [list(pair) for pair in MORPH_PAIR_REQUESTS],
        },
        "protected_sha256_before": {},
        "protected_sha256_after": {},
        "protected_inputs_unchanged": False,
        "compile_success": False,
        "validation_success": False,
        "compile_report": "",
        "validation_report": "",
        "outputs": {},
        "errors": [],
    }


def _run_compile(payload):
    if os.path.basename(PROJECT_FILE).lower() != "noshellforwinter.uproject":
        raise RuntimeError("This compiler harness is restricted to NoShellForWinter.uproject.")
    if not payload["engine_version"].startswith("5.8."):
        raise RuntimeError("Expected UE 5.8, got " + payload["engine_version"])
    if os.path.commonpath((RECEIPT_PATH, SAVED_DIR)).lower() != SAVED_DIR.lower():
        raise RuntimeError("Compiler receipt must remain under the target project's Saved directory.")

    meshes = {
        label: unreal.EditorAssetLibrary.load_asset(package)
        for label, package in INPUT_PACKAGES.items()
    }
    for label, mesh in meshes.items():
        if mesh is None or not isinstance(mesh, unreal.SkeletalMesh):
            raise RuntimeError("Could not load protected SkeletalMesh {}: {}".format(label, INPUT_PACKAGES[label]))

    shared_skeleton = _get_property(meshes["source_garment"], "skeleton")
    if shared_skeleton is None:
        raise RuntimeError("Source garment has no USkeleton.")
    for label, mesh in meshes.items():
        if _get_property(mesh, "skeleton") != shared_skeleton:
            raise RuntimeError(
                "Input {} does not reference the exact shared USkeleton object.".format(label)
            )
    payload["shared_skeleton"] = _object_path(shared_skeleton)

    protected_packages = dict(INPUT_PACKAGES)
    protected_packages["shared_skeleton"] = _package_name_from_object(shared_skeleton)
    protected_files = {
        label: _package_file(package)
        for label, package in protected_packages.items()
    }
    payload["protected_packages"] = protected_packages
    payload["protected_sha256_before"] = _capture_hashes(protected_files)

    options = unreal.EFClothingFitCompileOptions()
    for name, value in COMPILE_OPTIONS.items():
        options.set_editor_property(name, value)
    pair_requests = []
    for first_body_morph, second_body_morph in MORPH_PAIR_REQUESTS:
        request = unreal.EFClothingMorphPairCompileRequest()
        request.set_editor_property("first_body_morph", first_body_morph)
        request.set_editor_property("second_body_morph", second_body_morph)
        pair_requests.append(request)
    options.set_editor_property("morph_pair_requests", pair_requests)

    try:
        result = unreal.EFClothingFitCompilerLibrary.compile_fit_profile(
            meshes["source_garment"],
            meshes["body_surface"],
            meshes["compatibility_reference"],
            options,
        )
        compile_success = bool(_get_property(result, "success"))
        compile_report = str(_get_property(result, "report"))
        profile = _get_property(result, "profile")
        derived = _get_property(result, "derived_garment")

        payload["compile_success"] = compile_success
        payload["compile_report"] = compile_report
        payload["outputs"] = {
            "derived_garment": _object_path(derived),
            "profile": _object_path(profile),
        }

        if not compile_success:
            raise RuntimeError("Native compiler returned failure: " + compile_report)
        if profile is None or derived is None:
            raise RuntimeError("Native compiler reported success without both generated assets.")

        validation_result = (
            unreal.EFClothingFitCompilerLibrary.validate_compiled_profile_detailed(
                profile
            )
        )
        validation_success = bool(_get_property(validation_result, "success"))
        validation_report = str(_get_property(validation_result, "report"))
        payload["validation_reflection"] = {
            "type": type(validation_result).__name__,
            "contract": "FEFClothingFitValidationResult",
        }
        payload["validation_success"] = bool(validation_success)
        payload["validation_report"] = str(validation_report)
        payload["metrics"] = _profile_metrics(profile)
        if not payload["validation_success"]:
            raise RuntimeError(
                "Generated profile validation failed: " + payload["validation_report"]
            )
    except Exception:
        # Preserve the compiler/validator exception as the process failure.
        # A secondary hash-capture problem is evidence, never a replacement
        # for the original error.
        try:
            payload["protected_sha256_after"] = _capture_hashes(protected_files)
            payload["protected_inputs_unchanged"] = (
                payload["protected_sha256_before"]
                == payload["protected_sha256_after"]
            )
        except Exception as integrity_error:
            payload["integrity_capture_error"] = repr(integrity_error)
        raise

    payload["protected_sha256_after"] = _capture_hashes(protected_files)
    payload["protected_inputs_unchanged"] = (
        payload["protected_sha256_before"] == payload["protected_sha256_after"]
    )

    if not payload["protected_inputs_unchanged"]:
        raise RuntimeError("A protected input package changed during compilation.")


def main():
    payload = _make_payload()
    try:
        raise RuntimeError(
            "This single-garment compiler is retired because it can invalidate the atomic "
            "Clothing Director registry. Run Compile-EFClothingGarmentCatalog58.ps1."
        )
        _run_compile(payload)
        payload["success"] = True
        payload["status"] = "UE58_EF_CLOTHING_MORPH_V2_COMPILE_PASS"
        _write_receipt(payload)
        unreal.log("EF_CLOTHING_MORPH_V2_COMPILER_RECEIPT=" + RECEIPT_PATH)
        unreal.log(
            "EF_CLOTHING_MORPH_V2_COMPILER_RESULT="
            + json.dumps(payload, sort_keys=True, default=str)
        )
    except Exception as exc:
        payload["errors"].append(str(exc))
        payload["traceback"] = traceback.format_exc()
        try:
            _write_receipt(payload)
        except Exception as receipt_error:
            unreal.log_error(
                "EF Clothing Morph V2 could not write its failure receipt: "
                + repr(receipt_error)
            )
        unreal.log_error(
            "EF Clothing Morph V2 compiler failed: "
            + json.dumps(payload, sort_keys=True, default=str)
        )
        raise


# PythonScriptCommandlet maps an unhandled Python error to a non-zero process
# exit code and exits naturally with zero on success.  Never call quit_editor().
main()
