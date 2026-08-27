"""Create and validate the project-owned EF Clothing Morph V2 director assets.

The editor-facing Director mirrors the Calysto policy pattern while keeping the
existing compile catalog authoritative. It creates only project-owned assets:

* DA_EFClothingMorphDirector: central policy and authoring guide;
* DT_EFClothingGarmentTuning: topology-free per-catalog-index runtime offsets;
* updates DT_EFClothingGarments with friendly display metadata and migrates any
  legacy RuntimeOffsetCm value into the tuning table.

No SkeletalMesh, USkeleton, Player, DAZ, ACFU or generated fit asset is ever
modified. Rows are linked by the DataTable RowName, which is the stable index.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import traceback

import unreal


PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
CONTENT_DIR = os.path.realpath(unreal.Paths.project_content_dir())
SAVED_DIR = os.path.realpath(unreal.Paths.project_saved_dir())
DIRECTORY = "/Game/_Game/Data/EFClothingMorph"
COMPILE_CATALOG_PATH = DIRECTORY + "/DT_EFClothingGarments"
TUNING_CATALOG_PATH = DIRECTORY + "/DT_EFClothingGarmentTuning"
DIRECTOR_PATH = DIRECTORY + "/DA_EFClothingMorphDirector"
TUNING_STRUCT_PATH = "/Script/EFClothingMorphRuntime.EFClothingGarmentTuningRow"
COMPILE_STRUCT_PATH = "/Script/EFClothingMorphRuntime.EFClothingGarmentRow"
DIRECTOR_CLASS_PATH = "/Script/EFClothingMorphRuntime.EFClothingMorphDirectorPolicy"
MAXIMUM_SAFE_OFFSET_CM = 0.35
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
RECEIPT_PATH = os.path.join(
    SAVED_DIR, "ClothingMorphV2QA", "Director", f"EFClothingMorphDirector_{STAMP}.json"
)

PROTECTED_RELATIVE_PATHS = (
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


def write_json(path: str, payload) -> None:
    root = os.path.realpath(os.path.dirname(path))
    if os.path.commonpath((root, SAVED_DIR)).lower() != SAVED_DIR.lower():
        fail("Director receipt escaped Saved.")
    os.makedirs(root, exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False, sort_keys=True, default=str)
        handle.write("\n")
    os.replace(temporary, path)


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def snapshot(relative_paths: tuple[str, ...]) -> dict:
    result = {}
    for relative_path in relative_paths:
        absolute_path = os.path.realpath(os.path.join(PROJECT_DIR, relative_path))
        if not os.path.isfile(absolute_path):
            fail("Missing protected input: " + absolute_path)
        result[relative_path] = {
            "size_bytes": os.path.getsize(absolute_path),
            "sha256": sha256(absolute_path),
        }
    return result


def package_file(asset_path: str) -> str:
    if not asset_path.startswith("/Game/"):
        fail("Asset escaped /Game: " + asset_path)
    relative = asset_path[len("/Game/") :].replace("/", os.sep) + ".uasset"
    absolute = os.path.realpath(os.path.join(CONTENT_DIR, relative))
    if os.path.commonpath((absolute, CONTENT_DIR)).lower() != CONTENT_DIR.lower():
        fail("Asset file escaped Content: " + absolute)
    return absolute


def export_table(table) -> str:
    exported = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(exported, tuple):
        if len(exported) != 2 or not bool(exported[0]):
            fail("DataTable export failed: " + repr(exported))
        return str(exported[1])
    if not isinstance(exported, str):
        fail("Unexpected DataTable export result: " + repr(exported))
    return exported


def table_row_struct(table):
    return unreal.DataTableFunctionLibrary.get_data_table_row_struct(table)


def fill_table(table, rows: list[dict], row_struct) -> None:
    payload = json.dumps(rows, ensure_ascii=False)
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
        table, payload, row_struct
    ):
        fail("DataTable JSON import failed for " + table.get_path_name())


def field_key(row: dict, *candidates: str) -> str:
    folded = {str(key).casefold(): key for key in row}
    for candidate in candidates:
        result = folded.get(candidate.casefold())
        if result is not None:
            return result
    fail("Missing table field {} in {}".format(candidates, sorted(row)))


def optional_field_key(row: dict, *candidates: str) -> str | None:
    try:
        return field_key(row, *candidates)
    except RuntimeError:
        return None


def row_name(row: dict) -> str:
    return str(row[field_key(row, "Name", "RowName")])


def is_enabled(row: dict) -> bool:
    key = optional_field_key(row, "bEnabled", "Enabled")
    if key is None:
        return True
    value = row[key]
    return value if isinstance(value, bool) else str(value).strip().casefold() not in {
        "0",
        "false",
        "no",
        "disabled",
    }


def numeric(value, default: float = 0.0) -> float:
    try:
        converted = float(value)
    except (TypeError, ValueError):
        return default
    return converted if converted == converted and abs(converted) != float("inf") else default


def friendly_name(index: str) -> str:
    return index.replace("_", " ").replace("UnderWear", "Underwear")


def object_path(value) -> str:
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def package_is_dirty(asset) -> bool:
    package_path = asset.get_outermost().get_path_name()
    dirty_packages = list(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    ) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return any(package.get_path_name() == package_path for package in dirty_packages)


def load_struct(path: str):
    result = unreal.load_object(None, path)
    if result is None:
        fail("Could not load native row struct: " + path)
    return result


def ensure_table(path: str, row_struct, result: dict):
    table = unreal.EditorAssetLibrary.load_asset(path)
    if table is not None:
        if not isinstance(table, unreal.DataTable):
            fail(path + " exists but is not a DataTable")
        if table_row_struct(table) != row_struct:
            fail(path + " has the wrong row struct")
        if package_is_dirty(table):
            fail(path + " is dirty; refusing to overwrite an unsaved editor state")
        return table

    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct)
    name = path.rsplit("/", 1)[-1]
    unreal.EditorAssetLibrary.make_directory(DIRECTORY)
    table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, DIRECTORY, unreal.DataTable, factory
    )
    if table is None:
        fail("Could not create DataTable " + path)
    result["created_assets"].append(path)
    return table


def ensure_director(policy_class, result: dict):
    policy = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    if policy is not None:
        if policy.get_class() != policy_class:
            fail(DIRECTOR_PATH + " exists with the wrong native class")
        if package_is_dirty(policy):
            fail(DIRECTOR_PATH + " is dirty; refusing to overwrite an unsaved editor state")
        return policy, False

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", policy_class)
    unreal.EditorAssetLibrary.make_directory(DIRECTORY)
    policy = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        DIRECTOR_PATH.rsplit("/", 1)[-1], DIRECTORY, policy_class, factory
    )
    if policy is None:
        fail("Could not create Clothing Director DataAsset")
    result["created_assets"].append(DIRECTOR_PATH)
    return policy, True


def validate_policy(policy) -> str:
    boolean_validator = getattr(policy, "is_policy_valid", None)
    error_getter = getattr(policy, "get_policy_validation_error", None)
    if callable(boolean_validator) and callable(error_getter):
        if bool(boolean_validator()):
            return ""
        detail = str(error_getter())
        return detail if detail else "IsPolicyValid returned false"

    validation = policy.validate_policy()
    if isinstance(validation, tuple):
        if not validation or not bool(validation[0]):
            return str(validation[1]) if len(validation) > 1 else repr(validation)
        return ""
    if validation is False:
        return "ValidatePolicy returned false"
    return ""


def main() -> None:
    result = {
        "schema": "EFClothingMorph.Director.1",
        "status": "FAIL",
        "success": False,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "compile_catalog": COMPILE_CATALOG_PATH,
        "runtime_tuning_catalog": TUNING_CATALOG_PATH,
        "director": DIRECTOR_PATH,
        "created_assets": [],
        "migrated_legacy_offsets": [],
        "added_tuning_rows": [],
        "errors": [],
    }
    compile_table = None
    tuning_table = None
    compile_before_json = ""
    tuning_before_json = ""
    try:
        result["protected_before"] = snapshot(PROTECTED_RELATIVE_PATHS)
        compile_struct = load_struct(COMPILE_STRUCT_PATH)
        tuning_struct = load_struct(TUNING_STRUCT_PATH)
        policy_class = unreal.load_class(None, DIRECTOR_CLASS_PATH)
        if policy_class is None:
            fail("Could not load native Clothing Director class")

        compile_table = unreal.EditorAssetLibrary.load_asset(COMPILE_CATALOG_PATH)
        if compile_table is None or not isinstance(compile_table, unreal.DataTable):
            fail("Missing compile catalog " + COMPILE_CATALOG_PATH)
        if table_row_struct(compile_table) != compile_struct:
            fail("Compile catalog row struct is not FEFClothingGarmentRow")
        if package_is_dirty(compile_table):
            fail("Compile catalog is dirty; save/discard it before Director migration")
        compile_before_json = export_table(compile_table)
        compile_rows = json.loads(compile_before_json)
        original_compile_rows = json.loads(compile_before_json)
        if not isinstance(compile_rows, list) or not compile_rows:
            fail("Compile catalog has no rows")

        tuning_table = ensure_table(TUNING_CATALOG_PATH, tuning_struct, result)
        tuning_before_json = export_table(tuning_table)
        tuning_rows = json.loads(tuning_before_json)
        original_tuning_rows = json.loads(tuning_before_json)
        if not isinstance(tuning_rows, list):
            fail("Runtime tuning catalog JSON is not a list")
        tuning_by_name = {row_name(row): row for row in tuning_rows}
        if len(tuning_by_name) != len(tuning_rows):
            fail("Runtime tuning catalog has duplicate row indices")

        display_order = 100
        for catalog_row in compile_rows:
            index = row_name(catalog_row)
            display_name_key = optional_field_key(catalog_row, "DisplayName")
            if display_name_key is not None and not str(catalog_row.get(display_name_key, "")).strip():
                catalog_row[display_name_key] = friendly_name(index)
            display_order_key = optional_field_key(catalog_row, "DisplayOrder")
            if display_order_key is not None and int(numeric(catalog_row.get(display_order_key))) == 0:
                catalog_row[display_order_key] = display_order
            display_order += 100

            legacy_key = optional_field_key(catalog_row, "RuntimeOffsetCm")
            legacy_offset = numeric(catalog_row.get(legacy_key, 0.0)) if legacy_key else 0.0
            tuning_row = tuning_by_name.get(index)
            if tuning_row is None:
                tuning_row = {
                    "Name": index,
                    "bEnableTuning": True,
                    "AdditionalClearanceCm": max(0.0, min(legacy_offset, MAXIMUM_SAFE_OFFSET_CM)),
                    "Notes": "Runtime-only V2 clearance tuning. Row name matches the compile catalog index.",
                }
                tuning_rows.append(tuning_row)
                tuning_by_name[index] = tuning_row
                result["added_tuning_rows"].append(index)
            if legacy_key is not None and legacy_offset != 0.0:
                catalog_row[legacy_key] = 0.0
                result["migrated_legacy_offsets"].append(
                    {
                        "index": index,
                        "legacy_offset_cm": legacy_offset,
                        "applied_tuning_offset_cm": max(
                            0.0, min(legacy_offset, MAXIMUM_SAFE_OFFSET_CM)
                        ),
                    }
                )

        compile_changed = compile_rows != original_compile_rows
        tuning_changed = tuning_rows != original_tuning_rows
        if compile_changed:
            fill_table(compile_table, compile_rows, compile_struct)
        if tuning_changed:
            fill_table(tuning_table, tuning_rows, tuning_struct)

        policy, created_policy = ensure_director(policy_class, result)
        if created_policy:
            policy.set_editor_property("compile_catalog", compile_table)
            policy.set_editor_property("runtime_tuning_catalog", tuning_table)
            policy.set_editor_property("enable_runtime_tuning", True)
            policy.set_editor_property("global_additional_clearance_cm", 0.0)
            policy.set_editor_property("maximum_additional_clearance_cm", MAXIMUM_SAFE_OFFSET_CM)
            policy.set_editor_property("warn_on_orphan_tuning_rows", True)
        else:
            compile_reference = object_path(policy.get_editor_property("compile_catalog"))
            tuning_reference = object_path(policy.get_editor_property("runtime_tuning_catalog"))
            expected_compile_reference = object_path(compile_table)
            expected_tuning_reference = object_path(tuning_table)
            if (
                compile_reference != expected_compile_reference
                or tuning_reference != expected_tuning_reference
            ):
                fail(
                    "Existing Director references different catalogs; update it deliberately in the editor, "
                    "then rerun validation. "
                    f"compile={compile_reference!r} expected={expected_compile_reference!r}; "
                    f"tuning={tuning_reference!r} expected={expected_tuning_reference!r}"
                )

        policy_error = validate_policy(policy)
        if policy_error:
            fail("Director policy validation failed before save: " + policy_error)
        for asset, changed in (
            (compile_table, compile_changed),
            (tuning_table, tuning_changed),
            (policy, created_policy),
        ):
            if changed and not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                fail("Could not save " + asset.get_path_name())

        compile_table = unreal.EditorAssetLibrary.load_asset(COMPILE_CATALOG_PATH)
        tuning_table = unreal.EditorAssetLibrary.load_asset(TUNING_CATALOG_PATH)
        policy = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
        if compile_table is None or tuning_table is None or policy is None:
            fail("One or more director assets could not reload after save")
        policy_error = validate_policy(policy)
        if policy_error:
            fail("Director policy validation failed after save: " + policy_error)

        compile_names = {row_name(row) for row in json.loads(export_table(compile_table))}
        tuning_names = {row_name(row) for row in json.loads(export_table(tuning_table))}
        result["compile_row_indices"] = sorted(compile_names)
        result["tuning_row_indices"] = sorted(tuning_names)
        result["missing_tuning_indices"] = sorted(compile_names - tuning_names)
        result["orphan_tuning_indices"] = sorted(tuning_names - compile_names)
        if result["missing_tuning_indices"]:
            fail("Director failed to create tuning rows for " + repr(result["missing_tuning_indices"]))

        result["protected_after"] = snapshot(PROTECTED_RELATIVE_PATHS)
        result["protected_inputs_unchanged"] = (
            result["protected_before"] == result["protected_after"]
        )
        if not result["protected_inputs_unchanged"]:
            fail("A protected mesh, skeleton or Player asset changed during Director creation")

        result["status"] = "PASS"
        result["success"] = True
        write_json(RECEIPT_PATH, result)
        unreal.log("EF_CLOTHING_MORPH_DIRECTOR_RECEIPT=" + RECEIPT_PATH)
    except Exception as exc:
        result["errors"].append(str(exc))
        result["traceback"] = traceback.format_exc()
        # Restore existing tables in-memory whenever a later validation fails.
        for table, original_json in ((compile_table, compile_before_json), (tuning_table, tuning_before_json)):
            if table is not None and original_json:
                try:
                    fill_table(
                        table,
                        json.loads(original_json),
                        unreal.DataTableFunctionLibrary.get_data_table_row_struct(table),
                    )
                except Exception as rollback_error:
                    result["errors"].append("In-memory rollback failed: " + repr(rollback_error))
        try:
            write_json(RECEIPT_PATH, result)
        except Exception as receipt_error:
            unreal.log_error("Could not write Director receipt: " + repr(receipt_error))
        unreal.log_error("EF Clothing Morph Director creation failed: " + repr(exc))
        raise


main()
