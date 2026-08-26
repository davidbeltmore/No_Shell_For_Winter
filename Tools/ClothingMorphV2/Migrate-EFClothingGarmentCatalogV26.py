"""One-time, data-driven migration of enabled EF garment rows to V26 SurfaceWrapGPU.

The migration edits only the garment DataTable. It exports a byte-independent JSON
backup first, validates that no unrelated row fields changed, and saves only after
the complete in-memory table passes its contract.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import traceback

import unreal


CATALOG_PATH = "/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments"
SAVED_ROOT = os.path.realpath(
    os.path.join(unreal.Paths.project_saved_dir(), "ClothingMorphV2QA", "CatalogV26")
)
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
BACKUP_PATH = os.path.join(SAVED_ROOT, f"DT_EFClothingGarments_before_{STAMP}.json")
RECEIPT_PATH = os.path.join(SAVED_ROOT, f"CatalogV26Migration_{STAMP}.json")


def _sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def _catalog_file() -> str:
    return os.path.realpath(
        os.path.join(
            unreal.Paths.project_content_dir(),
            "_Game",
            "Data",
            "EFClothingMorph",
            "DT_EFClothingGarments.uasset",
        )
    )


def _export_json(table) -> str:
    result = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(result, tuple):
        if len(result) != 2 or not bool(result[0]):
            raise RuntimeError(f"DataTable JSON export failed: {result!r}")
        return str(result[1])
    if isinstance(result, str):
        return result
    raise RuntimeError(f"Unexpected DataTable JSON export result: {result!r}")


def _find_key(row: dict, *names: str) -> str:
    folded = {str(key).casefold(): key for key in row}
    for name in names:
        match = folded.get(name.casefold())
        if match is not None:
            return match
    raise RuntimeError(f"Missing expected catalog field {names!r} in {sorted(row)}")


def _optional_key(row: dict, *names: str) -> str | None:
    try:
        return _find_key(row, *names)
    except RuntimeError:
        return None


def _enabled(value) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().casefold() not in {"0", "false", "no", "disabled"}


def _enum_value(existing, member: str) -> str:
    text = str(existing)
    if "::" in text:
        return text.rsplit("::", 1)[0] + "::" + member
    return member


def _as_strings(value) -> list[str]:
    if value is None:
        return []
    if isinstance(value, list):
        return [str(item) for item in value]
    return [str(value)]


def _row_name(row: dict) -> str:
    key = _find_key(row, "Name", "RowName")
    return str(row[key])


def _without_allowed_fields(row: dict, allowed: set[str]) -> dict:
    return {key: value for key, value in row.items() if key.casefold() not in allowed}


def _write_json(path: str, payload) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False, sort_keys=True)
        handle.write("\n")
    os.replace(temporary, path)


def main() -> None:
    payload = {
        "schema": "EFClothingMorph.CatalogMigration.26.1",
        "status": "FAIL",
        "success": False,
        "catalog": CATALOG_PATH,
        "timestamp_utc": STAMP,
        "backup": BACKUP_PATH,
        "rows": [],
        "errors": [],
    }
    original_json = ""
    table = None
    try:
        table = unreal.EditorAssetLibrary.load_asset(CATALOG_PATH)
        if table is None or not isinstance(table, unreal.DataTable):
            raise RuntimeError("Garment catalog DataTable could not be loaded.")

        catalog_file = _catalog_file()
        payload["catalog_file"] = catalog_file
        payload["sha256_before"] = _sha256(catalog_file)
        original_json = _export_json(table)
        original_rows = json.loads(original_json)
        if not isinstance(original_rows, list) or not original_rows:
            raise RuntimeError("Catalog JSON did not contain any rows.")
        _write_json(BACKUP_PATH, original_rows)

        migrated_rows = json.loads(original_json)
        enabled_count = 0
        gp_prefix_row_count = 0
        allowed_field_names = {
            "backend",
            "fitpolicy",
            "fabricclearancecm",
            "runtimeoffsetcm",
            "maximumcorrectioncm",
            "bfailclosedonmissinglod",
            "failclosedonmissinglod",
            "excludedbodymorphprefixes",
        }
        for row in migrated_rows:
            enabled_key = _find_key(row, "bEnabled", "Enabled")
            if not _enabled(row[enabled_key]):
                continue
            enabled_count += 1

            backend_key = _find_key(row, "Backend")
            fit_policy_key = _find_key(row, "FitPolicy")
            fabric_key = _find_key(row, "FabricClearanceCm")
            runtime_offset_key = _find_key(row, "RuntimeOffsetCm")
            maximum_key = _find_key(row, "MaximumCorrectionCm")
            fail_closed_key = _find_key(
                row, "bFailClosedOnMissingLOD", "FailClosedOnMissingLOD"
            )
            prefixes_key = _find_key(row, "ExcludedBodyMorphPrefixes")

            row[backend_key] = _enum_value(row[backend_key], "SurfaceWrapGPU")
            row[fit_policy_key] = _enum_value(row[fit_policy_key], "Auto")
            row[fabric_key] = -1.0
            row[runtime_offset_key] = 0.0
            row[maximum_key] = -1.0
            row[fail_closed_key] = True

            hidden_key = _optional_key(row, "HiddenBodyMaterialSlots")
            excluded_slots_key = _optional_key(row, "ExcludedBodySurfaceMaterialSlots")
            branches_key = _optional_key(row, "ExcludedBodyBoneBranches")
            gp_evidence = []
            for key in (hidden_key, excluded_slots_key, branches_key):
                if key:
                    gp_evidence.extend(_as_strings(row[key]))
            evidence_text = "|".join(gp_evidence).casefold()
            if any(
                marker in evidence_text
                for marker in ("genesis9_gp_torso", "anus_01", "pelvis2", "rectum_01")
            ):
                prefixes = _as_strings(row[prefixes_key])
                for prefix in ("GPC_", "GPL_", "GPV_"):
                    if prefix not in prefixes:
                        prefixes.append(prefix)
                row[prefixes_key] = prefixes
                gp_prefix_row_count += 1

            payload["rows"].append(
                {
                    "name": _row_name(row),
                    "backend": str(row[backend_key]),
                    "fit_policy": str(row[fit_policy_key]),
                    "golden_palace_prefixes": list(_as_strings(row[prefixes_key])),
                }
            )

        if enabled_count <= 0:
            raise RuntimeError("Catalog has no enabled rows to migrate.")

        row_struct = unreal.DataTableFunctionLibrary.get_data_table_row_struct(table)
        migrated_json = json.dumps(migrated_rows, ensure_ascii=False)
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
            table, migrated_json, row_struct
        ):
            raise RuntimeError("Fill Data Table from JSON String returned false.")

        verified_rows = json.loads(_export_json(table))
        original_by_name = {_row_name(row): row for row in original_rows}
        verified_by_name = {_row_name(row): row for row in verified_rows}
        if set(original_by_name) != set(verified_by_name):
            raise RuntimeError("Migration changed the catalog row-name set.")
        for name, original in original_by_name.items():
            verified = verified_by_name[name]
            if _without_allowed_fields(original, allowed_field_names) != _without_allowed_fields(
                verified, allowed_field_names
            ):
                raise RuntimeError(f"Migration changed unrelated fields in row {name}.")

        surface_wrap_count = 0
        for row in verified_rows:
            if not _enabled(row[_find_key(row, "bEnabled", "Enabled")]):
                continue
            backend = str(row[_find_key(row, "Backend")])
            if backend.rsplit("::", 1)[-1] != "SurfaceWrapGPU":
                raise RuntimeError(f"Enabled row {_row_name(row)} is not SurfaceWrapGPU.")
            if not bool(
                row[_find_key(row, "bFailClosedOnMissingLOD", "FailClosedOnMissingLOD")]
            ):
                raise RuntimeError(f"Enabled row {_row_name(row)} is not fail-closed.")
            surface_wrap_count += 1

        if surface_wrap_count != enabled_count:
            raise RuntimeError("Not every enabled row migrated to SurfaceWrapGPU.")
        if not unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False):
            raise RuntimeError("Failed to save the migrated garment catalog.")

        payload["enabled_rows"] = enabled_count
        payload["surface_wrap_rows"] = surface_wrap_count
        payload["golden_palace_prefix_rows"] = gp_prefix_row_count
        payload["sha256_after"] = _sha256(catalog_file)
        payload["status"] = "PASS"
        payload["success"] = True
        _write_json(RECEIPT_PATH, payload)
        unreal.log("EF_CLOTHING_CATALOG_V26_MIGRATION_RECEIPT=" + RECEIPT_PATH)
    except Exception as exc:
        payload["errors"].append(str(exc))
        payload["traceback"] = traceback.format_exc()
        if table is not None and original_json:
            try:
                row_struct = unreal.DataTableFunctionLibrary.get_data_table_row_struct(table)
                unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
                    table, original_json, row_struct
                )
            except Exception as rollback_error:
                payload["errors"].append("In-memory rollback failed: " + repr(rollback_error))
        _write_json(RECEIPT_PATH, payload)
        unreal.log_error("EF Clothing catalog V26 migration failed: " + repr(exc))
        raise


main()
