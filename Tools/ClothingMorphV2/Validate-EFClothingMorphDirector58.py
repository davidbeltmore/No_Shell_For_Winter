"""Read-only smoke validation for the EF Clothing Morph V2 Director.

This script intentionally never creates, fills, saves, recompiles or mutates
an Unreal asset.  It proves that the runtime startup contract can resolve the
Director, its two catalog tables and the compiled V26 registry from disk.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import math
import os
import re
import traceback
from pathlib import Path

import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()
DIRECTOR_PATH = "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector"
COMPILE_CATALOG_PATH = "/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments"
TUNING_CATALOG_PATH = "/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarmentTuning"
REGISTRY_PATH = "/Game/_Generated/EFClothingMorphV2/DA_EFClothingFitRegistry"
DIRECTOR_CLASS_PATH = "/Script/EFClothingMorphRuntime.EFClothingMorphDirectorPolicy"
SETTINGS_CLASS_PATH = "/Script/EFClothingMorphRuntime.EFClothingMorphV2Settings"
COMPILE_STRUCT_PATH = "/Script/EFClothingMorphRuntime.EFClothingGarmentRow"
TUNING_STRUCT_PATH = "/Script/EFClothingMorphRuntime.EFClothingGarmentTuningRow"
MAXIMUM_SAFE_OFFSET_CM = 0.35
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
RECEIPT_PATH = (
    SAVED_ROOT
    / "ClothingMorphV2QA"
    / "Director"
    / f"EFClothingMorphDirectorValidation_{STAMP}.json"
)
PROTECTED_RELATIVE_PATHS = (
    "Content/DazToUnreal/Female/Female.uasset",
    "Content/DazToUnreal/Male/Male.uasset",
    "Content/DazToUnreal/Multiple/Multiple.uasset",
    "Content/DazToUnreal/Multiple/Multiple_Skeleton.uasset",
    "Content/DazToUnreal/UnderWearPanty/UnderWearPanty.uasset",
    "Content/DazToUnreal/UnderWearPanty/UnderWearPanty_Skeleton.uasset",
    "Content/FullSample/Player.uasset",
    "Content/_Game/Data/EFClothingMorph/DT_EFClothingGarments.uasset",
    "Content/_Game/Data/EFClothingMorph/DT_EFClothingGarmentTuning.uasset",
    "Content/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.uasset",
    "Content/_Generated/EFClothingMorphV2/DA_EFClothingFitRegistry.uasset",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def snapshot(paths: tuple[str, ...]) -> dict[str, dict[str, int | str]]:
    result = {}
    for relative_path in paths:
        absolute_path = (PROJECT_ROOT / relative_path).resolve()
        if not absolute_path.is_file() or PROJECT_ROOT not in absolute_path.parents:
            fail("Missing or unsafe protected path: " + str(absolute_path))
        result[relative_path] = {
            "size_bytes": int(absolute_path.stat().st_size),
            "sha256": sha256(absolute_path),
        }
    return result


def object_path(value) -> str:
    if value is None:
        return ""
    getter = getattr(value, "get_path_name", None)
    if callable(getter):
        try:
            return str(getter())
        except Exception:
            pass
    return str(value)


def canonical_asset_path(value) -> str:
    text = object_path(value)
    match = re.search(r"(/Game/[A-Za-z0-9_./-]+)", text)
    return match.group(1).split(".", 1)[0] if match else ""


def property_value(owner, property_name: str):
    candidates = [property_name]
    if property_name.startswith("b") and len(property_name) > 1:
        property_name_without_bool_prefix = (
            property_name[1].lower() + property_name[2:]
        )
        candidates.append(property_name_without_bool_prefix)
    else:
        property_name_without_bool_prefix = property_name
    snake = []
    for character in property_name:
        if character.isupper() and snake:
            snake.append("_")
        snake.append(character.lower())
    candidates.append("".join(snake))
    if property_name_without_bool_prefix != property_name:
        bool_snake = []
        for character in property_name_without_bool_prefix:
            if character.isupper() and bool_snake:
                bool_snake.append("_")
            bool_snake.append(character.lower())
        candidates.append("".join(bool_snake))
    for candidate in dict.fromkeys(candidates):
        try:
            return owner.get_editor_property(candidate)
        except Exception:
            pass
    fail(f"{object_path(owner)} is missing property {property_name}")


def export_rows(table) -> list[dict]:
    exported = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(exported, tuple):
        if len(exported) != 2 or not bool(exported[0]):
            fail("Could not export DataTable " + object_path(table))
        exported = exported[1]
    rows = json.loads(str(exported))
    if not isinstance(rows, list):
        fail("DataTable JSON is not a row list: " + object_path(table))
    return rows


def field(row: dict, *names: str):
    lowered = {str(key).casefold(): key for key in row}
    for name in names:
        key = lowered.get(name.casefold())
        if key is not None:
            return row[key]
    fail(f"DataTable row lacks {names}: {row}")


def row_name(row: dict) -> str:
    return str(field(row, "Name", "RowName"))


def policy_validation(policy) -> str:
    boolean_validator = getattr(policy, "is_policy_valid", None)
    error_getter = getattr(policy, "get_policy_validation_error", None)
    if callable(boolean_validator) and callable(error_getter):
        if bool(boolean_validator()):
            return ""
        detail = str(error_getter())
        return detail if detail else "IsPolicyValid returned false"

    validator = getattr(policy, "validate_policy", None)
    if not callable(validator):
        fail("Director does not expose native ValidatePolicy")
    result = validator()
    if isinstance(result, tuple):
        if not result or not bool(result[0]):
            detail = str(result[1]) if len(result) > 1 else ""
            return detail if detail else repr(result)
        return ""
    return "" if result is True else repr(result)


def load_latest_compiler_receipt() -> dict:
    candidates = sorted(
        (SAVED_ROOT / "ClothingMorphV2QA").glob("compiler_receipt_FullCatalog_V26_*.json"),
        key=lambda path: path.stat().st_mtime_ns,
        reverse=True,
    )
    if not candidates:
        fail("No V26 full-catalog compiler receipt exists")
    receipt_path = candidates[0]
    payload = json.loads(receipt_path.read_text(encoding="utf-8"))
    required = (
        payload.get("status") == "UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_PASS"
        and bool(payload.get("success"))
        and bool(payload.get("catalog_equality_gate"))
        and bool(payload.get("protected_inputs_unchanged"))
    )
    counts = [
        int(payload.get(key, -1))
        for key in (
            "enabled_row_count",
            "valid_profile_count",
            "valid_binding_count",
            "tested_row_count",
            "passed_row_count",
        )
    ]
    if not required or counts[0] < 1 or len(set(counts)) != 1:
        fail("Latest V26 compiler receipt is not an equality PASS: " + str(receipt_path))
    return {
        "path": str(receipt_path),
        "sha256": sha256(receipt_path),
        "counts": counts,
        "registry": str(payload.get("registry", "")),
    }


def write_receipt(payload: dict) -> None:
    receipt_root = RECEIPT_PATH.parent.resolve()
    if SAVED_ROOT not in receipt_root.parents and receipt_root != SAVED_ROOT:
        fail("Receipt escaped Saved")
    receipt_root.mkdir(parents=True, exist_ok=True)
    temporary = RECEIPT_PATH.with_suffix(".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, RECEIPT_PATH)


result = {
    "schema": "EFClothingMorph.Director.Validation.1",
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "status": "FAIL",
    "success": False,
    "director": DIRECTOR_PATH,
    "compile_catalog": COMPILE_CATALOG_PATH,
    "runtime_tuning_catalog": TUNING_CATALOG_PATH,
    "registry": REGISTRY_PATH,
}

try:
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Director validation requires Unreal Engine 5.8")
    if PROJECT_ROOT.name.casefold() != "noshellforwinter":
        fail("Wrong project root: " + str(PROJECT_ROOT))

    result["protected_before"] = snapshot(PROTECTED_RELATIVE_PATHS)
    director_class = unreal.load_class(None, DIRECTOR_CLASS_PATH)
    settings_class = unreal.load_class(None, SETTINGS_CLASS_PATH)
    compile_struct = unreal.load_object(None, COMPILE_STRUCT_PATH)
    tuning_struct = unreal.load_object(None, TUNING_STRUCT_PATH)
    if None in (director_class, settings_class, compile_struct, tuning_struct):
        fail("A required native Director class or DataTable row struct did not load")

    director = unreal.load_asset(DIRECTOR_PATH)
    compile_table = unreal.load_asset(COMPILE_CATALOG_PATH)
    tuning_table = unreal.load_asset(TUNING_CATALOG_PATH)
    registry = unreal.load_asset(REGISTRY_PATH)
    if None in (director, compile_table, tuning_table, registry):
        fail("Director startup asset failed to load")
    if director.get_class() != director_class:
        fail("Director asset has the wrong native class")
    if unreal.DataTableFunctionLibrary.get_data_table_row_struct(compile_table) != compile_struct:
        fail("Compile catalog has the wrong native row struct")
    if unreal.DataTableFunctionLibrary.get_data_table_row_struct(tuning_table) != tuning_struct:
        fail("Runtime tuning catalog has the wrong native row struct")

    policy_error = policy_validation(director)
    if policy_error:
        fail("Native Director validation failed: " + policy_error)

    settings = unreal.get_default_object(settings_class)
    configured_compile = canonical_asset_path(property_value(settings, "GarmentCatalog"))
    configured_tuning = canonical_asset_path(property_value(settings, "GarmentTuningCatalog"))
    configured_director = canonical_asset_path(property_value(settings, "DirectorPolicy"))
    director_compile = canonical_asset_path(property_value(director, "CompileCatalog"))
    director_tuning = canonical_asset_path(property_value(director, "RuntimeTuningCatalog"))
    expected_paths = {
        "settings_director": configured_director,
        "settings_compile": configured_compile,
        "settings_tuning": configured_tuning,
        "director_compile": director_compile,
        "director_tuning": director_tuning,
    }
    if expected_paths != {
        "settings_director": DIRECTOR_PATH,
        "settings_compile": COMPILE_CATALOG_PATH,
        "settings_tuning": TUNING_CATALOG_PATH,
        "director_compile": COMPILE_CATALOG_PATH,
        "director_tuning": TUNING_CATALOG_PATH,
    }:
        fail("Settings/Director asset reference contract differs: " + repr(expected_paths))

    compile_rows = export_rows(compile_table)
    tuning_rows = export_rows(tuning_table)
    compile_indices = {row_name(row) for row in compile_rows}
    tuning_indices = {row_name(row) for row in tuning_rows}
    if not compile_indices or compile_indices != tuning_indices:
        fail(
            "Catalog/tuning indices differ: compile="
            + repr(sorted(compile_indices))
            + " tuning="
            + repr(sorted(tuning_indices))
        )

    enabled_tuning = bool(property_value(director, "bEnableRuntimeTuning"))
    maximum_offset = float(property_value(director, "MaximumAdditionalClearanceCm"))
    if (
        not enabled_tuning
        or not math.isfinite(maximum_offset)
        or maximum_offset < 0.0
        or maximum_offset > MAXIMUM_SAFE_OFFSET_CM
    ):
        fail("Director runtime tuning switch or maximum safe offset is invalid")
    tuning_offsets = {}
    for row in tuning_rows:
        index = row_name(row)
        offset = float(field(row, "AdditionalClearanceCm"))
        if not math.isfinite(offset) or offset < 0.0 or offset > maximum_offset:
            fail(f"Tuning row {index} has unsafe Extra Surface Offset: {offset}")
        tuning_offsets[index] = offset

    result["policy_validation"] = "PASS"
    result["runtime_tuning_enabled"] = enabled_tuning
    result["maximum_additional_clearance_cm"] = maximum_offset
    result["catalog_indices"] = sorted(compile_indices)
    result["tuning_offsets_cm"] = tuning_offsets
    result["compiler_receipt"] = load_latest_compiler_receipt()
    result["dirty_content_packages"] = [
        object_path(package)
        for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    ]
    if result["dirty_content_packages"]:
        fail("Read-only Director validation dirtied a content package")
    result["protected_after"] = snapshot(PROTECTED_RELATIVE_PATHS)
    result["protected_inputs_unchanged"] = (
        result["protected_before"] == result["protected_after"]
    )
    if not result["protected_inputs_unchanged"]:
        fail("Read-only Director validation changed a protected asset")
    result["status"] = "PASS"
    result["success"] = True
    write_receipt(result)
    unreal.log("EF_CLOTHING_MORPH_DIRECTOR_VALIDATION_RECEIPT=" + str(RECEIPT_PATH))
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
