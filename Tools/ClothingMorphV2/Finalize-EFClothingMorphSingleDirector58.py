"""Finalize EF Clothing Morph V2's single-Director cutover safely.

This commandlet is intentionally narrow and fail-closed. It validates the
schema-3 Director and the newly compiled internal V26 registry, audits every
legacy referencer, then retires only the two legacy DataTables and the old
generated output root through Unreal's EditorAssetLibrary. It never deletes a
live Unreal package through the filesystem.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import traceback
from pathlib import Path

import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()

DIRECTOR_PATH = "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector"
DIRECTOR_CLASS_PATH = "/Script/EFClothingMorphRuntime.EFClothingMorphDirectorPolicy"
PUBLIC_AUTHORING_ROOT = "/Game/_Game/Data/EFClothingMorph"
LEGACY_COMPILE_TABLE = PUBLIC_AUTHORING_ROOT + "/DT_EFClothingGarments"
LEGACY_TUNING_TABLE = PUBLIC_AUTHORING_ROOT + "/DT_EFClothingGarmentTuning"
LEGACY_TABLES = (LEGACY_COMPILE_TABLE, LEGACY_TUNING_TABLE)

INTERNAL_COMPILED_ROOT = "/EFClothingMorph/_Internal/Compiled/V26"
INTERNAL_REGISTRY_PATH = INTERNAL_COMPILED_ROOT + "/DA_EFClothingFitRegistry"
REGISTRY_CLASS_PATH = "/Script/EFClothingMorphRuntime.EFClothingFitRegistry"
LEGACY_GENERATED_ROOT = "/Game/_Generated/EFClothingMorphV2"

APPLY_ENV = "EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE"
RECEIPT_ENV = "EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE_RECEIPT"

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


def path_is_within(path: Path, root: Path) -> bool:
    try:
        return os.path.commonpath(
            (os.path.normcase(str(path.resolve())), os.path.normcase(str(root.resolve())))
        ) == os.path.normcase(str(root.resolve()))
    except ValueError:
        return False


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def protected_snapshot() -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for relative in PROTECTED_RELATIVE_PATHS:
        path = (PROJECT_ROOT / relative).resolve()
        if not path_is_within(path, PROJECT_ROOT) or not path.is_file():
            fail("Missing or unsafe protected input: " + str(path))
        result[relative] = {
            "path": str(path),
            "size_bytes": int(path.stat().st_size),
            "sha256": sha256(path),
        }
    return result


def write_receipt(path: Path, payload: dict) -> None:
    if path.suffix.casefold() != ".json" or not path_is_within(path, SAVED_ROOT):
        fail("Finalization receipt must remain under project Saved and use .json.")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(payload, stream, indent=2, ensure_ascii=False, sort_keys=True, default=str)
        stream.write("\n")
    os.replace(temporary, path)


def property_value(owner, *names: str):
    candidates: list[str] = []
    for name in names:
        candidates.append(name)
        candidates.append(name[:1].lower() + name[1:])
        snake: list[str] = []
        for character in name:
            if character.isupper() and snake:
                snake.append("_")
            snake.append(character.lower())
        candidates.append("".join(snake))
        if name.startswith("b") and len(name) > 1 and name[1].isupper():
            candidates.append(name[1:2].lower() + name[2:])
    for candidate in dict.fromkeys(candidates):
        try:
            return owner.get_editor_property(candidate)
        except Exception:
            pass
        try:
            return getattr(owner, candidate)
        except Exception:
            pass
    fail("Missing required property {} on {}".format(names, owner))


def object_path(value) -> str:
    if value is None:
        return ""
    try:
        return str(value.get_path_name())
    except Exception:
        return str(value)


def package_name(value) -> str:
    text = object_path(value).strip().strip("'\"")
    if text.casefold() in {"", "none", "null"}:
        return ""
    if "'" in text and text.endswith("'"):
        text = text.split("'", 1)[1][:-1]
    return text.split(".", 1)[0]


def load_soft(value):
    if value is None:
        return None
    loader = getattr(value, "load_synchronous", None)
    if callable(loader):
        try:
            loaded = loader()
            if loaded is not None:
                return loaded
        except Exception:
            pass
    return value


def asset_class_path(asset) -> str:
    if asset is None:
        return ""
    try:
        return str(asset.get_class().get_path_name())
    except Exception:
        return ""


def dirty_packages() -> list[str]:
    content = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    maps = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({str(package.get_path_name()) for package in content + maps})


def validate_director() -> dict[str, object]:
    director_class = unreal.load_class(None, DIRECTOR_CLASS_PATH)
    director = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    if director_class is None or director is None:
        fail("Schema-3 Clothing Director class or asset could not be loaded.")
    if asset_class_path(director) != DIRECTOR_CLASS_PATH:
        fail(
            "Clothing Director has the wrong class: actual={} expected={}".format(
                asset_class_path(director), DIRECTOR_CLASS_PATH
            )
        )
    schema_version = int(property_value(director, "SchemaVersion"))
    if schema_version != 3:
        fail("Clothing Director schema must be 3 before legacy retirement.")
    garments = list(property_value(director, "Garments"))
    if not garments:
        fail("Clothing Director Garments array is empty.")
    validator = getattr(director, "is_policy_valid", None)
    if not callable(validator) or not bool(validator()):
        error_getter = getattr(director, "get_policy_validation_error", None)
        detail = str(error_getter()) if callable(error_getter) else "native validator unavailable"
        fail("Clothing Director native validation failed: " + detail)

    garment_ids: list[str] = []
    enabled_keys: set[str] = set()
    for garment in garments:
        garment_id = str(property_value(garment, "GarmentId"))
        enabled = bool(property_value(garment, "bEnabled", "Enabled"))
        source = package_name(load_soft(property_value(garment, "SourceGarment")))
        body = package_name(load_soft(property_value(garment, "BodySurface")))
        if (
            not enabled
            and (not garment_id or garment_id.casefold() == "none")
            and not source
            and not body
        ):
            continue
        if not garment_id or garment_id.casefold() == "none":
            fail("Clothing Director contains an empty GarmentId.")
        garment_ids.append(garment_id)
        if enabled:
            if not source or not body:
                fail("Enabled garment {} has an unresolved source/body pair.".format(garment_id))
            enabled_keys.add(source + "|" + body)
    if len(set(garment_ids)) != len(garment_ids):
        fail("Clothing Director contains duplicate GarmentIds.")
    if not enabled_keys:
        fail("Clothing Director has no enabled garment definitions.")
    return {
        "asset_path": DIRECTOR_PATH,
        "class_path": asset_class_path(director),
        "schema_version": schema_version,
        "garment_ids": sorted(garment_ids),
        "garment_count": len(garment_ids),
        "enabled_pair_keys": sorted(enabled_keys),
    }


def validate_internal_registry(expected_pair_keys: set[str]) -> dict[str, object]:
    registry_class = unreal.load_class(None, REGISTRY_CLASS_PATH)
    registry = unreal.EditorAssetLibrary.load_asset(INTERNAL_REGISTRY_PATH)
    if registry_class is None or registry is None:
        fail("Internal V26 fit registry class or asset could not be loaded.")
    if asset_class_path(registry) != REGISTRY_CLASS_PATH:
        fail(
            "Internal registry has the wrong class: actual={} expected={}".format(
                asset_class_path(registry), REGISTRY_CLASS_PATH
            )
        )
    profiles = list(property_value(registry, "Profiles"))
    if not profiles:
        fail("Internal V26 fit registry contains no profiles.")

    validator_owner = getattr(unreal, "EFClothingFitCompilerLibrary", None)
    validator = (
        getattr(validator_owner, "validate_compiled_profile_detailed", None)
        if validator_owner is not None
        else None
    )
    if not callable(validator):
        fail("Native compiled-profile validator is unavailable in the commandlet.")

    profile_rows: list[dict[str, object]] = []
    profile_keys: set[str] = set()
    for profile in profiles:
        if profile is None:
            fail("Internal V26 fit registry contains a null profile.")
        profile_package = package_name(profile)
        if not profile_package.startswith(INTERNAL_COMPILED_ROOT + "/"):
            fail("Registry profile escaped the internal V26 root: " + profile_package)
        if int(property_value(profile, "CompilerVersion")) != 26:
            fail("Registry profile is not compiler version 26: " + profile_package)

        validation = validator(profile)
        validation_success = bool(property_value(validation, "bSuccess", "Success"))
        validation_report = str(property_value(validation, "Report"))
        if not validation_success:
            fail("Compiled profile validation failed: {} | {}".format(profile_package, validation_report))

        source = package_name(load_soft(property_value(profile, "SourceGarment")))
        body = package_name(load_soft(property_value(profile, "BodySurface")))
        fitted = package_name(load_soft(property_value(profile, "FittedGarment")))
        if not source or not body or not fitted:
            fail("Compiled profile has an unresolved source/body/fitted asset: " + profile_package)
        if not fitted.startswith(INTERNAL_COMPILED_ROOT + "/"):
            fail("Fitted garment escaped the internal V26 root: " + fitted)
        pair_key = source + "|" + body
        if pair_key in profile_keys:
            fail("Internal registry contains duplicate source/body profile pair: " + pair_key)
        profile_keys.add(pair_key)
        profile_rows.append(
            {
                "profile": profile_package,
                "source_body_key": pair_key,
                "fitted_garment": fitted,
                "validation_report": validation_report,
            }
        )

    if profile_keys != expected_pair_keys:
        fail(
            "Director/registry source-body equality failed: director={} registry={}".format(
                sorted(expected_pair_keys), sorted(profile_keys)
            )
        )
    return {
        "asset_path": INTERNAL_REGISTRY_PATH,
        "class_path": asset_class_path(registry),
        "profile_count": len(profiles),
        "profiles": profile_rows,
    }


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        options.set_editor_property(name, True)
    return options


def referencers_for_asset(registry, asset_path: str) -> dict[str, list[str]]:
    package = package_name(asset_path)
    registry_values = registry.get_referencers(package, dependency_options()) or []
    editor_values = unreal.EditorAssetLibrary.find_package_referencers_for_asset(
        package, load_assets_to_confirm=True
    ) or []
    return {
        "asset_registry": sorted(
            {package_name(value) for value in registry_values if package_name(value)}
        ),
        "editor_confirmed": sorted(
            {package_name(value) for value in editor_values if package_name(value)}
        ),
    }


def is_retirement_package(package: str) -> bool:
    return package in LEGACY_TABLES or package.startswith(LEGACY_GENERATED_ROOT + "/")


def audit_retirement_referencers(registry, generated_assets: list[str]) -> dict[str, object]:
    audited_assets = list(LEGACY_TABLES) + generated_assets
    rows: dict[str, object] = {}
    external: dict[str, list[str]] = {}
    for asset in audited_assets:
        references = referencers_for_asset(registry, asset)
        rows[asset] = references
        combined = set(references["asset_registry"]) | set(references["editor_confirmed"])
        blocked = sorted(
            package
            for package in combined
            if package != package_name(asset) and not is_retirement_package(package)
        )
        if blocked:
            external[asset] = blocked
    if external:
        fail("Legacy retirement has external referencers: " + repr(external))
    return {"assets": rows, "external_referencers": external}


def list_packages(root: str, recursive: bool) -> list[str]:
    return sorted(
        {
            package_name(value)
            for value in unreal.EditorAssetLibrary.list_assets(
                root, recursive=recursive, include_folder=False
            )
            if package_name(value)
        }
    )


def physical_uassets(root: Path, recursive: bool) -> list[str]:
    root = root.resolve()
    if not path_is_within(root, CONTENT_ROOT):
        fail("Physical verification root escaped project Content: " + str(root))
    if not root.is_dir():
        return []
    iterator = root.rglob("*.uasset") if recursive else root.glob("*.uasset")
    return sorted(str(path.resolve()) for path in iterator if path.is_file())


def validate_execution_context() -> Path:
    if os.environ.get(APPLY_ENV, "") != "1":
        fail("Explicit finalization guard is missing: {}=1".format(APPLY_ENV))
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail("Finalization is restricted to NoShellForWinter.uproject.")
    if PROJECT_ROOT.name.casefold() != "noshellforwinter":
        fail("Finalization is outside the writable NoShellForWinter target.")
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Single-Director finalization requires Unreal Engine 5.8.")
    receipt_text = os.environ.get(RECEIPT_ENV, "").strip()
    if not receipt_text:
        fail("Finalization receipt environment path is missing.")
    receipt_path = Path(receipt_text).resolve()
    if receipt_path.exists():
        fail("Refusing to overwrite an existing finalization receipt: " + str(receipt_path))
    if not path_is_within(receipt_path, SAVED_ROOT):
        fail("Finalization receipt escaped project Saved.")
    return receipt_path


def main() -> None:
    fail(
        "RETIRED: legacy Clothing Morph finalization is intentionally disabled; "
        "schema 3 must not delete legacy assets."
    )
    default_receipt = (
        SAVED_ROOT
        / "ClothingMorphV2QA"
        / "Director"
        / (
            "EFClothingMorphSingleDirectorFinalization_"
            + datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            + ".json"
        )
    )
    receipt_path = default_receipt
    payload: dict[str, object] = {
        "schema": "EFClothingMorph.SingleDirectorFinalization.1",
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "FAIL",
        "success": False,
        "project": str(PROJECT_FILE),
        "director": DIRECTOR_PATH,
        "registry": INTERNAL_REGISTRY_PATH,
        "legacy_tables": list(LEGACY_TABLES),
        "legacy_generated_root": LEGACY_GENERATED_ROOT,
        "deleted_assets": [],
        "errors": [],
    }
    try:
        receipt_path = validate_execution_context()
        if dirty_packages():
            fail("Commandlet has dirty content/map packages before finalization.")

        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.search_all_assets(True)
        registry.scan_paths_synchronous([INTERNAL_COMPILED_ROOT], True)
        registry.wait_for_completion()

        payload["protected_before"] = protected_snapshot()
        payload["director_validation"] = validate_director()
        enabled_pairs = set(payload["director_validation"]["enabled_pair_keys"])
        payload["registry_validation"] = validate_internal_registry(enabled_pairs)

        public_before = list_packages(PUBLIC_AUTHORING_ROOT, recursive=False)
        expected_public_before = sorted([DIRECTOR_PATH, *LEGACY_TABLES])
        if public_before != expected_public_before:
            fail(
                "Public authoring folder is not the exact Director + two legacy tables set: "
                + repr(public_before)
            )
        public_directory = (
            CONTENT_ROOT / "_Game" / "Data" / "EFClothingMorph"
        ).resolve()
        public_uassets_before = physical_uassets(public_directory, recursive=False)
        expected_public_files = sorted(
            str((public_directory / (name + ".uasset")).resolve())
            for name in (
                "DA_EFClothingMorphDirector",
                "DT_EFClothingGarments",
                "DT_EFClothingGarmentTuning",
            )
        )
        if public_uassets_before != expected_public_files:
            fail(
                "Physical public authoring folder is not the exact Director + two legacy tables set: "
                + repr(public_uassets_before)
            )
        for table in LEGACY_TABLES:
            if not unreal.EditorAssetLibrary.does_asset_exist(table):
                fail("Required legacy DataTable is already missing: " + table)
        generated_before = list_packages(LEGACY_GENERATED_ROOT, recursive=True)
        if not generated_before:
            fail("Old generated root is absent or empty; refusing a partial finalization.")
        legacy_generated_directory = (
            CONTENT_ROOT / "_Generated" / "EFClothingMorphV2"
        ).resolve()
        generated_uassets_before = physical_uassets(
            legacy_generated_directory, recursive=True
        )
        if not generated_uassets_before:
            fail("Old generated root has no physical uassets; refusing a partial finalization.")
        payload["public_assets_before"] = public_before
        payload["public_uassets_before"] = public_uassets_before
        payload["legacy_generated_assets_before"] = generated_before
        payload["legacy_generated_uassets_before"] = generated_uassets_before
        payload["referencers_before"] = audit_retirement_referencers(
            registry, generated_before
        )

        # The only asset mutations in this script. Generated assets go first so
        # any permitted references from them to the legacy tables disappear
        # before the two table packages are retired.
        if not unreal.EditorAssetLibrary.delete_directory(LEGACY_GENERATED_ROOT):
            fail("EditorAssetLibrary failed to delete the old generated directory.")
        deleted_assets: list[str] = list(generated_before)
        payload["deleted_assets"] = sorted(deleted_assets)
        for table in LEGACY_TABLES:
            if not unreal.EditorAssetLibrary.delete_asset(table):
                fail("EditorAssetLibrary failed to delete legacy table: " + table)
            deleted_assets.append(table)
            payload["deleted_assets"] = sorted(deleted_assets)

        try:
            unreal.SystemLibrary.collect_garbage()
        except Exception:
            pass
        registry.search_all_assets(True)
        registry.wait_for_completion()

        for table in LEGACY_TABLES:
            if unreal.EditorAssetLibrary.does_asset_exist(table):
                fail("Legacy DataTable still exists after deletion: " + table)
        if list_packages(LEGACY_GENERATED_ROOT, recursive=True):
            fail("Old generated root still contains registered assets after deletion.")
        directory_exists = getattr(unreal.EditorAssetLibrary, "does_directory_exist", None)
        if callable(directory_exists) and directory_exists(LEGACY_GENERATED_ROOT):
            fail("Old generated directory still exists after deletion.")
        if physical_uassets(legacy_generated_directory, recursive=True):
            fail("Old generated directory still contains physical uassets after deletion.")

        public_after = list_packages(PUBLIC_AUTHORING_ROOT, recursive=False)
        if public_after != [DIRECTOR_PATH]:
            fail("Director is not the only public authoring asset: " + repr(public_after))
        public_uassets_after = physical_uassets(public_directory, recursive=False)
        expected_director_file = [
            str((public_directory / "DA_EFClothingMorphDirector.uasset").resolve())
        ]
        if public_uassets_after != expected_director_file:
            fail(
                "Director is not the only physical public uasset: "
                + repr(public_uassets_after)
            )
        payload["director_validation_after"] = validate_director()
        payload["registry_validation_after"] = validate_internal_registry(enabled_pairs)
        payload["protected_after"] = protected_snapshot()
        payload["protected_inputs_unchanged"] = (
            payload["protected_before"] == payload["protected_after"]
        )
        if not payload["protected_inputs_unchanged"]:
            fail("A protected mesh, skeleton or Player asset changed during finalization.")
        if dirty_packages():
            fail("Finalization left dirty content/map packages.")

        payload["deleted_assets"] = sorted(deleted_assets)
        payload["public_assets_after"] = public_after
        payload["public_uassets_after"] = public_uassets_after
        payload["legacy_assets_absent"] = True
        payload["single_public_asset"] = True
        payload["status"] = "UE58_EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE_PASS"
        payload["success"] = True
        write_receipt(receipt_path, payload)
        unreal.log("EF_CLOTHING_SINGLE_DIRECTOR_FINALIZATION_RECEIPT=" + str(receipt_path))
    except Exception as error:
        payload["errors"].append(str(error))
        payload["traceback"] = traceback.format_exc()
        retirement_candidates = list(payload.get("legacy_generated_assets_before", []))
        retirement_candidates.extend(LEGACY_TABLES)
        payload["deleted_assets"] = sorted(
            package
            for package in retirement_candidates
            if not unreal.EditorAssetLibrary.does_asset_exist(package)
        )
        try:
            if "protected_before" in payload:
                payload["protected_after"] = protected_snapshot()
                payload["protected_inputs_unchanged"] = (
                    payload["protected_before"] == payload["protected_after"]
                )
            write_receipt(receipt_path, payload)
        except Exception as receipt_error:
            unreal.log_error("Could not write finalization receipt: " + repr(receipt_error))
        unreal.log_error("EF Clothing single-Director finalization failed: " + repr(error))
        raise


main()
