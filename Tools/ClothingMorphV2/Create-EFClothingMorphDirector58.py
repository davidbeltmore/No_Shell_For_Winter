"""Create or migrate the single-authority EF Clothing Morph V2 Director.

Schema 2 deliberately exposes one human-authored asset:

    /Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector

When the two schema-1 DataTables still exist, this commandlet reads them and
merges rows by DataTable RowName into Director.Garments.  It never modifies or
deletes either legacy table.  A populated, valid schema-2 Director is treated
as authoritative and is only validated; authored garment values are not reset.

No SkeletalMesh, USkeleton, Player, DAZ/ACFU asset or generated fit artifact is
written by this migration.
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
CONTENT_DIR = os.path.realpath(unreal.Paths.project_content_dir())
SAVED_DIR = os.path.realpath(unreal.Paths.project_saved_dir())
PUBLIC_DIRECTORY = "/Game/_Game/Data/EFClothingMorph"
DIRECTOR_PATH = PUBLIC_DIRECTORY + "/DA_EFClothingMorphDirector"
LEGACY_COMPILE_PATH = PUBLIC_DIRECTORY + "/DT_EFClothingGarments"
LEGACY_TUNING_PATH = PUBLIC_DIRECTORY + "/DT_EFClothingGarmentTuning"
DIRECTOR_CLASS_PATH = "/Script/EFClothingMorphRuntime.EFClothingMorphDirectorPolicy"
DIRECTOR_SCHEMA = 2
MAXIMUM_SAFE_OFFSET_CM = 0.35
STAMP = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
RECEIPT_PATH = os.path.join(
    SAVED_DIR,
    "ClothingMorphV2QA",
    "Director",
    f"EFClothingMorphDirector_{STAMP}.json",
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
        fail("Legacy asset escaped /Game: " + asset_path)
    relative = asset_path[len("/Game/") :].replace("/", os.sep) + ".uasset"
    absolute = os.path.realpath(os.path.join(CONTENT_DIR, relative))
    if os.path.commonpath((absolute, CONTENT_DIR)).lower() != CONTENT_DIR.lower():
        fail("Legacy asset file escaped Content: " + absolute)
    return absolute


def optional_asset_hash(asset_path: str) -> dict | None:
    path = package_file(asset_path)
    if not os.path.isfile(path):
        return None
    return {"file": path, "size_bytes": os.path.getsize(path), "sha256": sha256(path)}


def package_is_dirty(asset) -> bool:
    package_path = asset.get_outermost().get_path_name()
    dirty_packages = list(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    ) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return any(package.get_path_name() == package_path for package in dirty_packages)


def export_table(table) -> list[dict]:
    exported = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    if isinstance(exported, tuple):
        if len(exported) != 2 or not bool(exported[0]):
            fail("DataTable export failed: " + repr(exported))
        exported = exported[1]
    if not isinstance(exported, str):
        fail("Unexpected DataTable export result: " + repr(exported))
    rows = json.loads(exported)
    if not isinstance(rows, list):
        fail("DataTable JSON root is not an array: " + table.get_path_name())
    return rows


def field_key(row: dict, *candidates: str) -> str:
    folded = {str(key).casefold(): key for key in row}
    for candidate in candidates:
        result = folded.get(candidate.casefold())
        if result is not None:
            return result
    fail("Missing field {} in {}".format(candidates, sorted(row)))


def optional_field(row: dict, *candidates: str, default=None):
    try:
        return row[field_key(row, *candidates)]
    except RuntimeError:
        return default


def row_name(row: dict) -> str:
    value = str(optional_field(row, "Name", "RowName", default="")).strip()
    if not value or value.casefold() == "none":
        fail("Legacy DataTable contains an empty RowName")
    return value


def boolean(value, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    return str(value).strip().casefold() not in {"0", "false", "no", "off", "disabled"}


def numeric(value, default: float = 0.0) -> float:
    try:
        converted = float(value)
    except (TypeError, ValueError):
        return default
    return converted if math.isfinite(converted) else default


def friendly_name(index: str) -> str:
    return index.replace("_", " ").replace("UnderWear", "Underwear")


def canonical_object_reference(value) -> str:
    text = str(value or "").strip()
    match = re.search(r"/(?:Game|EFClothingMorph)/[A-Za-z0-9_./-]+", text)
    if not match:
        return ""
    result = match.group(0).rstrip("'\"")
    return result.split(".", 1)[0]


def load_asset_reference(value, label: str):
    path = canonical_object_reference(value)
    if not path:
        return None
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        fail(f"Could not load {label}: {path}")
    return asset


def enum_value(enum_name: str, value, mapping: dict[str, str]):
    enum_class = getattr(unreal, enum_name, None)
    if enum_class is None:
        fail("Missing reflected enum unreal." + enum_name)
    raw = str(value or "").split("::")[-1]
    compact = re.sub(r"[^A-Za-z0-9]", "", raw).casefold()
    member_name = mapping.get(compact)
    if not member_name or not hasattr(enum_class, member_name):
        fail(f"Unsupported {enum_name} value: {value!r}")
    return getattr(enum_class, member_name)


def gameplay_tag_container(value):
    if isinstance(value, dict):
        source = json.dumps(value, ensure_ascii=False)
    elif isinstance(value, list):
        source = " ".join(str(item) for item in value)
    else:
        source = str(value or "")
    tags = set(
        re.findall(
            r"(?:TagName\s*[=:]\s*[\"']?|\b)([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+)",
            source,
        )
    )
    if not tags:
        return unreal.GameplayTagContainer()
    compiler = getattr(unreal, "EFClothingFitCompilerLibrary", None)
    if compiler is None:
        fail("Missing EFClothingFitCompilerLibrary coverage-tag import bridge")
    container = compiler.make_gameplay_tag_container_from_names(
        [unreal.Name(tag) for tag in sorted(tags)]
    )
    imported_count = len(list(container.get_editor_property("gameplay_tags")))
    if imported_count != len(tags):
        fail(
            "One or more legacy coverage tags are absent from the project tag dictionary: "
            + ", ".join(sorted(tags))
        )
    return container


def string_array(value) -> list[str]:
    if value is None:
        return []
    if isinstance(value, list):
        return [str(item) for item in value if str(item).strip()]
    text = str(value).strip()
    if not text or text in {"()", "[]"}:
        return []
    quoted = re.findall(r'[\"\']([^\"\']+)[\"\']', text)
    if quoted:
        return quoted
    return [item.strip() for item in text.strip("()[]").split(",") if item.strip()]


def set_property(target, name: str, value) -> None:
    try:
        target.set_editor_property(name, value)
    except Exception as exc:
        fail(f"Could not set {name} on {type(target).__name__}: {exc}")


def make_director_garment(compile_row: dict, tuning_row: dict | None):
    row_type = getattr(unreal, "EFClothingGarmentRow", None)
    if row_type is None:
        fail("Missing reflected struct unreal.EFClothingGarmentRow")
    garment = row_type()
    index = row_name(compile_row)

    legacy_offset = numeric(optional_field(compile_row, "RuntimeOffsetCm", default=0.0))
    tuning_offset = numeric(
        optional_field(tuning_row or {}, "AdditionalClearanceCm", default=legacy_offset)
    )
    tuning_offset = max(0.0, min(tuning_offset, MAXIMUM_SAFE_OFFSET_CM))

    backend = enum_value(
        "EFClothingSurfaceBackend",
        optional_field(compile_row, "Backend", default="SurfaceWrapGPU"),
        {
            "geometryfitfallback": "GEOMETRY_FIT_FALLBACK",
            "surfacewrapgpu": "SURFACE_WRAP_GPU",
            "disabled": "DISABLED",
            "0": "GEOMETRY_FIT_FALLBACK",
            "1": "SURFACE_WRAP_GPU",
            "2": "DISABLED",
        },
    )
    fit_policy = enum_value(
        "EFClothingFitPolicy",
        optional_field(compile_row, "FitPolicy", default="Auto"),
        {
            "auto": "AUTO",
            "tight": "TIGHT",
            "hybrid": "HYBRID",
            "loose": "LOOSE",
            "rigid": "RIGID",
            "0": "AUTO",
            "1": "TIGHT",
            "2": "HYBRID",
            "3": "LOOSE",
            "4": "RIGID",
        },
    )

    set_property(garment, "garment_id", index)
    set_property(
        garment,
        "display_name",
        str(optional_field(compile_row, "DisplayName", default="")).strip()
        or friendly_name(index),
    )
    set_property(
        garment,
        "enabled",
        boolean(optional_field(compile_row, "bEnabled", "Enabled", default=True), True),
    )
    set_property(
        garment,
        "source_garment",
        load_asset_reference(optional_field(compile_row, "SourceGarment"), index + " SourceGarment"),
    )
    set_property(
        garment,
        "body_surface",
        load_asset_reference(optional_field(compile_row, "BodySurface"), index + " BodySurface"),
    )
    set_property(garment, "backend", backend)
    set_property(garment, "fit_policy", fit_policy)
    set_property(
        garment,
        "coverage_tags",
        gameplay_tag_container(optional_field(compile_row, "CoverageTags", default="")),
    )
    set_property(
        garment,
        "hidden_body_material_slots",
        string_array(optional_field(compile_row, "HiddenBodyMaterialSlots", default=[])),
    )
    set_property(
        garment,
        "excluded_body_surface_material_slots",
        string_array(optional_field(compile_row, "ExcludedBodySurfaceMaterialSlots", default=[])),
    )
    set_property(
        garment,
        "excluded_body_bone_branches",
        string_array(optional_field(compile_row, "ExcludedBodyBoneBranches", default=[])),
    )
    set_property(
        garment,
        "excluded_body_morph_prefixes",
        string_array(optional_field(compile_row, "ExcludedBodyMorphPrefixes", default=[])),
    )
    set_property(
        garment,
        "minimum_clearance_multiplier",
        numeric(optional_field(compile_row, "MinimumClearanceMultiplier", default=1.0), 1.0),
    )
    set_property(
        garment,
        "fabric_clearance_cm",
        numeric(optional_field(compile_row, "FabricClearanceCm", default=-1.0), -1.0),
    )
    set_property(
        garment,
        "enable_runtime_tuning",
        boolean(
            optional_field(tuning_row or {}, "bEnableTuning", "bEnableRuntimeTuning", default=True),
            True,
        ),
    )
    set_property(garment, "additional_clearance_cm", tuning_offset)
    set_property(
        garment,
        "notes",
        str(optional_field(tuning_row or {}, "Notes", default="")),
    )
    set_property(
        garment,
        "maximum_correction_cm",
        numeric(optional_field(compile_row, "MaximumCorrectionCm", default=-1.0), -1.0),
    )
    set_property(
        garment,
        "fail_closed_on_missing_lod",
        boolean(optional_field(compile_row, "bFailClosedOnMissingLOD", default=True), True),
    )
    return garment


def ensure_director(policy_class, result: dict):
    policy = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    if policy is not None:
        if policy.get_class() != policy_class:
            fail(DIRECTOR_PATH + " exists with the wrong native class")
        if package_is_dirty(policy):
            fail(DIRECTOR_PATH + " is dirty; save or discard the editor state first")
        return policy, False

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", policy_class)
    unreal.EditorAssetLibrary.make_directory(PUBLIC_DIRECTORY)
    policy = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        DIRECTOR_PATH.rsplit("/", 1)[-1], PUBLIC_DIRECTORY, policy_class, factory
    )
    if policy is None:
        fail("Could not create EF Clothing Morph Director")
    result["created_assets"].append(DIRECTOR_PATH)
    return policy, True


def validate_policy(policy) -> str:
    validator = getattr(policy, "is_policy_valid", None)
    error_getter = getattr(policy, "get_policy_validation_error", None)
    if callable(validator) and callable(error_getter):
        if bool(validator()):
            return ""
        return str(error_getter()) or "IsPolicyValid returned false"
    fail("Director does not expose the schema-2 validation API")


def garment_id(garment) -> str:
    return str(garment.get_editor_property("garment_id"))


def main() -> None:
    result = {
        "schema": "EFClothingMorph.Director.2",
        "status": "FAIL",
        "success": False,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "director": DIRECTOR_PATH,
        "legacy_compile_catalog": LEGACY_COMPILE_PATH,
        "legacy_tuning_catalog": LEGACY_TUNING_PATH,
        "legacy_assets_deleted": False,
        "created_assets": [],
        "migration_mode": "unresolved",
        "errors": [],
    }
    try:
        if os.path.basename(PROJECT_FILE).casefold() != "noshellforwinter.uproject":
            fail("Director migration is restricted to NoShellForWinter.uproject.")
        if os.path.basename(PROJECT_DIR.rstrip("\\/")).casefold() != "noshellforwinter":
            fail("Director migration is outside the writable NoShellForWinter target.")
        engine_version = str(unreal.SystemLibrary.get_engine_version())
        if not engine_version.startswith("5.8."):
            fail("Director migration requires Unreal Engine 5.8; got " + engine_version)
        result["engine_version"] = engine_version
        result["protected_before"] = snapshot(PROTECTED_RELATIVE_PATHS)
        result["legacy_hashes_before"] = {
            LEGACY_COMPILE_PATH: optional_asset_hash(LEGACY_COMPILE_PATH),
            LEGACY_TUNING_PATH: optional_asset_hash(LEGACY_TUNING_PATH),
        }
        policy_class = unreal.load_class(None, DIRECTOR_CLASS_PATH)
        if policy_class is None:
            fail("Could not load native EF Clothing Morph Director class")
        policy, created_policy = ensure_director(policy_class, result)

        existing_schema = int(policy.get_editor_property("schema_version"))
        existing_garments = list(policy.get_editor_property("garments"))
        changed = False
        if existing_schema == DIRECTOR_SCHEMA and existing_garments:
            result["migration_mode"] = "validate_existing_schema2"
        else:
            compile_table = unreal.EditorAssetLibrary.load_asset(LEGACY_COMPILE_PATH)
            tuning_table = unreal.EditorAssetLibrary.load_asset(LEGACY_TUNING_PATH)
            if not isinstance(compile_table, unreal.DataTable) or not isinstance(
                tuning_table, unreal.DataTable
            ):
                fail(
                    "Schema-2 Director is empty and both legacy DataTables are required for one-time migration"
                )
            if package_is_dirty(compile_table) or package_is_dirty(tuning_table):
                fail("A legacy DataTable is dirty; save or discard it before migration")

            compile_rows = export_table(compile_table)
            tuning_rows = export_table(tuning_table)
            if not compile_rows:
                fail("Legacy compile catalog has no rows")
            tuning_by_id = {row_name(row): row for row in tuning_rows}
            if len(tuning_by_id) != len(tuning_rows):
                fail("Legacy runtime tuning table has duplicate RowName values")
            compile_ids = [row_name(row) for row in compile_rows]
            if len(set(compile_ids)) != len(compile_ids):
                fail("Legacy compile table has duplicate RowName values")

            garments = []
            for compile_row in compile_rows:
                index = row_name(compile_row)
                garments.append(
                    make_director_garment(
                        compile_row,
                        tuning_by_id.get(index),
                    )
                )
            # Preserve valid schema-1 global tuning when upgrading an existing
            # Director.  Only a newly created asset receives defaults.
            previous_maximum = numeric(
                policy.get_editor_property("maximum_additional_clearance_cm"),
                MAXIMUM_SAFE_OFFSET_CM,
            )
            previous_maximum = max(
                0.0, min(previous_maximum, MAXIMUM_SAFE_OFFSET_CM)
            )
            previous_global = numeric(
                policy.get_editor_property("global_additional_clearance_cm"), 0.0
            )
            previous_global = max(0.0, min(previous_global, previous_maximum))
            previous_tuning_enabled = boolean(
                policy.get_editor_property("enable_runtime_tuning"), True
            )
            if not unreal.EFClothingFitCompilerLibrary.upgrade_director_identity_to_schema2(
                policy
            ):
                fail("Native Director schema gate rejected the 1 -> 2 migration")
            set_property(policy, "garments", garments)
            set_property(
                policy,
                "enable_runtime_tuning",
                True if created_policy else previous_tuning_enabled,
            )
            set_property(policy, "global_additional_clearance_cm", previous_global)
            set_property(policy, "maximum_additional_clearance_cm", previous_maximum)
            changed = True
            result["migration_mode"] = "create_from_legacy" if created_policy else "migrate_schema1_from_legacy"
            result["missing_legacy_tuning_rows_defaulted"] = sorted(
                set(compile_ids) - set(tuning_by_id)
            )
            result["orphan_legacy_tuning_rows_ignored"] = sorted(
                set(tuning_by_id) - set(compile_ids)
            )

        policy_error = validate_policy(policy)
        if policy_error:
            fail("Director policy validation failed: " + policy_error)
        garments = list(policy.get_editor_property("garments"))
        ids = [garment_id(garment) for garment in garments]
        if not ids or len(ids) != len(set(ids)):
            fail("Schema-2 Director Garment Id values are empty or duplicated")

        if changed and not unreal.EditorAssetLibrary.save_loaded_asset(
            policy, only_if_is_dirty=False
        ):
            fail("Could not save " + DIRECTOR_PATH)
        reloaded = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
        if reloaded is None or reloaded.get_class() != policy_class:
            fail("Director could not reload after migration")
        policy_error = validate_policy(reloaded)
        if policy_error:
            fail("Reloaded Director validation failed: " + policy_error)

        reloaded_garments = list(reloaded.get_editor_property("garments"))
        result["director_schema_version"] = int(
            reloaded.get_editor_property("schema_version")
        )
        result["garment_ids"] = sorted(garment_id(row) for row in reloaded_garments)
        result["garment_count"] = len(reloaded_garments)
        result["legacy_hashes_after"] = {
            LEGACY_COMPILE_PATH: optional_asset_hash(LEGACY_COMPILE_PATH),
            LEGACY_TUNING_PATH: optional_asset_hash(LEGACY_TUNING_PATH),
        }
        result["legacy_assets_retained_unchanged"] = (
            result["legacy_hashes_before"] == result["legacy_hashes_after"]
        )
        if not result["legacy_assets_retained_unchanged"]:
            fail("A legacy DataTable changed during migration")

        result["protected_after"] = snapshot(PROTECTED_RELATIVE_PATHS)
        result["protected_inputs_unchanged"] = (
            result["protected_before"] == result["protected_after"]
        )
        if not result["protected_inputs_unchanged"]:
            fail("A protected mesh, skeleton or Player asset changed")

        result["status"] = "PASS"
        result["success"] = True
        write_json(RECEIPT_PATH, result)
        unreal.log("EF_CLOTHING_MORPH_DIRECTOR_RECEIPT=" + RECEIPT_PATH)
    except Exception as exc:
        result["errors"].append(str(exc))
        result["traceback"] = traceback.format_exc()
        try:
            write_json(RECEIPT_PATH, result)
        except Exception as receipt_error:
            unreal.log_error("Could not write Director receipt: " + repr(receipt_error))
        unreal.log_error("EF Clothing Morph Director migration failed: " + repr(exc))
        raise


main()
