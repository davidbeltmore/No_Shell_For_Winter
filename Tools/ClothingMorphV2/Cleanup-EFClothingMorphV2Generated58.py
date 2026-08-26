"""Conservatively prune stale EF Clothing Morph V2 compiler outputs.

This runs in a fresh UE 5.8 Python commandlet after compilation so historical
profiles/meshes are not retained by the compiler process.  Only packages under
the exact generated output root are candidates, and referenced packages are
never deleted.
"""

import datetime
import json
import os
import traceback

import unreal


PROJECT_FILE = os.path.realpath(unreal.Paths.get_project_file_path())
PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
SAVED_DIR = os.path.realpath(os.path.join(PROJECT_DIR, "Saved"))
OUTPUT_ROOT = "/Game/_Generated/EFClothingMorphV2"
REGISTRY_PACKAGE = OUTPUT_ROOT + "/DA_EFClothingFitRegistry"
DEFAULT_RECEIPT_PATH = os.path.join(
    SAVED_DIR,
    "ClothingMorphV2QA",
    "generated_cleanup_receipt.json",
)
RECEIPT_PATH = os.path.realpath(
    os.environ.get("EF_CLOTHING_V2_CLEANUP_RECEIPT", DEFAULT_RECEIPT_PATH)
)


def _write_receipt(payload):
    os.makedirs(os.path.dirname(RECEIPT_PATH), exist_ok=True)
    temporary_path = RECEIPT_PATH + ".tmp"
    with open(temporary_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False, sort_keys=True)
        handle.write("\n")
    os.replace(temporary_path, RECEIPT_PATH)


def _get_property(value, name):
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
    return _object_path(value).split(".", 1)[0]


def _list_generated_packages():
    return sorted(
        {
            _package_name(asset_path)
            for asset_path in unreal.EditorAssetLibrary.list_assets(
                OUTPUT_ROOT,
                recursive=True,
                include_folder=False,
            )
        }
    )


def _physical_generated_packages():
    output_directory = os.path.realpath(
        os.path.join(
            unreal.Paths.project_content_dir(),
            "_Generated",
            "EFClothingMorphV2",
        )
    )
    expected_content_root = os.path.realpath(unreal.Paths.project_content_dir())
    if os.path.commonpath((output_directory, expected_content_root)) != expected_content_root:
        raise RuntimeError("Generated cleanup directory escaped project Content.")
    if not os.path.isdir(output_directory):
        return []
    return sorted(
        OUTPUT_ROOT + "/" + os.path.splitext(name)[0]
        for name in os.listdir(output_directory)
        if name.lower().endswith(".uasset")
    )


def _physical_package_files(package):
    if not package.startswith(OUTPUT_ROOT + "/"):
        raise RuntimeError("Physical cleanup package escaped generated root: " + package)
    asset_name = package.rsplit("/", 1)[-1]
    if not asset_name.startswith(("DA_", "SK_")):
        raise RuntimeError("Unexpected generated asset family: " + package)
    output_directory = os.path.realpath(
        os.path.join(
            unreal.Paths.project_content_dir(),
            "_Generated",
            "EFClothingMorphV2",
        )
    )
    content_root = os.path.realpath(unreal.Paths.project_content_dir())
    if os.path.commonpath((output_directory, content_root)) != content_root:
        raise RuntimeError("Physical cleanup directory escaped project Content.")
    base_path = os.path.realpath(os.path.join(output_directory, asset_name))
    if os.path.commonpath((base_path, output_directory)) != output_directory:
        raise RuntimeError("Physical cleanup file escaped generated directory.")
    return [
        base_path + extension
        for extension in (".uasset", ".uexp", ".ubulk", ".uptnl")
        if os.path.isfile(base_path + extension)
    ]


def _active_packages_from_registry():
    registry = unreal.EditorAssetLibrary.load_asset(REGISTRY_PACKAGE)
    if registry is None:
        raise RuntimeError("Generated fit registry is missing: " + REGISTRY_PACKAGE)
    keep = {REGISTRY_PACKAGE}
    profiles = list(_get_property(registry, "profiles"))
    if not profiles:
        raise RuntimeError("Generated fit registry contains no active profiles.")
    for profile in profiles:
        if profile is None:
            raise RuntimeError("Generated fit registry contains a null profile.")
        profile_package = _package_name(profile)
        fitted_garment = _get_property(profile, "fitted_garment")
        try:
            fitted_garment = fitted_garment.load_synchronous()
        except Exception:
            pass
        fitted_package = _package_name(fitted_garment)
        for package in (profile_package, fitted_package):
            if not package.startswith(OUTPUT_ROOT + "/"):
                raise RuntimeError(
                    "Registry active output escaped generated root: " + package
                )
            keep.add(package)
    return keep


def _run(payload):
    if os.path.basename(PROJECT_FILE).lower() != "noshellforwinter.uproject":
        raise RuntimeError("Cleanup is restricted to NoShellForWinter.uproject.")
    engine_version = str(unreal.SystemLibrary.get_engine_version())
    if not engine_version.startswith("5.8."):
        raise RuntimeError("Expected UE 5.8, got " + engine_version)
    if os.path.commonpath((RECEIPT_PATH, SAVED_DIR)).lower() != SAVED_DIR.lower():
        raise RuntimeError("Cleanup receipt must remain under project Saved.")

    keep = _active_packages_from_registry()
    initial_registry_packages = _list_generated_packages()
    initial_physical_packages = _physical_generated_packages()
    candidates = sorted(
        (set(initial_registry_packages) | set(initial_physical_packages)) - keep
    )
    if any(not package.startswith(OUTPUT_ROOT + "/") for package in candidates):
        raise RuntimeError("Cleanup candidate escaped generated output root.")

    deleted = []
    asset_registry_orphans = []
    blocked = {}
    ordered = sorted(
        candidates,
        key=lambda package: (
            0 if package.rsplit("/", 1)[-1].startswith("DA_") else 1,
            package,
        ),
    )
    for package in ordered:
        if package not in _list_generated_packages():
            # It is already absent from the Asset Registry. Physical cleanup is
            # deferred until every registered candidate has passed references.
            asset_registry_orphans.append(package)
            continue
        referencers = sorted(
            {
                _package_name(referencer)
                for referencer in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                    package,
                    load_assets_to_confirm=True,
                )
                if _package_name(referencer) != package
            }
        )
        if referencers:
            blocked[package] = referencers
            continue
        if unreal.EditorAssetLibrary.delete_asset(package):
            deleted.append(package)
        else:
            blocked[package] = ["DELETE_API_RETURNED_FALSE"]

    try:
        unreal.SystemLibrary.collect_garbage()
    except Exception:
        pass

    remaining_registry = sorted(set(_list_generated_packages()) - keep)
    remaining_physical = sorted(set(_physical_generated_packages()) - keep)
    physical_orphan_deletions = []
    # ObjectTools can successfully unregister a package while a stale primary
    # file remains on disk in commandlet mode. Only after AssetTools reports no
    # referencers and the Asset Registry contains no stale package do we remove
    # those exact generated cache files. Source assets and active outputs cannot
    # enter this allowlist.
    if not blocked and not remaining_registry:
        for package in remaining_physical:
            if package not in candidates or package in keep:
                raise RuntimeError("Unapproved physical orphan entered cleanup: " + package)
            file_rows = []
            for path in _physical_package_files(package):
                row = {
                    "path": path,
                    "size_bytes": os.path.getsize(path),
                }
                os.remove(path)
                row["deleted"] = not os.path.exists(path)
                if not row["deleted"]:
                    raise RuntimeError("Could not remove generated physical orphan: " + path)
                file_rows.append(row)
            physical_orphan_deletions.append(
                {"package": package, "files": file_rows}
            )
        remaining_physical = sorted(set(_physical_generated_packages()) - keep)
    payload.update(
        {
            "engine_version": engine_version,
            "keep_packages": sorted(keep),
            "candidate_packages": candidates,
            "deleted_packages": deleted,
            "asset_registry_orphans": asset_registry_orphans,
            "physical_orphan_deletions": physical_orphan_deletions,
            "blocked_packages": blocked,
            "remaining_registry_packages": remaining_registry,
            "remaining_physical_packages": remaining_physical,
        }
    )
    if blocked or remaining_registry or remaining_physical:
        raise RuntimeError("Stale generated assets remain after AssetTools cleanup.")


def main():
    payload = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_EF_CLOTHING_MORPH_V2_GENERATED_CLEANUP_FAIL",
        "success": False,
        "project": PROJECT_FILE,
        "output_root": OUTPUT_ROOT,
        "errors": [],
    }
    try:
        _run(payload)
        payload["success"] = True
        payload["status"] = "UE58_EF_CLOTHING_MORPH_V2_GENERATED_CLEANUP_PASS"
        _write_receipt(payload)
        unreal.log("EF_CLOTHING_MORPH_V2_GENERATED_CLEANUP_RECEIPT=" + RECEIPT_PATH)
    except Exception as exc:
        payload["errors"].append(str(exc))
        payload["traceback"] = traceback.format_exc()
        _write_receipt(payload)
        unreal.log_error(
            "EF Clothing Morph V2 generated cleanup failed: "
            + json.dumps(payload, sort_keys=True)
        )
        raise


main()
