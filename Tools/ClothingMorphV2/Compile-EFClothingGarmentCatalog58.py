"""Compile the schema-2 EF Clothing Morph Director transactionally.

The single Director is the only human-authored catalog.  Native compilation
publishes derived meshes, fit certificates, surface bindings and the registry
under the project-owned plugin's internal content mount.  Those artifacts are
runtime implementation and are not authoring surfaces.
"""

from __future__ import annotations

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
DIRECTOR_PATH = os.environ.get(
    "EF_CLOTHING_V26_DIRECTOR",
    "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector",
)
COMPATIBILITY_PATH = os.environ.get(
    "EF_CLOTHING_V26_COMPATIBILITY",
    "/Game/DazToUnreal/Multiple/Multiple",
)
RECEIPT_PATH = os.path.realpath(
    os.environ.get(
        "EF_CLOTHING_V26_RECEIPT",
        os.path.join(
            SAVED_DIR,
            "ClothingMorphV2QA",
            "compiler_receipt_FullCatalog_V26.json",
        ),
    )
)
OUTPUT_ROOT = "/EFClothingMorph/_Internal/Compiled/V26"
DIRECTOR_SCHEMA = 2


def _get(value, name):
    try:
        return value.get_editor_property(name)
    except Exception:
        return getattr(value, name)


def _try_get(value, *names, default=None):
    for name in names:
        try:
            return _get(value, name)
        except Exception:
            pass
    return default


def _object_path(value) -> str:
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def _canonical_asset_path(value) -> str:
    text = _object_path(value)
    match = re.search(r"/(?:Game|EFClothingMorph)/[A-Za-z0-9_./-]+", text)
    if not match:
        return ""
    return match.group(0).rstrip("'\"").split(".", 1)[0]


def _package_name(value) -> str:
    path = _canonical_asset_path(value)
    return path or _object_path(value).split(".", 1)[0]


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


def _bool(value) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().casefold() not in {"0", "false", "no", "off", "disabled"}


def _director_rows(director):
    rows = list(_get(director, "garments"))
    if not rows:
        raise RuntimeError("Schema-2 Director has no garments.")
    return rows


def _garment_id(row) -> str:
    result = str(_get(row, "garment_id")).strip()
    if not result or result.casefold() == "none":
        raise RuntimeError("Director contains an empty Garment Id.")
    return result


def _extract_director_mesh_packages(director):
    packages = set()
    ids = []
    enabled_ids = []
    for garment in _director_rows(director):
        garment_id = _garment_id(garment)
        ids.append(garment_id)
        if _bool(_get(garment, "enabled")):
            enabled_ids.append(garment_id)
        for property_name in ("source_garment", "body_surface"):
            package = _package_name(_get(garment, property_name))
            if package:
                if not package.startswith("/Game/"):
                    raise RuntimeError(
                        f"Director {garment_id} {property_name} escaped /Game: {package}"
                    )
                packages.add(package)
    if len(ids) != len(set(ids)):
        raise RuntimeError("Director contains duplicate Garment Id values.")
    return packages, sorted(ids), sorted(enabled_ids)


def _validate_director(director):
    if int(_get(director, "schema_version")) != DIRECTOR_SCHEMA:
        raise RuntimeError("Director schema is not 2.")
    validator = getattr(director, "is_policy_valid", None)
    error_getter = getattr(director, "get_policy_validation_error", None)
    if not callable(validator) or not callable(error_getter):
        raise RuntimeError("Director does not expose schema-2 validation functions.")
    if not bool(validator()):
        raise RuntimeError("Director validation failed: " + str(error_getter()))


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
                "body_render_vertices": int(_get(body_topology, "render_vertex_count")),
                "garment_topology_fingerprint": str(
                    _get(garment_topology, "topology_fingerprint")
                ),
                "body_topology_fingerprint": str(
                    _get(body_topology, "topology_fingerprint")
                ),
                "bound_render_vertices": int(_get(metrics, "bound_render_vertex_count")),
                "invalid_anchors": int(_get(metrics, "invalid_anchor_count")),
                "surface_follow_vertices": int(_get(metrics, "surface_follow_vertex_count")),
                "hybrid_vertices": int(_get(metrics, "hybrid_vertex_count")),
                "collision_only_vertices": int(_get(metrics, "collision_only_vertex_count")),
                "preserve_upstream_vertices": int(
                    _get(metrics, "preserve_upstream_vertex_count")
                ),
                "candidate_triangles": int(_get(metrics, "candidate_triangle_count")),
                "neighbor_references": int(_get(metrics, "neighbor_reference_count")),
                "witnesses": int(_get(metrics, "witness_count")),
                "degenerate_body_triangles": int(
                    _get(metrics, "degenerate_body_triangle_count")
                ),
                "excluded_body_triangles": int(_get(metrics, "excluded_body_triangle_count")),
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
        raise RuntimeError("Director compiler is restricted to NoShellForWinter.uproject.")
    engine_version = str(unreal.SystemLibrary.get_engine_version())
    if not engine_version.startswith("5.8."):
        raise RuntimeError("Expected UE 5.8, got " + engine_version)
    payload["engine_version"] = engine_version

    director_class = getattr(unreal, "EFClothingMorphDirectorPolicy", None)
    if director_class is None:
        raise RuntimeError("Native EFClothingMorphDirectorPolicy class is unavailable.")
    director = _load_asset(DIRECTOR_PATH, director_class)
    _validate_director(director)
    compatibility = _load_asset(COMPATIBILITY_PATH, unreal.SkeletalMesh)
    skeleton = _get(compatibility, "skeleton")
    if skeleton is None:
        raise RuntimeError("Compatibility mesh has no shared USkeleton.")

    director_mesh_packages, garment_ids, enabled_garment_ids = _extract_director_mesh_packages(
        director
    )
    payload["director_schema_version"] = int(_get(director, "schema_version"))
    payload["garment_ids"] = garment_ids
    payload["enabled_garment_ids"] = enabled_garment_ids

    protected_packages = set(director_mesh_packages)
    protected_packages.update(
        {
            _package_name(director),
            _package_name(compatibility),
            _package_name(skeleton),
        }
    )

    # A source/body mesh can reference a distinct USkeleton package. Protect
    # every such package explicitly; mesh hashes alone do not prove skeleton
    # integrity.
    for mesh_package in sorted(director_mesh_packages):
        mesh = _load_asset(mesh_package, unreal.SkeletalMesh)
        mesh_skeleton = _get(mesh, "skeleton")
        if mesh_skeleton is None:
            raise RuntimeError("Director mesh has no USkeleton: " + mesh_package)
        protected_packages.add(_package_name(mesh_skeleton))
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
        director, compatibility, options
    )
    payload["native_report"] = str(_get(result, "report"))
    payload["enabled_row_count"] = int(_get(result, "enabled_row_count"))
    payload["compiled_row_count"] = int(_get(result, "compiled_row_count"))
    payload["surface_wrap_row_count"] = int(_get(result, "surface_wrap_row_count"))
    payload["registry"] = _object_path(_get(result, "registry"))
    if not _package_name(_get(result, "registry")).startswith(OUTPUT_ROOT + "/"):
        raise RuntimeError("Published registry escaped the internal plugin output root.")

    payload["rows"] = []
    valid_profile_count = 0
    valid_binding_count = 0
    passed_row_count = 0
    compiled_ids = []
    for row in list(_get(result, "rows")):
        profile = _get(row, "profile")
        binding = _get(row, "surface_binding")
        row_id = str(_try_get(row, "garment_id", "row_name", default="None"))
        compiled_ids.append(row_id)
        row_payload = {
            "garment_id": row_id,
            "success": bool(_get(row, "success")),
            "requires_surface_binding": bool(_get(row, "requires_surface_binding")),
            "report": str(_get(row, "report")),
            "derived_garment": _object_path(_get(row, "derived_garment")),
            "profile": _object_path(profile),
            "surface_binding": _object_path(binding),
        }
        for field in ("derived_garment", "profile"):
            if not _package_name(row_payload[field]).startswith(OUTPUT_ROOT + "/"):
                raise RuntimeError(f"Compiled {field} for {row_id} escaped internal output root.")
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
            if not _package_name(binding).startswith(OUTPUT_ROOT + "/"):
                raise RuntimeError(f"Compiled binding for {row_id} escaped internal output root.")
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

    payload["compiled_garment_ids"] = sorted(compiled_ids)
    payload["valid_profile_count"] = valid_profile_count
    payload["valid_binding_count"] = valid_binding_count
    payload["tested_row_count"] = len(payload["rows"])
    payload["passed_row_count"] = passed_row_count
    payload["protected_sha256_after"] = _capture_hashes(protected_packages)
    payload["protected_inputs_unchanged"] = (
        payload["protected_sha256_before"] == payload["protected_sha256_after"]
    )
    expected = len(enabled_garment_ids)
    equality_gate = (
        expected > 0
        and expected == payload["enabled_row_count"]
        and expected == payload["compiled_row_count"]
        and expected == valid_profile_count
        and payload["surface_wrap_row_count"] == valid_binding_count
        and expected == payload["tested_row_count"]
        and expected == passed_row_count
        and sorted(enabled_garment_ids) == sorted(compiled_ids)
    )
    payload["catalog_equality_gate"] = equality_gate
    if not bool(_get(result, "success")):
        raise RuntimeError("Native Director compiler failed: " + payload["native_report"])
    if not equality_gate:
        raise RuntimeError(
            "Director equality gate failed: enabled={} surface_wrap={} compiled={} profiles={} bindings={} tested={} passed={} ids={}/{}".format(
                expected,
                payload["surface_wrap_row_count"],
                payload["compiled_row_count"],
                valid_profile_count,
                valid_binding_count,
                payload["tested_row_count"],
                passed_row_count,
                sorted(enabled_garment_ids),
                sorted(compiled_ids),
            )
        )
    if not payload["protected_inputs_unchanged"]:
        raise RuntimeError("A protected input package changed during Director compilation.")


def main():
    payload = {
        "schema_version": 7,
        "compiler_version": 26,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_FAIL",
        "success": False,
        "project": PROJECT_FILE,
        "director": DIRECTOR_PATH,
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
            "EF Clothing Morph V26 Director compile failed: "
            + json.dumps(payload, sort_keys=True, default=str)
        )
        raise


main()
