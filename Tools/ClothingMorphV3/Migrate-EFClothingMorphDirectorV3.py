"""Migrate the existing single EF Clothing Morph Director in place to V3.

Only the Director asset is saved. Garment meshes, body meshes, Player and every
Skeleton are hashed before and after and must remain byte-identical.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import re
import traceback

import unreal


DIRECTOR_PATH = "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector"
PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
CONTENT_DIR = os.path.realpath(unreal.Paths.project_content_dir())
SAVED_DIR = os.path.realpath(unreal.Paths.project_saved_dir())
RECEIPT_PATH = os.path.realpath(
    os.environ.get(
        "EF_CLOTHING_V3_MIGRATION_RECEIPT",
        os.path.join(SAVED_DIR, "ClothingMorphV3QA", "director_migration.json"),
    )
)
RESET_RUNTIME_FIT_DEFAULTS = (
    os.environ.get("EF_CLOTHING_V3_RESET_RUNTIME_FIT_DEFAULTS", "0") == "1"
)


def prop(value, name):
    return value.get_editor_property(name)


def package_name(value) -> str:
    if value is None:
        return ""
    try:
        text = str(value.get_path_name())
    except Exception:
        text = str(value)
    match = re.search(r"/Game/[A-Za-z0-9_./-]+", text)
    return match.group(0).rstrip("'\"").split(".", 1)[0] if match else ""


def package_file(package: str) -> str:
    if not package.startswith("/Game/"):
        raise RuntimeError("Package escaped /Game: " + package)
    result = os.path.realpath(
        os.path.join(CONTENT_DIR, package[len("/Game/") :].replace("/", os.sep) + ".uasset")
    )
    if os.path.commonpath((result, CONTENT_DIR)).lower() != CONTENT_DIR.lower():
        raise RuntimeError("Package file escaped Content: " + result)
    return result


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def snapshot(packages):
    result = {}
    for package in sorted(set(packages)):
        path = package_file(package)
        if not os.path.isfile(path):
            raise RuntimeError("Protected package is missing: " + path)
        result[package] = {"sha256": sha256(path), "size_bytes": os.path.getsize(path)}
    return result


def write_receipt(payload):
    if os.path.commonpath((RECEIPT_PATH, SAVED_DIR)).lower() != SAVED_DIR.lower():
        raise RuntimeError("Migration receipt escaped Saved.")
    os.makedirs(os.path.dirname(RECEIPT_PATH), exist_ok=True)
    temporary = RECEIPT_PATH + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True, default=str)
        stream.write("\n")
    os.replace(temporary, RECEIPT_PATH)


def run(payload):
    director = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    if director is None:
        raise RuntimeError("The single EF Clothing Morph Director is missing.")
    director_file = package_file(DIRECTOR_PATH)
    payload["director_sha256_before"] = sha256(director_file)
    payload["director_size_before"] = os.path.getsize(director_file)

    upgrader = getattr(
        unreal.EFClothingFitCompilerLibrary,
        "upgrade_director_identity_to_schema4",
        None,
    )
    if not callable(upgrader) or not bool(upgrader(director)):
        raise RuntimeError("Native schema-4 Director migration API is unavailable or failed.")

    rows = list(prop(director, "garments"))
    if not rows:
        raise RuntimeError("Director has no garment entries to migrate.")
    protected = {
        "/Game/DazToUnreal/Female/Female",
        "/Game/DazToUnreal/Male/Male",
        "/Game/DazToUnreal/Multiple/Multiple",
        "/Game/FullSample/Player",
    }
    ids = []
    for row in rows:
        garment_id = str(prop(row, "garment_id"))
        ids.append(garment_id)

        for field in ("source_garment", "body_surface"):
            mesh_package = package_name(prop(row, field))
            if mesh_package:
                protected.add(mesh_package)
                mesh = unreal.EditorAssetLibrary.load_asset(mesh_package)
                skeleton = prop(mesh, "skeleton") if mesh else None
                if skeleton is None:
                    raise RuntimeError("Garment/body mesh has no Skeleton: " + mesh_package)
                protected.add(package_name(skeleton))

    if RESET_RUNTIME_FIT_DEFAULTS:
        for row in rows:
            row.set_editor_property("additional_clearance_cm", 0.0)
            row.set_editor_property("shell_thickness_cm", 0.0)
        director.set_editor_property("garments", rows)

    if len(ids) != len(set(ids)):
        raise RuntimeError("Director has duplicate Garment IDs.")
    protected = {path for path in protected if unreal.EditorAssetLibrary.does_asset_exist(path)}
    before = snapshot(protected)

    if int(prop(director, "schema_version")) != 4:
        raise RuntimeError("Director did not migrate to schema 4.")
    if str(prop(director, "director_id")) != "EFClothingMorphV3":
        raise RuntimeError("Director did not migrate to identity EFClothingMorphV3.")
    if not bool(director.is_policy_valid()):
        raise RuntimeError("Migrated Director is invalid: " + str(director.get_policy_validation_error()))
    if not unreal.EditorAssetLibrary.save_asset(DIRECTOR_PATH, only_if_is_dirty=False):
        raise RuntimeError("Unreal failed to save the migrated Director.")

    reloaded = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    if reloaded is None or int(prop(reloaded, "schema_version")) != 4:
        raise RuntimeError("Saved Director did not reload as schema 4.")
    after = snapshot(protected)
    payload["protected_packages"] = sorted(protected)
    payload["protected_sha256_before"] = before
    payload["protected_sha256_after"] = after
    payload["protected_inputs_unchanged"] = before == after
    payload["director_sha256_after"] = sha256(director_file)
    payload["director_size_after"] = os.path.getsize(director_file)
    payload["director_schema_version"] = int(prop(reloaded, "schema_version"))
    payload["director_id"] = str(prop(reloaded, "director_id"))
    payload["garment_ids"] = ids
    payload["runtime_fit_defaults_reset"] = RESET_RUNTIME_FIT_DEFAULTS
    payload["public_authoring_asset_count"] = 1
    if not payload["protected_inputs_unchanged"]:
        raise RuntimeError("A protected mesh, Player or Skeleton changed during Director migration.")


def main():
    payload = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_EF_CLOTHING_MORPH_V3_DIRECTOR_MIGRATION_FAIL",
        "success": False,
        "director": DIRECTOR_PATH,
        "errors": [],
    }
    try:
        run(payload)
        payload["success"] = True
        payload["status"] = "UE58_EF_CLOTHING_MORPH_V3_DIRECTOR_MIGRATION_PASS"
    except Exception as exc:
        payload["errors"].append(str(exc))
        payload["traceback"] = traceback.format_exc()
    write_receipt(payload)
    if not payload["success"]:
        unreal.log_error(json.dumps(payload, sort_keys=True, default=str))
        raise RuntimeError(payload["errors"][-1])
    unreal.log("EF_CLOTHING_MORPH_V3_DIRECTOR_MIGRATION_RECEIPT=" + RECEIPT_PATH)


main()
