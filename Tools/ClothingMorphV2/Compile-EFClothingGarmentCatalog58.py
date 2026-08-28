"""Compile the schema-3 EF Clothing Morph Director transactionally.

The single Director is the only human-authored catalog.  Native compilation
publishes derived meshes, fit certificates, surface bindings and the registry
under the project-owned plugin's internal content mount.  Those artifacts are
runtime implementation and are not authoring surfaces.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import math
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
DIRECTOR_SCHEMA = 3
COMPILER_RECEIPT_SCHEMA = 10
THICKNESS_SHELL_ALGORITHM_VERSION = 4


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
    rows = [
        row
        for row in list(_get(director, "garments"))
        if not _is_disabled_empty_placeholder(row)
    ]
    if not rows:
        raise RuntimeError("Schema-3 Director has no configured garments.")
    return rows


def _is_disabled_empty_placeholder(row) -> bool:
    if _bool(_get(row, "enabled")):
        return False
    raw_id = str(_try_get(row, "garment_id", default="")).strip()
    has_id = bool(raw_id) and raw_id.casefold() != "none"
    has_source = bool(_package_name(_try_get(row, "source_garment", default=None)))
    has_body = bool(_package_name(_try_get(row, "body_surface", default=None)))
    return not has_id and not has_source and not has_body


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
        raise RuntimeError("Director schema is not 3.")
    validator = getattr(director, "is_policy_valid", None)
    error_getter = getattr(director, "get_policy_validation_error", None)
    if not callable(validator) or not callable(error_getter):
        raise RuntimeError("Director does not expose schema-3 validation functions.")
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


def _shell_metrics(profile):
    return {
        "enabled": bool(_get(profile, "compiled_thickness_shell")),
        "algorithm_version": int(_get(profile, "thickness_shell_algorithm_version")),
        "requested_thickness_cm": float(_get(profile, "compiled_thickness_cm")),
        "pre_shell_vertices": int(_get(profile, "pre_shell_vertex_count")),
        "pre_shell_triangles": int(_get(profile, "pre_shell_triangle_count")),
        "final_shell_vertices": int(_get(profile, "final_shell_vertex_count")),
        "final_shell_triangles": int(_get(profile, "final_shell_triangle_count")),
        "vertex_pairs": int(_get(profile, "shell_vertex_pair_count")),
        "boundary_loops": int(_get(profile, "shell_boundary_loop_count")),
        "wall_triangles": int(_get(profile, "shell_wall_triangle_count")),
        "open_boundaries_after": int(_get(profile, "shell_open_boundary_count_after")),
        "degenerate_triangles": int(_get(profile, "shell_degenerate_triangle_count")),
        "detected_non_adjacent_intersection_pairs": int(
            _get(profile, "shell_detected_non_adjacent_intersection_count")
        ),
        "baseline_source_intersection_pairs": int(
            _get(profile, "shell_baseline_source_intersection_pair_count")
        ),
        "tolerated_inherited_source_intersection_pairs": int(
            _get(profile, "shell_tolerated_inherited_source_intersection_count")
        ),
        "baseline_inheritance_radius_cm": float(
            _get(profile, "shell_baseline_inheritance_radius_cm")
        ),
        "tolerated_local_repair_intersection_count": int(
            _get(profile, "shell_tolerated_local_repair_intersection_count")
        ),
        "local_repair_thickness_ceiling_cm": float(
            _get(profile, "shell_local_repair_thickness_ceiling_cm")
        ),
        "tolerated_excluded_region_intersection_pairs": int(
            _get(profile, "shell_tolerated_excluded_region_intersection_count")
        ),
        "excluded_region_affected_source_triangles": int(
            _get(profile, "shell_excluded_region_affected_source_triangle_count")
        ),
        "excluded_region_certification_radius_cm": float(
            _get(profile, "shell_excluded_region_certification_radius_cm")
        ),
        "excluded_region_maximum_witness_distance_cm": float(
            _get(profile, "shell_excluded_region_maximum_witness_distance_cm")
        ),
        "self_intersects": bool(_get(profile, "shell_self_intersects")),
        "minimum_measured_thickness_cm": float(
            _get(profile, "shell_minimum_measured_thickness_cm")
        ),
        "average_measured_thickness_cm": float(
            _get(profile, "shell_average_measured_thickness_cm")
        ),
        "maximum_measured_thickness_cm": float(
            _get(profile, "shell_maximum_measured_thickness_cm")
        ),
    }


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
    director_rows = list(_director_rows(director))
    director_rows_by_id = {_garment_id(row): row for row in director_rows}
    enabled_shell_ids = sorted(
        _garment_id(row)
        for row in director_rows
        if _bool(_get(row, "enabled"))
        and _bool(_get(row, "create_thickness_shell"))
    )
    payload["director_schema_version"] = int(_get(director, "schema_version"))
    payload["garment_ids"] = garment_ids
    payload["enabled_garment_ids"] = enabled_garment_ids
    payload["enabled_thickness_shell_ids"] = enabled_shell_ids

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
    if bool(_get(result, "success")) and not _package_name(
        _get(result, "registry")
    ).startswith(OUTPUT_ROOT + "/"):
        raise RuntimeError("Published registry escaped the internal plugin output root.")

    payload["rows"] = []
    valid_profile_count = 0
    valid_binding_count = 0
    passed_row_count = 0
    valid_shell_count = 0
    detected_shell_intersection_pair_count = 0
    baseline_source_shell_intersection_pair_count = 0
    tolerated_inherited_shell_intersection_pair_count = 0
    tolerated_local_repair_shell_intersection_pair_count = 0
    tolerated_excluded_region_shell_intersection_pair_count = 0
    residual_shell_intersection_pair_count = 0
    inherited_shell_intersection_ids = []
    local_repair_shell_intersection_ids = []
    excluded_region_shell_intersection_ids = []
    shell_intersection_policy_gate = True
    compiled_ids = []
    for row in list(_get(result, "rows")):
        profile = _get(row, "profile")
        binding = _get(row, "surface_binding")
        row_id = str(_try_get(row, "garment_id", "row_name", default="None"))
        compiled_ids.append(row_id)
        row_payload = {
            "garment_id": row_id,
            "thickness_shell_requested": row_id in enabled_shell_ids,
            "success": bool(_get(row, "success")),
            "requires_surface_binding": bool(_get(row, "requires_surface_binding")),
            "report": str(_get(row, "report")),
            "derived_garment": _object_path(_get(row, "derived_garment")),
            "profile": _object_path(profile),
            "surface_binding": _object_path(binding),
        }
        for field in ("derived_garment", "profile"):
            if row_payload["success"] and not _package_name(
                row_payload[field]
            ).startswith(OUTPUT_ROOT + "/"):
                raise RuntimeError(f"Compiled {field} for {row_id} escaped internal output root.")
        if profile is not None:
            validation = unreal.EFClothingFitCompilerLibrary.validate_compiled_profile_detailed(
                profile
            )
            row_payload["validation_success"] = bool(_get(validation, "success"))
            row_payload["validation_report"] = str(_get(validation, "report"))
            row_payload["build_guid"] = _guid(_get(profile, "build_guid"))
            row_payload["thickness_shell"] = _shell_metrics(profile)
            shell = row_payload["thickness_shell"]
            director_row = director_rows_by_id.get(row_id)
            has_explicit_anatomy_exclusion = bool(
                director_row is not None
                and list(_get(director_row, "excluded_body_surface_material_slots"))
                and list(_get(director_row, "excluded_body_bone_branches"))
            )
            detected_pairs = shell["detected_non_adjacent_intersection_pairs"]
            baseline_pairs = shell["baseline_source_intersection_pairs"]
            inherited_pairs = shell[
                "tolerated_inherited_source_intersection_pairs"
            ]
            local_repair_pairs = shell[
                "tolerated_local_repair_intersection_count"
            ]
            excluded_pairs = shell[
                "tolerated_excluded_region_intersection_pairs"
            ]
            residual_pairs = (
                detected_pairs
                - inherited_pairs
                - local_repair_pairs
                - excluded_pairs
            )
            maximum_inherited_pairs = baseline_pairs * 8 + 64
            maximum_local_repair_pairs = min(
                512,
                int(math.ceil(shell["final_shell_triangles"] * 0.01)),
            )
            maximum_excluded_pairs = min(
                512,
                int(math.ceil(shell["final_shell_triangles"] * 0.01)),
            )
            maximum_affected_source_triangles = int(
                math.ceil(shell["pre_shell_triangles"] * 0.01)
            )
            expected_local_repair_thickness_ceiling_cm = max(
                0.0001,
                shell["requested_thickness_cm"] * 0.15,
            )
            shell["residual_new_intersection_pairs"] = residual_pairs
            shell["maximum_tolerated_inherited_pairs"] = maximum_inherited_pairs
            shell["maximum_tolerated_local_repair_pairs"] = (
                maximum_local_repair_pairs
            )
            shell["maximum_tolerated_excluded_region_pairs"] = (
                maximum_excluded_pairs
            )
            shell["maximum_affected_source_triangles"] = (
                maximum_affected_source_triangles
            )
            shell["has_explicit_anatomy_exclusion"] = (
                has_explicit_anatomy_exclusion
            )
            intersection_policy_valid = (
                detected_pairs >= 0
                and baseline_pairs >= 0
                and inherited_pairs >= 0
                and local_repair_pairs >= 0
                and excluded_pairs >= 0
                and inherited_pairs + local_repair_pairs + excluded_pairs
                == detected_pairs
                and residual_pairs == 0
                and inherited_pairs <= maximum_inherited_pairs
                and (baseline_pairs > 0 or inherited_pairs == 0)
                and local_repair_pairs <= maximum_local_repair_pairs
                and math.isfinite(shell["baseline_inheritance_radius_cm"])
                and shell["baseline_inheritance_radius_cm"] > 0.0
                and math.isfinite(shell["local_repair_thickness_ceiling_cm"])
                and shell["local_repair_thickness_ceiling_cm"] > 0.0
                and math.isclose(
                    shell["local_repair_thickness_ceiling_cm"],
                    expected_local_repair_thickness_ceiling_cm,
                    rel_tol=0.0,
                    abs_tol=0.000001,
                )
                and shell["excluded_region_affected_source_triangles"] >= 0
                and math.isfinite(shell["excluded_region_certification_radius_cm"])
                and shell["excluded_region_certification_radius_cm"] > 0.0
                and math.isfinite(
                    shell["excluded_region_maximum_witness_distance_cm"]
                )
                and shell["excluded_region_maximum_witness_distance_cm"] >= 0.0
                and shell["self_intersects"]
                == (detected_pairs > 0)
                and (
                    excluded_pairs == 0
                    and shell["excluded_region_affected_source_triangles"] == 0
                    and shell["excluded_region_maximum_witness_distance_cm"]
                    <= 0.0001
                    or (
                        excluded_pairs > 0
                        and has_explicit_anatomy_exclusion
                        and excluded_pairs <= maximum_excluded_pairs
                        and shell["excluded_region_affected_source_triangles"]
                        <= maximum_affected_source_triangles
                        and shell["excluded_region_certification_radius_cm"] > 0.0
                        and shell["excluded_region_maximum_witness_distance_cm"]
                        <= shell["excluded_region_certification_radius_cm"] + 0.0001
                    )
                )
            )
            row_payload["thickness_shell_intersection_policy_valid"] = (
                intersection_policy_valid
                if row_payload["thickness_shell_requested"]
                else not shell["self_intersects"]
            )
            disabled_shell_metrics_valid = (
                not shell["enabled"]
                and shell["algorithm_version"] == 0
                and shell["requested_thickness_cm"] == 0.0
                and shell["pre_shell_vertices"] == 0
                and shell["pre_shell_triangles"] == 0
                and shell["final_shell_vertices"] == 0
                and shell["final_shell_triangles"] == 0
                and shell["vertex_pairs"] == 0
                and shell["boundary_loops"] == 0
                and shell["wall_triangles"] == 0
                and shell["open_boundaries_after"] == 0
                and shell["degenerate_triangles"] == 0
                and shell["detected_non_adjacent_intersection_pairs"] == 0
                and shell["baseline_source_intersection_pairs"] == 0
                and shell["tolerated_inherited_source_intersection_pairs"] == 0
                and shell["baseline_inheritance_radius_cm"] == 0.0
                and shell["tolerated_local_repair_intersection_count"] == 0
                and shell["local_repair_thickness_ceiling_cm"] == 0.0
                and shell["tolerated_excluded_region_intersection_pairs"] == 0
                and shell["excluded_region_affected_source_triangles"] == 0
                and shell["excluded_region_certification_radius_cm"] == 0.0
                and shell["excluded_region_maximum_witness_distance_cm"] == 0.0
                and not shell["self_intersects"]
                and shell["minimum_measured_thickness_cm"] == 0.0
                and shell["average_measured_thickness_cm"] == 0.0
                and shell["maximum_measured_thickness_cm"] == 0.0
            )
            row_payload["thickness_shell_valid"] = (
                shell["enabled"] == row_payload["thickness_shell_requested"]
                and (
                    (
                        not row_payload["thickness_shell_requested"]
                        and disabled_shell_metrics_valid
                    )
                    or (
                        row_payload["thickness_shell_requested"]
                        and shell["algorithm_version"]
                        == THICKNESS_SHELL_ALGORITHM_VERSION
                        and shell["requested_thickness_cm"] > 0.0
                        and shell["pre_shell_vertices"] > 0
                        and shell["pre_shell_triangles"] > 0
                        and shell["final_shell_vertices"]
                        == shell["pre_shell_vertices"] * 2
                        and shell["final_shell_triangles"]
                        == shell["pre_shell_triangles"] * 2
                        + shell["wall_triangles"]
                        and shell["vertex_pairs"] == shell["pre_shell_vertices"]
                        and shell["boundary_loops"] > 0
                        and shell["wall_triangles"] > 0
                        and shell["open_boundaries_after"] == 0
                        and shell["degenerate_triangles"] == 0
                        and intersection_policy_valid
                        and shell["minimum_measured_thickness_cm"] > 0.0
                        and shell["average_measured_thickness_cm"] > 0.0
                        and shell["maximum_measured_thickness_cm"] > 0.0
                        and shell["minimum_measured_thickness_cm"]
                        <= shell["average_measured_thickness_cm"]
                        <= shell["maximum_measured_thickness_cm"]
                    )
                )
            )
            valid_shell_count += int(
                row_payload["thickness_shell_requested"]
                and row_payload["thickness_shell_valid"]
            )
            if row_payload["thickness_shell_requested"]:
                detected_shell_intersection_pair_count += shell[
                    "detected_non_adjacent_intersection_pairs"
                ]
                baseline_source_shell_intersection_pair_count += shell[
                    "baseline_source_intersection_pairs"
                ]
                tolerated_inherited_shell_intersection_pair_count += shell[
                    "tolerated_inherited_source_intersection_pairs"
                ]
                tolerated_local_repair_shell_intersection_pair_count += shell[
                    "tolerated_local_repair_intersection_count"
                ]
                tolerated_excluded_region_shell_intersection_pair_count += shell[
                    "tolerated_excluded_region_intersection_pairs"
                ]
                residual_shell_intersection_pair_count += residual_pairs
                if shell["tolerated_inherited_source_intersection_pairs"] > 0:
                    inherited_shell_intersection_ids.append(row_id)
                if shell["tolerated_local_repair_intersection_count"] > 0:
                    local_repair_shell_intersection_ids.append(row_id)
                if shell["tolerated_excluded_region_intersection_pairs"] > 0:
                    excluded_region_shell_intersection_ids.append(row_id)
                shell_intersection_policy_gate &= intersection_policy_valid
            valid_profile_count += int(row_payload["validation_success"])
        else:
            row_payload["validation_success"] = False
            row_payload["validation_report"] = "Profile is null."
            row_payload["thickness_shell"] = None
            row_payload["thickness_shell_valid"] = False
            row_payload["thickness_shell_intersection_policy_valid"] = False
            if row_payload["thickness_shell_requested"]:
                shell_intersection_policy_gate = False
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
            and row_payload["thickness_shell_valid"]
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
    payload["requested_thickness_shell_count"] = len(enabled_shell_ids)
    payload["valid_thickness_shell_count"] = valid_shell_count
    payload["detected_shell_intersection_pair_count"] = (
        detected_shell_intersection_pair_count
    )
    payload["baseline_source_shell_intersection_pair_count"] = (
        baseline_source_shell_intersection_pair_count
    )
    payload["tolerated_inherited_shell_intersection_pair_count"] = (
        tolerated_inherited_shell_intersection_pair_count
    )
    payload["tolerated_local_repair_shell_intersection_pair_count"] = (
        tolerated_local_repair_shell_intersection_pair_count
    )
    payload["tolerated_excluded_region_shell_intersection_pair_count"] = (
        tolerated_excluded_region_shell_intersection_pair_count
    )
    payload["residual_shell_intersection_pair_count"] = (
        residual_shell_intersection_pair_count
    )
    payload["inherited_shell_intersection_ids"] = sorted(
        inherited_shell_intersection_ids
    )
    payload["local_repair_shell_intersection_ids"] = sorted(
        local_repair_shell_intersection_ids
    )
    payload["excluded_region_shell_intersection_ids"] = sorted(
        excluded_region_shell_intersection_ids
    )
    # Compatibility aliases for older receipt readers. In schema 10 these mean
    # every explicitly classified/tolerated pair, not only anatomy exclusions.
    payload["tolerated_shell_intersection_pair_count"] = (
        tolerated_inherited_shell_intersection_pair_count
        + tolerated_local_repair_shell_intersection_pair_count
        + tolerated_excluded_region_shell_intersection_pair_count
    )
    payload["tolerated_shell_intersection_ids"] = sorted(
        set(inherited_shell_intersection_ids)
        | set(local_repair_shell_intersection_ids)
        | set(excluded_region_shell_intersection_ids)
    )
    payload["shell_intersection_policy_gate"] = shell_intersection_policy_gate
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
        and len(enabled_shell_ids) == valid_shell_count
        and shell_intersection_policy_gate
        and sorted(enabled_garment_ids) == sorted(compiled_ids)
    )
    payload["catalog_equality_gate"] = equality_gate
    if not bool(_get(result, "success")):
        raise RuntimeError("Native Director compiler failed: " + payload["native_report"])
    if not equality_gate:
        raise RuntimeError(
            "Director equality gate failed: enabled={} surface_wrap={} compiled={} profiles={} bindings={} shells={}/{} tested={} passed={} ids={}/{}".format(
                expected,
                payload["surface_wrap_row_count"],
                payload["compiled_row_count"],
                valid_profile_count,
                valid_binding_count,
                valid_shell_count,
                len(enabled_shell_ids),
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
        "schema_version": COMPILER_RECEIPT_SCHEMA,
        "compiler_version": 26,
        "thickness_shell_algorithm_version": THICKNESS_SHELL_ALGORITHM_VERSION,
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
