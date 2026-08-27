"""Compile the configured EF Clothing Morph V26 catalog transactionally.

The native compiler stages every generated mesh/profile/surface binding and
publishes the registry once, only after the complete enabled catalog validates.
This commandlet adds file-hash evidence without containing garment-specific rules.
"""

import datetime
import hashlib
import json
import os
import re
import traceback

import unreal


PROJECT_FILE = os.path.realpath(unreal.Paths.get_project_file_path())
PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
SAVED_DIR = os.path.realpath(os.path.join(PROJECT_DIR, "Saved"))
CATALOG_PATH = os.environ.get(
    "EF_CLOTHING_V26_CATALOG",
    "/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments",
)
COMPATIBILITY_PATH = os.environ.get(
    "EF_CLOTHING_V26_COMPATIBILITY",
    "/Game/DazToUnreal/Multiple/Multiple",
)
RECEIPT_PATH = os.path.realpath(
    os.environ.get(
        "EF_CLOTHING_V26_RECEIPT",
        os.path.join(SAVED_DIR, "ClothingMorphV2QA", "compiler_receipt_FullCatalog_V26.json"),
    )
)
OUTPUT_ROOT = "/Game/_Generated/EFClothingMorphV2"


def _get(value, name):
    try:
        return value.get_editor_property(name)
    except Exception:
        return getattr(value, name)


def _object_path(value):
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def _package_name(value):
    path = _object_path(value)
    return path.split(".", 1)[0]


def _load_asset(path, expected_type=None):
    value = unreal.EditorAssetLibrary.load_asset(path)
    if value is None or (expected_type is not None and not isinstance(value, expected_type)):
        raise RuntimeError("Could not load required asset: " + path)
    return value


def _package_file(package_name):
    if not package_name.startswith("/Game/"):
        raise RuntimeError("Protected package escaped /Game: " + package_name)
    relative = package_name[len("/Game/") :].replace("/", os.sep)
    content = os.path.realpath(unreal.Paths.project_content_dir())
    path = os.path.realpath(os.path.join(content, relative + ".uasset"))
    if os.path.commonpath((path, content)).lower() != content.lower():
        raise RuntimeError("Protected package file escaped project Content: " + path)
    return path


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def _capture_hashes(packages):
    result = {}
    for package in sorted(set(packages)):
        path = _package_file(package)
        if not os.path.isfile(path):
            raise RuntimeError("Protected package file is missing: " + path)
        result[package] = {
            "file": path,
            "size_bytes": os.path.getsize(path),
            "sha256": _sha256(path),
        }
    return result


def _extract_catalog_mesh_packages(catalog):
    packages = set()
    for column_name in ("SourceGarment", "BodySurface"):
        values = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(
            catalog, column_name
        )
        for value in values:
            match = re.search(r"/Game/[A-Za-z0-9_./-]+", str(value))
            if match:
                packages.add(match.group(0).split(".", 1)[0].rstrip("'\""))
    return packages


def _guid(value):
    try:
        return str(value.to_string())
    except Exception:
        return str(value)


def _binding_metrics(binding):
    pairs = list(_get(binding, "lod_pair_bindings"))
    rows = []
    for pair in pairs:
        garment_topology = _get(pair, "garment_topology")
        body_topology = _get(pair, "body_topology")
        metrics = _get(pair, "metrics")
        rows.append(
            {
                "garment_lod": int(_get(garment_topology, "lod_index")),
                "body_lod": int(_get(body_topology, "lod_index")),
                "garment_render_vertices": int(
                    _get(garment_topology, "render_vertex_count")
                ),
                "body_render_vertices": int(
                    _get(body_topology, "render_vertex_count")
                ),
                "garment_topology_fingerprint": str(
                    _get(garment_topology, "topology_fingerprint")
                ),
                "body_topology_fingerprint": str(
                    _get(body_topology, "topology_fingerprint")
                ),
                "bound_render_vertices": int(
                    _get(metrics, "bound_render_vertex_count")
                ),
                "invalid_anchors": int(_get(metrics, "invalid_anchor_count")),
                "surface_follow_vertices": int(
                    _get(metrics, "surface_follow_vertex_count")
                ),
                "hybrid_vertices": int(_get(metrics, "hybrid_vertex_count")),
                "collision_only_vertices": int(
                    _get(metrics, "collision_only_vertex_count")
                ),
                "preserve_upstream_vertices": int(
                    _get(metrics, "preserve_upstream_vertex_count")
                ),
                "candidate_triangles": int(
                    _get(metrics, "candidate_triangle_count")
                ),
                "neighbor_references": int(
                    _get(metrics, "neighbor_reference_count")
                ),
                "witnesses": int(_get(metrics, "witness_count")),
                "excluded_preserve_upstream_garment_triangles": int(
                    _get(
                        metrics,
                        "excluded_preserve_upstream_garment_triangle_count",
                    )
                ),
                "degenerate_body_triangles": int(
                    _get(metrics, "degenerate_body_triangle_count")
                ),
                "excluded_degenerate_body_triangles": int(
                    _get(metrics, "excluded_degenerate_body_triangle_count")
                ),
                "excluded_body_triangles": int(
                    _get(metrics, "excluded_body_triangle_count")
                ),
                "minimum_rest_signed_gap_cm": float(
                    _get(metrics, "minimum_rest_signed_gap_cm")
                ),
                "maximum_initial_correction_cm": float(
                    _get(metrics, "maximum_initial_correction_cm")
                ),
                "maximum_anchor_error_cm": float(
                    _get(metrics, "maximum_anchor_error_cm")
                ),
                "certified": bool(_get(pair, "certified")),
            }
        )
    return rows


def _write_receipt(payload):
    if os.path.commonpath((RECEIPT_PATH, SAVED_DIR)).lower() != SAVED_DIR.lower():
        raise RuntimeError("Compiler receipt must remain under project Saved.")
    os.makedirs(os.path.dirname(RECEIPT_PATH), exist_ok=True)
    temporary = RECEIPT_PATH + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False, sort_keys=True, default=str)
        handle.write("\n")
    os.replace(temporary, RECEIPT_PATH)


def _run(payload):
    if os.path.basename(PROJECT_FILE).lower() != "noshellforwinter.uproject":
        raise RuntimeError("Catalog compiler is restricted to NoShellForWinter.uproject.")
    engine_version = str(unreal.SystemLibrary.get_engine_version())
    if not engine_version.startswith("5.8."):
        raise RuntimeError("Expected UE 5.8, got " + engine_version)
    payload["engine_version"] = engine_version

    catalog = _load_asset(CATALOG_PATH, unreal.DataTable)
    compatibility = _load_asset(COMPATIBILITY_PATH, unreal.SkeletalMesh)
    skeleton = _get(compatibility, "skeleton")
    if skeleton is None:
        raise RuntimeError("Compatibility mesh has no shared USkeleton.")

    protected_packages = _extract_catalog_mesh_packages(catalog)
    protected_packages.update(
        {
            _package_name(catalog),
            _package_name(compatibility),
            _package_name(skeleton),
        }
    )
    # These project invariants are outside garment logic but must remain byte-stable.
    for invariant in (
        "/Game/DazToUnreal/Female/Female",
        "/Game/DazToUnreal/Male/Male",
        "/Game/DazToUnreal/Multiple/Multiple",
        "/Game/FullSample/Player",
    ):
        if unreal.EditorAssetLibrary.does_asset_exist(invariant):
            protected_packages.add(invariant)
    payload["protected_packages"] = sorted(protected_packages)
    payload["protected_sha256_before"] = _capture_hashes(protected_packages)

    options = unreal.EFClothingFitCompileOptions()
    settings = {
        "output_root": OUTPUT_ROOT,
        "minimum_clearance_cm": 0.45,
        "maximum_push_cm": 2.5,
        "smoothing_iterations": 4,
        "maximum_influences": 8,
        "transfer_missing_body_morphs": False,
        "compile_body_morph_bindings": False,
        "maximum_transferred_morphs": 0,
        "minimum_transferred_morph_delta_cm": 0.02,
        "morph_clearance_sample_count": 4,
        "maximum_morph_repair_cm": 5.0,
        "morph_pair_requests": [],
        "morph_pair_grid_resolution": 4,
        "morph_pair_probe_count_per_axis": 3,
        "morph_activation_epsilon": 0.0,
        "copy_body_deformer_to_derived": True,
    }
    for name, value in settings.items():
        options.set_editor_property(name, value)
    payload["settings"] = settings

    result = unreal.EFClothingFitCompilerLibrary.compile_garment_catalog(
        catalog, compatibility, options
    )
    payload["native_report"] = str(_get(result, "report"))
    payload["enabled_row_count"] = int(_get(result, "enabled_row_count"))
    payload["compiled_row_count"] = int(_get(result, "compiled_row_count"))
    payload["surface_wrap_row_count"] = int(_get(result, "surface_wrap_row_count"))
    payload["registry"] = _object_path(_get(result, "registry"))
    payload["rows"] = []
    valid_profile_count = 0
    valid_binding_count = 0
    passed_row_count = 0
    for row in list(_get(result, "rows")):
        profile = _get(row, "profile")
        binding = _get(row, "surface_binding")
        row_payload = {
            "row_name": str(_get(row, "row_name")),
            "success": bool(_get(row, "success")),
            "requires_surface_binding": bool(
                _get(row, "requires_surface_binding")
            ),
            "report": str(_get(row, "report")),
            "derived_garment": _object_path(_get(row, "derived_garment")),
            "profile": _object_path(profile),
            "surface_binding": _object_path(binding),
        }
        if profile is not None:
            validation = unreal.EFClothingFitCompilerLibrary.validate_compiled_profile_detailed(
                profile
            )
            row_payload["validation_success"] = bool(_get(validation, "success"))
            row_payload["validation_report"] = str(_get(validation, "report"))
            row_payload["build_guid"] = _guid(_get(profile, "build_guid"))
            valid_profile_count += int(row_payload["validation_success"])
        else:
            row_payload["validation_success"] = False
            row_payload["validation_report"] = "Profile is null."
        if binding is not None:
            row_payload["binding_lod_pairs"] = _binding_metrics(binding)
            binding_valid = bool(row_payload["binding_lod_pairs"]) and all(
                pair["certified"] and pair["invalid_anchors"] == 0
                for pair in row_payload["binding_lod_pairs"]
            )
            row_payload["binding_valid"] = binding_valid
            valid_binding_count += int(binding_valid)
        else:
            row_payload["binding_lod_pairs"] = []
            row_payload["binding_valid"] = False
        passed_row_count += int(
            row_payload["success"]
            and row_payload["validation_success"]
            and (
                row_payload["binding_valid"]
                if row_payload["requires_surface_binding"]
                else binding is None
            )
        )
        payload["rows"].append(row_payload)

    payload["valid_profile_count"] = valid_profile_count
    payload["valid_binding_count"] = valid_binding_count
    payload["tested_row_count"] = len(payload["rows"])
    payload["passed_row_count"] = passed_row_count
    payload["protected_sha256_after"] = _capture_hashes(protected_packages)
    payload["protected_inputs_unchanged"] = (
        payload["protected_sha256_before"] == payload["protected_sha256_after"]
    )
    expected = payload["enabled_row_count"]
    equality_gate = (
        expected > 0
        and expected == payload["compiled_row_count"]
        and expected == valid_profile_count
        and payload["surface_wrap_row_count"] == valid_binding_count
        and expected == payload["tested_row_count"]
        and expected == passed_row_count
    )
    payload["catalog_equality_gate"] = equality_gate
    if not bool(_get(result, "success")):
        raise RuntimeError("Native catalog compiler failed: " + payload["native_report"])
    if not equality_gate:
        raise RuntimeError(
            "Catalog equality gate failed: enabled={} surface_wrap={} compiled={} profiles={} bindings={} tested={} passed={}".format(
                expected,
                payload["surface_wrap_row_count"],
                payload["compiled_row_count"],
                valid_profile_count,
                valid_binding_count,
                payload["tested_row_count"],
                passed_row_count,
            )
        )
    if not payload["protected_inputs_unchanged"]:
        raise RuntimeError("A protected input package changed during catalog compilation.")


def main():
    payload = {
        "schema_version": 6,
        "compiler_version": 26,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_FAIL",
        "success": False,
        "project": PROJECT_FILE,
        "catalog": CATALOG_PATH,
        "compatibility_reference": COMPATIBILITY_PATH,
        "output_root": OUTPUT_ROOT,
        "errors": [],
    }
    try:
        _run(payload)
        payload["success"] = True
        payload["status"] = "UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_PASS"
        _write_receipt(payload)
        unreal.log("EF_CLOTHING_MORPH_V26_CATALOG_RECEIPT=" + RECEIPT_PATH)
    except Exception as exc:
        payload["errors"].append(str(exc))
        payload["traceback"] = traceback.format_exc()
        try:
            _write_receipt(payload)
        except Exception as receipt_error:
            unreal.log_error("Could not write V26 compiler receipt: " + repr(receipt_error))
        unreal.log_error(
            "EF Clothing Morph V26 catalog compile failed: "
            + json.dumps(payload, sort_keys=True, default=str)
        )
        raise


main()
