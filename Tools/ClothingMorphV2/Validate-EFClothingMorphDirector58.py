"""Read-only validation for the schema-2 single Clothing Director.

The validator intentionally ignores the retired schema-1 DataTables.  It checks
the one public Director, its stable garment IDs and safe offsets, the internal
compiled registry and the latest schema-2 compiler receipt.  No package is
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
DIRECTOR_SCHEMA = 2
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
        fail("Director schema is not 2")
    validator = getattr(director, "is_policy_valid", None)
    error_getter = getattr(director, "get_policy_validation_error", None)
    if not callable(validator) or not callable(error_getter):
        fail("Director does not expose schema-2 validation functions")
    if not bool(validator()):
        fail("Director policy validation failed: " + str(error_getter()))


def validate_internal_path(value, label: str) -> str:
    path = canonical_asset_path(value)
    if not path.startswith(INTERNAL_ROOT + "/"):
        fail(f"{label} escaped internal compiled content: {path!r}")
    return path


def load_latest_compiler_receipt(expected_ids, enabled_ids, registry_path) -> dict:
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
        if payload.get("status") != "UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_PASS":
            continue
        if canonical_asset_path(payload.get("director")) != DIRECTOR_PATH:
            continue
        if payload.get("output_root") != INTERNAL_ROOT:
            continue
        if sorted(str(value) for value in payload.get("garment_ids", [])) != sorted(expected_ids):
            continue
        if sorted(str(value) for value in payload.get("enabled_garment_ids", [])) != sorted(enabled_ids):
            continue
        if canonical_asset_path(payload.get("registry")) != registry_path:
            continue
        if not payload.get("catalog_equality_gate") or not payload.get(
            "protected_inputs_unchanged"
        ):
            continue
        payload = dict(payload)
        payload["path"] = path
        return payload
    fail("No successful schema-2 Director compiler receipt matches the current policy")


def main() -> None:
    result = {
        "schema": "EFClothingMorph.Director.Validation.2",
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
        offsets = {}
        pairs = {}
        for garment in garments:
            index = garment_id(garment)
            if index in ids:
                fail("Duplicate Garment Id: " + index)
            ids.append(index)
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
            if not boolean(get_property(garment, "enabled")):
                continue
            source = canonical_asset_path(get_property(garment, "source_garment"))
            body = canonical_asset_path(get_property(garment, "body_surface"))
            if not source or not body:
                fail(f"Enabled garment {index} has no source/body pair")
            pair = source + "|" + body
            if pair in pairs:
                fail(f"Garments {pairs[pair]} and {index} duplicate a source/body pair")
            pairs[pair] = index
            enabled_ids.append(index)

        if not ids or not enabled_ids:
            fail("Director requires at least one garment and one enabled garment")

        profiles = list(get_property(registry, "profiles"))
        if len(profiles) != len(enabled_ids):
            fail(
                f"Internal registry profile count {len(profiles)} does not match enabled Director entries {len(enabled_ids)}"
            )
        registry_ids = []
        native_profile_reports = {}
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
        result["per_garment_runtime_offsets_only"] = True
        result["retired_global_director_controls_absent"] = True
        result["runtime_offset_limit_cm"] = maximum_offset
        result["garment_offsets_cm"] = offsets
        result["registry_profile_count"] = len(profiles)
        result["native_profile_reports"] = native_profile_reports
        result["compiler_receipt"] = load_latest_compiler_receipt(
            ids, enabled_ids, REGISTRY_PATH
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
