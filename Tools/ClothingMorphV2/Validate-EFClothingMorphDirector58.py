"""Read-only validation for the schema-3 single Clothing Director.

The validator intentionally ignores the retired schema-1 DataTables.  It checks
the one public Director, its stable garment IDs and safe offsets, the internal
compiled registry and the latest schema-3 compiler receipt.  No package is
saved, compiled, renamed or deleted.
"""

from __future__ import annotations

import datetime
import glob
import hashlib
import json
import math
import os
import re
import traceback

import unreal


PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
CONTENT_DIR = os.path.realpath(unreal.Paths.project_content_dir())
SAVED_DIR = os.path.realpath(unreal.Paths.project_saved_dir())
DIRECTOR_PATH = "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector"
REGISTRY_PATH = "/EFClothingMorph/_Internal/Compiled/V26/DA_EFClothingFitRegistry"
INTERNAL_ROOT = "/EFClothingMorph/_Internal/Compiled/V26"
SETTINGS_SECTION = "/Script/EFClothingMorphRuntime.EFClothingMorphV2Settings"
EXPECTED_DIRECTOR_CONFIG = DIRECTOR_PATH + ".DA_EFClothingMorphDirector"
EXPECTED_REGISTRY_CONFIG = REGISTRY_PATH + ".DA_EFClothingFitRegistry"
DIRECTOR_SCHEMA = 3
COMPILER_RECEIPT_SCHEMA = 10
COMPILER_VERSION = 26
THICKNESS_SHELL_ALGORITHM_VERSION = 4
MAXIMUM_SAFE_OFFSET_CM = 0.35
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
RECEIPT_PATH = os.path.join(
    SAVED_DIR,
    "ClothingMorphV2QA",
    "Director",
    f"EFClothingMorphDirectorValidation_{STAMP}.json",
)

PROTECTED_RELATIVE_PATHS = (
    "Content/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.uasset",
    "Content/DazToUnreal/Female/Female.uasset",
    "Content/DazToUnreal/Male/Male.uasset",
    "Content/DazToUnreal/Multiple/Multiple.uasset",
    "Content/DazToUnreal/Multiple/Multiple_Skeleton.uasset",
    "Content/DazToUnreal/UnderWearPanty/UnderWearPanty.uasset",
    "Content/DazToUnreal/UnderWearPanty/UnderWearPanty_Skeleton.uasset",
    "Content/FullSample/Player.uasset",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def snapshot(relative_paths: tuple[str, ...]) -> dict:
    result = {}
    for relative_path in relative_paths:
        path = os.path.realpath(os.path.join(PROJECT_DIR, relative_path))
        if not os.path.isfile(path):
            fail("Missing protected input: " + path)
        result[relative_path] = {
            "size_bytes": os.path.getsize(path),
            "sha256": sha256(path),
        }
    return result


def write_receipt(payload) -> None:
    root = os.path.realpath(os.path.dirname(RECEIPT_PATH))
    if os.path.commonpath((root, SAVED_DIR)).lower() != SAVED_DIR.lower():
        fail("Validation receipt escaped Saved.")
    os.makedirs(root, exist_ok=True)
    temporary = RECEIPT_PATH + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False, sort_keys=True, default=str)
        handle.write("\n")
    os.replace(temporary, RECEIPT_PATH)


def get_property(value, name):
    try:
        return value.get_editor_property(name)
    except Exception:
        return getattr(value, name)


def editor_property_exists(value, name: str) -> bool:
    try:
        value.get_editor_property(name)
        return True
    except Exception:
        return False


def object_path(value) -> str:
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def canonical_asset_path(value) -> str:
    text = object_path(value)
    match = re.search(r"/(?:Game|EFClothingMorph)/[A-Za-z0-9_./-]+", text)
    if not match:
        return ""
    return match.group(0).rstrip("'\"").split(".", 1)[0]


def boolean(value) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().casefold() not in {"0", "false", "no", "off", "disabled"}


def shell_metrics(profile) -> dict:
    return {
        "enabled": boolean(get_property(profile, "compiled_thickness_shell")),
        "algorithm_version": int(
            get_property(profile, "thickness_shell_algorithm_version")
        ),
        "requested_thickness_cm": float(
            get_property(profile, "compiled_thickness_cm")
        ),
        "pre_shell_vertices": int(get_property(profile, "pre_shell_vertex_count")),
        "pre_shell_triangles": int(
            get_property(profile, "pre_shell_triangle_count")
        ),
        "final_shell_vertices": int(
            get_property(profile, "final_shell_vertex_count")
        ),
        "final_shell_triangles": int(
            get_property(profile, "final_shell_triangle_count")
        ),
        "vertex_pairs": int(get_property(profile, "shell_vertex_pair_count")),
        "boundary_loops": int(
            get_property(profile, "shell_boundary_loop_count")
        ),
        "wall_triangles": int(
            get_property(profile, "shell_wall_triangle_count")
        ),
        "open_boundaries_after": int(
            get_property(profile, "shell_open_boundary_count_after")
        ),
        "degenerate_triangles": int(
            get_property(profile, "shell_degenerate_triangle_count")
        ),
        "detected_non_adjacent_intersection_pairs": int(
            get_property(
                profile, "shell_detected_non_adjacent_intersection_count"
            )
        ),
        "baseline_source_intersection_pairs": int(
            get_property(profile, "shell_baseline_source_intersection_pair_count")
        ),
        "tolerated_inherited_source_intersection_pairs": int(
            get_property(
                profile, "shell_tolerated_inherited_source_intersection_count"
            )
        ),
        "baseline_inheritance_radius_cm": float(
            get_property(profile, "shell_baseline_inheritance_radius_cm")
        ),
        "tolerated_local_repair_intersection_count": int(
            get_property(
                profile, "shell_tolerated_local_repair_intersection_count"
            )
        ),
        "local_repair_thickness_ceiling_cm": float(
            get_property(profile, "shell_local_repair_thickness_ceiling_cm")
        ),
        "tolerated_excluded_region_intersection_pairs": int(
            get_property(
                profile, "shell_tolerated_excluded_region_intersection_count"
            )
        ),
        "excluded_region_affected_source_triangles": int(
            get_property(
                profile, "shell_excluded_region_affected_source_triangle_count"
            )
        ),
        "excluded_region_certification_radius_cm": float(
            get_property(profile, "shell_excluded_region_certification_radius_cm")
        ),
        "excluded_region_maximum_witness_distance_cm": float(
            get_property(
                profile, "shell_excluded_region_maximum_witness_distance_cm"
            )
        ),
        "self_intersects": boolean(
            get_property(profile, "shell_self_intersects")
        ),
        "minimum_measured_thickness_cm": float(
            get_property(profile, "shell_minimum_measured_thickness_cm")
        ),
        "average_measured_thickness_cm": float(
            get_property(profile, "shell_average_measured_thickness_cm")
        ),
        "maximum_measured_thickness_cm": float(
            get_property(profile, "shell_maximum_measured_thickness_cm")
        ),
    }


def shell_metrics_match(profile_values: dict, receipt_values: dict) -> bool:
    for name, expected in profile_values.items():
        if name not in receipt_values:
            return False
        actual = receipt_values[name]
        if isinstance(expected, float):
            try:
                if not math.isclose(
                    expected, float(actual), rel_tol=0.0, abs_tol=0.00001
                ):
                    return False
            except (TypeError, ValueError):
                return False
        elif actual != expected:
            return False
    return True


def validate_shell_contract(
    shell: dict,
    requested: bool,
    has_explicit_anatomy_exclusion: bool,
    require_derived_receipt_fields: bool = False,
) -> tuple[bool, str]:
    try:
        if boolean(shell.get("enabled")) != requested:
            return False, "compiled shell enabled state differs from Director"

        integer_fields = (
            "algorithm_version",
            "pre_shell_vertices",
            "pre_shell_triangles",
            "final_shell_vertices",
            "final_shell_triangles",
            "vertex_pairs",
            "boundary_loops",
            "wall_triangles",
            "open_boundaries_after",
            "degenerate_triangles",
            "detected_non_adjacent_intersection_pairs",
            "baseline_source_intersection_pairs",
            "tolerated_inherited_source_intersection_pairs",
            "tolerated_local_repair_intersection_count",
            "tolerated_excluded_region_intersection_pairs",
            "excluded_region_affected_source_triangles",
        )
        float_fields = (
            "requested_thickness_cm",
            "baseline_inheritance_radius_cm",
            "local_repair_thickness_ceiling_cm",
            "excluded_region_certification_radius_cm",
            "excluded_region_maximum_witness_distance_cm",
            "minimum_measured_thickness_cm",
            "average_measured_thickness_cm",
            "maximum_measured_thickness_cm",
        )
        values = {name: int(shell[name]) for name in integer_fields}
        values.update({name: float(shell[name]) for name in float_fields})
        if not all(math.isfinite(values[name]) for name in float_fields):
            return False, "shell receipt contains NaN or Inf"

        if not requested:
            zero_fields = integer_fields + float_fields
            if any(values[name] != 0 for name in zero_fields) or boolean(
                shell.get("self_intersects")
            ):
                return False, "disabled shell retains non-zero V4 evidence"
            return True, "disabled shell evidence is zero"

        if values["algorithm_version"] != THICKNESS_SHELL_ALGORITHM_VERSION:
            return False, "shell algorithm is not V4"
        if (
            values["requested_thickness_cm"] <= 0.0
            or values["pre_shell_vertices"] <= 0
            or values["pre_shell_triangles"] <= 0
            or values["final_shell_vertices"]
            != values["pre_shell_vertices"] * 2
            or values["final_shell_triangles"]
            != values["pre_shell_triangles"] * 2 + values["wall_triangles"]
            or values["vertex_pairs"] != values["pre_shell_vertices"]
            or values["boundary_loops"] <= 0
            or values["wall_triangles"] <= 0
            or values["open_boundaries_after"] != 0
            or values["degenerate_triangles"] != 0
            or values["minimum_measured_thickness_cm"] <= 0.0
            or not (
                values["minimum_measured_thickness_cm"]
                <= values["average_measured_thickness_cm"]
                <= values["maximum_measured_thickness_cm"]
            )
        ):
            return False, "shell topology or measured thickness evidence is invalid"

        detected = values["detected_non_adjacent_intersection_pairs"]
        baseline = values["baseline_source_intersection_pairs"]
        inherited = values["tolerated_inherited_source_intersection_pairs"]
        local_repair = values["tolerated_local_repair_intersection_count"]
        excluded = values["tolerated_excluded_region_intersection_pairs"]
        affected = values["excluded_region_affected_source_triangles"]
        residual = detected - inherited - local_repair - excluded
        maximum_inherited = baseline * 8 + 64
        maximum_local_repair = min(
            512, int(math.ceil(values["final_shell_triangles"] * 0.01))
        )
        maximum_excluded = min(
            512, int(math.ceil(values["final_shell_triangles"] * 0.01))
        )
        maximum_affected = int(
            math.ceil(values["pre_shell_triangles"] * 0.01)
        )
        witness_distance = values[
            "excluded_region_maximum_witness_distance_cm"
        ]
        exclusion_radius = values["excluded_region_certification_radius_cm"]
        expected_local_repair_thickness_ceiling = max(
            0.0001, values["requested_thickness_cm"] * 0.15
        )

        if (
            detected < 0
            or baseline < 0
            or inherited < 0
            or local_repair < 0
            or excluded < 0
            or affected < 0
            or inherited + local_repair + excluded != detected
            or residual != 0
            or inherited > maximum_inherited
            or (baseline == 0 and inherited != 0)
            or local_repair > maximum_local_repair
            or values["baseline_inheritance_radius_cm"] <= 0.0
            or values["local_repair_thickness_ceiling_cm"] <= 0.0
            or not math.isclose(
                values["local_repair_thickness_ceiling_cm"],
                expected_local_repair_thickness_ceiling,
                rel_tol=0.0,
                abs_tol=0.000001,
            )
            or exclusion_radius <= 0.0
            or witness_distance < 0.0
            or boolean(shell.get("self_intersects")) != (detected > 0)
        ):
            return False, "shell baseline/intersection evidence is inconsistent"

        if excluded == 0:
            if affected != 0 or witness_distance > 0.0001:
                return False, "shell without exclusions reports exclusion evidence"
        elif (
            not has_explicit_anatomy_exclusion
            or excluded > maximum_excluded
            or affected > maximum_affected
            or exclusion_radius <= 0.0
            or witness_distance > exclusion_radius + 0.0001
        ):
            return False, "new shell intersections are not spatially certified exclusions"

        derived_expectations = {
            "residual_new_intersection_pairs": residual,
            "maximum_tolerated_inherited_pairs": maximum_inherited,
            "maximum_tolerated_local_repair_pairs": maximum_local_repair,
            "maximum_tolerated_excluded_region_pairs": maximum_excluded,
            "maximum_affected_source_triangles": maximum_affected,
            "has_explicit_anatomy_exclusion": has_explicit_anatomy_exclusion,
        }
        if require_derived_receipt_fields:
            for name, expected in derived_expectations.items():
                if name not in shell or shell[name] != expected:
                    return False, f"shell derived receipt field {name} is inconsistent"
        return True, "shell V4 baseline/local-repair/exclusion contract is valid"
    except (KeyError, TypeError, ValueError) as exc:
        return False, "shell V4 receipt is incomplete: " + repr(exc)


def read_configured_asset_paths() -> tuple[str, str]:
    """Read the two authoritative soft-object paths without Python class reflection."""
    config_path = os.path.realpath(os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini"))
    expected_config_root = os.path.realpath(os.path.join(PROJECT_DIR, "Config"))
    if os.path.commonpath((config_path, expected_config_root)).lower() != expected_config_root.lower():
        fail("DefaultGame.ini escaped the project Config directory")
    if not os.path.isfile(config_path):
        fail("Missing project DefaultGame.ini")

    values: dict[str, list[str]] = {"directorpolicy": [], "registry": []}
    active_section = ""
    section_count = 0
    with open(config_path, "r", encoding="utf-8-sig") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line.startswith((";", "#")):
                continue
            if line.startswith("[") and line.endswith("]"):
                active_section = line[1:-1].strip()
                if active_section.casefold() == SETTINGS_SECTION.casefold():
                    section_count += 1
                continue
            if active_section.casefold() != SETTINGS_SECTION.casefold() or "=" not in line:
                continue
            key, value = line.split("=", 1)
            normalized_key = key.strip().casefold()
            if normalized_key in values:
                values[normalized_key].append(value.strip())

    if section_count != 1:
        fail(
            f"DefaultGame.ini must define exactly one [{SETTINGS_SECTION}] section; found {section_count}"
        )
    for key, entries in values.items():
        if len(entries) != 1:
            fail(
                f"{SETTINGS_SECTION} must define exactly one {key} entry; found {len(entries)}"
            )
    configured_director = values["directorpolicy"][0]
    configured_registry = values["registry"][0]
    if configured_director != EXPECTED_DIRECTOR_CONFIG or configured_registry != EXPECTED_REGISTRY_CONFIG:
        fail(
            "EF Clothing Morph settings paths are not exact: "
            + repr(
                {
                    "director": configured_director,
                    "expected_director": EXPECTED_DIRECTOR_CONFIG,
                    "registry": configured_registry,
                    "expected_registry": EXPECTED_REGISTRY_CONFIG,
                }
            )
        )
    return canonical_asset_path(configured_director), canonical_asset_path(configured_registry)


def garment_id(garment) -> str:
    result = str(get_property(garment, "garment_id")).strip()
    if not result or result.casefold() == "none":
        fail("Director contains an empty Garment Id")
    return result


def validate_policy(director) -> None:
    if int(get_property(director, "schema_version")) != DIRECTOR_SCHEMA:
        fail("Director schema is not 3")
    validator = getattr(director, "is_policy_valid", None)
    error_getter = getattr(director, "get_policy_validation_error", None)
    if not callable(validator) or not callable(error_getter):
        fail("Director does not expose schema-3 validation functions")
    if not bool(validator()):
        fail("Director policy validation failed: " + str(error_getter()))


def validate_internal_path(value, label: str) -> str:
    path = canonical_asset_path(value)
    if not path.startswith(INTERNAL_ROOT + "/"):
        fail(f"{label} escaped internal compiled content: {path!r}")
    return path


def load_latest_compiler_receipt(
    expected_ids,
    enabled_ids,
    enabled_shell_ids,
    explicit_anatomy_exclusion_ids,
    registry_path,
) -> dict:
    pattern = os.path.join(
        SAVED_DIR, "ClothingMorphV2QA", "compiler_receipt_FullCatalog_V26*.json"
    )
    candidates = sorted(glob.glob(pattern), key=os.path.getmtime, reverse=True)
    for path in candidates:
        try:
            with open(path, "r", encoding="utf-8") as handle:
                payload = json.load(handle)
        except Exception:
            continue
        if not payload.get("success"):
            continue
        if int(payload.get("schema_version", -1)) != COMPILER_RECEIPT_SCHEMA:
            continue
        if int(payload.get("compiler_version", -1)) != COMPILER_VERSION:
            continue
        if (
            int(payload.get("thickness_shell_algorithm_version", -1))
            != THICKNESS_SHELL_ALGORITHM_VERSION
        ):
            continue
        if payload.get("status") != "UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_PASS":
            continue
        if canonical_asset_path(payload.get("director")) != DIRECTOR_PATH:
            continue
        if payload.get("output_root") != INTERNAL_ROOT:
            continue
        if int(payload.get("director_schema_version", -1)) != DIRECTOR_SCHEMA:
            continue
        if sorted(str(value) for value in payload.get("garment_ids", [])) != sorted(expected_ids):
            continue
        if sorted(str(value) for value in payload.get("enabled_garment_ids", [])) != sorted(enabled_ids):
            continue
        if sorted(
            str(value) for value in payload.get("enabled_thickness_shell_ids", [])
        ) != sorted(enabled_shell_ids):
            continue
        if int(payload.get("requested_thickness_shell_count", -1)) != len(
            enabled_shell_ids
        ) or int(payload.get("valid_thickness_shell_count", -1)) != len(
            enabled_shell_ids
        ):
            continue
        if canonical_asset_path(payload.get("registry")) != registry_path:
            continue
        if (
            not payload.get("catalog_equality_gate")
            or not payload.get("shell_intersection_policy_gate")
            or not payload.get("protected_inputs_unchanged")
        ):
            continue

        rows = payload.get("rows", [])
        if not isinstance(rows, list):
            continue
        rows_by_id = {}
        duplicate_row = False
        for row in rows:
            row_id = str(row.get("garment_id", ""))
            if not row_id or row_id in rows_by_id:
                duplicate_row = True
                break
            rows_by_id[row_id] = row
        if duplicate_row or sorted(rows_by_id) != sorted(enabled_ids):
            continue

        detected_total = 0
        baseline_total = 0
        inherited_total = 0
        local_repair_total = 0
        excluded_total = 0
        residual_total = 0
        inherited_ids = []
        local_repair_ids = []
        excluded_ids = []
        receipt_shell_contract_valid = True
        for row_id, row in rows_by_id.items():
            requested = row_id in enabled_shell_ids
            shell = row.get("thickness_shell")
            if not isinstance(shell, dict):
                receipt_shell_contract_valid = False
                break
            contract_valid, _ = validate_shell_contract(
                shell,
                requested,
                row_id in explicit_anatomy_exclusion_ids,
                require_derived_receipt_fields=True,
            )
            if (
                not contract_valid
                or not boolean(row.get("thickness_shell_valid"))
                or boolean(row.get("thickness_shell_requested")) != requested
                or not boolean(
                    row.get("thickness_shell_intersection_policy_valid")
                )
            ):
                receipt_shell_contract_valid = False
                break
            if not requested:
                continue
            detected = int(shell["detected_non_adjacent_intersection_pairs"])
            baseline = int(shell["baseline_source_intersection_pairs"])
            inherited = int(
                shell["tolerated_inherited_source_intersection_pairs"]
            )
            local_repair = int(
                shell["tolerated_local_repair_intersection_count"]
            )
            excluded = int(
                shell["tolerated_excluded_region_intersection_pairs"]
            )
            detected_total += detected
            baseline_total += baseline
            inherited_total += inherited
            local_repair_total += local_repair
            excluded_total += excluded
            residual_total += detected - inherited - local_repair - excluded
            if inherited > 0:
                inherited_ids.append(row_id)
            if local_repair > 0:
                local_repair_ids.append(row_id)
            if excluded > 0:
                excluded_ids.append(row_id)
        if not receipt_shell_contract_valid:
            continue
        if residual_total != 0:
            continue

        aggregate_contract = {
            "detected_shell_intersection_pair_count": detected_total,
            "baseline_source_shell_intersection_pair_count": baseline_total,
            "tolerated_inherited_shell_intersection_pair_count": inherited_total,
            "tolerated_local_repair_shell_intersection_pair_count": local_repair_total,
            "tolerated_excluded_region_shell_intersection_pair_count": excluded_total,
            "residual_shell_intersection_pair_count": residual_total,
            "tolerated_shell_intersection_pair_count": inherited_total
            + local_repair_total
            + excluded_total,
        }
        if any(
            int(payload.get(name, -1)) != expected
            for name, expected in aggregate_contract.items()
        ):
            continue
        if sorted(
            str(value)
            for value in payload.get("inherited_shell_intersection_ids", [])
        ) != sorted(inherited_ids):
            continue
        if sorted(
            str(value)
            for value in payload.get("local_repair_shell_intersection_ids", [])
        ) != sorted(local_repair_ids):
            continue
        if sorted(
            str(value)
            for value in payload.get("excluded_region_shell_intersection_ids", [])
        ) != sorted(excluded_ids):
            continue
        if sorted(
            str(value)
            for value in payload.get("tolerated_shell_intersection_ids", [])
        ) != sorted(
            set(inherited_ids) | set(local_repair_ids) | set(excluded_ids)
        ):
            continue
        payload = dict(payload)
        payload["path"] = path
        return payload
    fail("No successful schema-3 Director compiler receipt matches the current policy")


def main() -> None:
    result = {
        "schema": "EFClothingMorph.Director.Validation.3",
        "status": "FAIL",
        "success": False,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "director": DIRECTOR_PATH,
        "registry": REGISTRY_PATH,
        "internal_root": INTERNAL_ROOT,
    }
    try:
        result["protected_before"] = snapshot(PROTECTED_RELATIVE_PATHS)
        director_class = unreal.load_class(
            None, "/Script/EFClothingMorphRuntime.EFClothingMorphDirectorPolicy"
        )
        registry_class = unreal.load_class(
            None, "/Script/EFClothingMorphRuntime.EFClothingFitRegistry"
        )
        if director_class is None or registry_class is None:
            fail("One or more EF Clothing Morph native classes are unavailable")

        asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
        asset_registry.scan_paths_synchronous([INTERNAL_ROOT], True)
        director = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
        registry = unreal.EditorAssetLibrary.load_asset(REGISTRY_PATH)
        if director is None or object_path(director.get_class()) != object_path(director_class):
            fail(
                "Single Clothing Director is missing or has the wrong class: "
                + object_path(director.get_class() if director else None)
            )
        if registry is None or object_path(registry.get_class()) != object_path(registry_class):
            fail(
                "Internal V26 registry is missing or has the wrong class: asset={} class={} expected={}".format(
                    object_path(registry),
                    object_path(registry.get_class() if registry else None),
                    object_path(registry_class),
                )
            )
        validate_policy(director)

        configured_director, configured_registry = read_configured_asset_paths()
        if configured_director != DIRECTOR_PATH or configured_registry != REGISTRY_PATH:
            fail(
                "Settings do not reference the single Director and internal registry: "
                + repr(
                    {
                        "director": configured_director,
                        "registry": configured_registry,
                    }
                )
            )

        retired_global_properties = (
            "enable_runtime_tuning",
            "global_additional_clearance_cm",
            "maximum_additional_clearance_cm",
        )
        reflected_retired_properties = [
            name for name in retired_global_properties if editor_property_exists(director, name)
        ]
        if reflected_retired_properties:
            fail(
                "Director still reflects retired top-level runtime-offset controls: "
                + ", ".join(reflected_retired_properties)
            )

        garments = list(get_property(director, "garments"))
        maximum_offset = MAXIMUM_SAFE_OFFSET_CM

        ids = []
        enabled_ids = []
        enabled_shell_ids = []
        offsets = {}
        thickness_shells = {}
        pairs = {}
        garments_by_id = {}
        explicit_anatomy_exclusion_ids = set()
        for garment in garments:
            enabled = boolean(get_property(garment, "enabled"))
            raw_id = str(get_property(garment, "garment_id")).strip()
            has_id = bool(raw_id) and raw_id.casefold() != "none"
            source_path = canonical_asset_path(get_property(garment, "source_garment"))
            body_path = canonical_asset_path(get_property(garment, "body_surface"))
            if not enabled and not has_id and not source_path and not body_path:
                # Details-panel array placeholders are intentionally ignored until
                # the author assigns an identity/source/body and enables the row.
                continue
            index = garment_id(garment)
            if index in ids:
                fail("Duplicate Garment Id: " + index)
            ids.append(index)
            garments_by_id[index] = garment
            offset = float(get_property(garment, "additional_clearance_cm"))
            offset_enabled = boolean(get_property(garment, "enable_runtime_tuning"))
            effective_offset = (
                min(max(offset, 0.0), maximum_offset)
                if offset_enabled and math.isfinite(offset)
                else 0.0
            )
            offsets[index] = {
                "enabled": offset_enabled,
                "authored_additional_clearance_cm": offset,
                "effective_additional_clearance_cm": effective_offset,
            }
            shell_enabled = boolean(get_property(garment, "create_thickness_shell"))
            shell_thickness_cm = float(get_property(garment, "shell_thickness_cm"))
            shell_steps = int(get_property(garment, "shell_offset_steps"))
            shell_offset_boundaries = boolean(
                get_property(garment, "shell_offset_boundaries")
            )
            shell_smoothing = float(
                get_property(garment, "shell_smoothing_per_step")
            )
            shell_reproject = boolean(get_property(garment, "shell_reproject_smooth"))
            if shell_enabled and (
                not math.isfinite(shell_thickness_cm)
                or shell_thickness_cm < 0.01
                or shell_thickness_cm > 0.35
                or shell_steps < 1
                or shell_steps > 100
                or not shell_offset_boundaries
                or not math.isfinite(shell_smoothing)
                or shell_smoothing < 0.0
                or shell_smoothing > 1.0
            ):
                fail(f"Garment {index} has invalid real-thickness settings")
            thickness_shells[index] = {
                "enabled": shell_enabled,
                "thickness_cm": shell_thickness_cm,
                "steps": shell_steps,
                "offset_boundaries": shell_offset_boundaries,
                "smoothing_per_step": shell_smoothing,
                "reproject_smooth": shell_reproject,
            }
            if not enabled:
                continue
            source = source_path
            body = body_path
            if not source or not body:
                fail(f"Enabled garment {index} has no source/body pair")
            pair = source + "|" + body
            if pair in pairs:
                fail(f"Garments {pairs[pair]} and {index} duplicate a source/body pair")
            pairs[pair] = index
            enabled_ids.append(index)
            if (
                list(
                    get_property(
                        garment, "excluded_body_surface_material_slots"
                    )
                )
                and list(get_property(garment, "excluded_body_bone_branches"))
            ):
                explicit_anatomy_exclusion_ids.add(index)
            if shell_enabled:
                enabled_shell_ids.append(index)

        if not ids or not enabled_ids:
            fail("Director requires at least one garment and one enabled garment")

        profiles = list(get_property(registry, "profiles"))
        if len(profiles) != len(enabled_ids):
            fail(
                f"Internal registry profile count {len(profiles)} does not match enabled Director entries {len(enabled_ids)}"
            )
        registry_ids = []
        native_profile_reports = {}
        profile_shell_evidence = {}
        compiler_library = getattr(unreal, "EFClothingFitCompilerLibrary", None)
        if compiler_library is None:
            fail("Native EFClothingFitCompilerLibrary is unavailable")
        for profile in profiles:
            if profile is None:
                fail("Internal registry contains a null profile")
            validate_internal_path(profile, "Fit profile")
            source = canonical_asset_path(get_property(profile, "source_garment"))
            body = canonical_asset_path(get_property(profile, "body_surface"))
            pair = source + "|" + body
            index = pairs.get(pair)
            if index is None:
                fail("Internal profile has no enabled Director source/body pair: " + pair)
            registry_ids.append(index)
            validate_internal_path(get_property(profile, "fitted_garment"), "Derived garment")
            surface_binding = get_property(profile, "surface_binding")
            if canonical_asset_path(surface_binding):
                validate_internal_path(surface_binding, "Surface binding")
            validation = compiler_library.validate_compiled_profile_detailed(profile)
            validation_success = boolean(get_property(validation, "success"))
            validation_report = str(get_property(validation, "report"))
            native_profile_reports[index] = validation_report
            if not validation_success:
                fail(f"Native profile validation failed for {index}: {validation_report}")
            profile_shell = shell_metrics(profile)
            shell_contract_valid, shell_contract_report = validate_shell_contract(
                profile_shell,
                index in enabled_shell_ids,
                index in explicit_anatomy_exclusion_ids,
            )
            if not shell_contract_valid:
                fail(
                    f"Profile shell V4 contract failed for {index}: "
                    + shell_contract_report
                )
            profile_shell_evidence[index] = profile_shell
        if sorted(registry_ids) != sorted(enabled_ids):
            fail(
                "Internal registry identities differ from enabled Director entries: "
                + repr({"registry": sorted(registry_ids), "director": sorted(enabled_ids)})
            )

        result["director_schema_version"] = int(
            get_property(director, "schema_version")
        )
        result["garment_ids"] = sorted(ids)
        result["enabled_garment_ids"] = sorted(enabled_ids)
        result["enabled_thickness_shell_ids"] = sorted(enabled_shell_ids)
        result["per_garment_runtime_offsets_only"] = True
        result["per_garment_controls_only"] = True
        result["retired_global_director_controls_absent"] = True
        result["runtime_offset_limit_cm"] = maximum_offset
        result["garment_offsets_cm"] = offsets
        result["garment_thickness_shells"] = thickness_shells
        result["registry_profile_count"] = len(profiles)
        result["native_profile_reports"] = native_profile_reports
        result["compiler_receipt"] = load_latest_compiler_receipt(
            ids,
            enabled_ids,
            enabled_shell_ids,
            explicit_anatomy_exclusion_ids,
            REGISTRY_PATH,
        )
        receipt_rows = {
            str(row["garment_id"]): row
            for row in result["compiler_receipt"]["rows"]
        }
        for index, profile_shell in profile_shell_evidence.items():
            receipt_shell = receipt_rows[index]["thickness_shell"]
            if not shell_metrics_match(profile_shell, receipt_shell):
                fail(
                    f"Profile/receipt shell V4 evidence differs for garment {index}"
                )
        result["profile_shell_evidence"] = profile_shell_evidence
        result["compiler_receipt_schema_version"] = int(
            result["compiler_receipt"]["schema_version"]
        )
        result["thickness_shell_algorithm_version"] = int(
            result["compiler_receipt"]["thickness_shell_algorithm_version"]
        )
        result["shell_intersection_policy_gate"] = boolean(
            result["compiler_receipt"]["shell_intersection_policy_gate"]
        )

        dirty_packages = list(
            unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        ) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
        result["dirty_packages"] = [object_path(package) for package in dirty_packages]
        if result["dirty_packages"]:
            fail("Read-only Director validation dirtied a package")

        result["protected_after"] = snapshot(PROTECTED_RELATIVE_PATHS)
        result["protected_inputs_unchanged"] = (
            result["protected_before"] == result["protected_after"]
        )
        if not result["protected_inputs_unchanged"]:
            fail("Read-only Director validation changed a protected asset")
        result["status"] = "PASS"
        result["success"] = True
        write_receipt(result)
        unreal.log("EF_CLOTHING_MORPH_DIRECTOR_VALIDATION_RECEIPT=" + RECEIPT_PATH)
    except Exception as exc:
        result["error"] = str(exc)
        result["traceback"] = traceback.format_exc()
        try:
            result["protected_after"] = snapshot(PROTECTED_RELATIVE_PATHS)
            result["protected_inputs_unchanged"] = (
                result.get("protected_before") == result["protected_after"]
            )
        except Exception as snapshot_error:
            result["snapshot_error"] = repr(snapshot_error)
        write_receipt(result)
        unreal.log_error("EF Clothing Morph Director validation failed: " + repr(exc))
        raise


main()
