"""Normalize visual body hiding independently from EF Clothing Morph fit geometry.

This editor-only tool changes the Director asset only.  It never changes a
clothing mesh, a body mesh, skin weights, morph targets, a Skeleton, or an
immutable V4 surface binding.  Geometry exclusions remain in
``excluded_body_surface_material_slots``; ``body_sections_to_exclude`` is the
author-facing gameplay-visibility list.

Set EF_CLOTHING_V4_CLEAR_BODY_HIDE_FOR to a comma-separated set of Clothing
Names when an entry should show every body material section in gameplay.
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
        "EF_CLOTHING_V4_BODY_VISIBILITY_RECEIPT",
        os.path.join(SAVED_DIR, "ClothingMorphV4QA", "body_visibility_normalization.json"),
    )
)
TARGETS = {
    value.strip().lower()
    for value in os.environ.get("EF_CLOTHING_V4_CLEAR_BODY_HIDE_FOR", "").split(",")
    if value.strip()
}


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
        raise RuntimeError("Protected package escaped /Game: " + package)
    path = os.path.realpath(
        os.path.join(CONTENT_DIR, package[len("/Game/") :].replace("/", os.sep) + ".uasset")
    )
    if os.path.commonpath((path, CONTENT_DIR)).lower() != CONTENT_DIR.lower():
        raise RuntimeError("Protected package file escaped Content: " + path)
    return path


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


def names(values):
    return [str(value) for value in values if str(value) not in ("", "None")]


def write_receipt(payload):
    if os.path.commonpath((RECEIPT_PATH, SAVED_DIR)).lower() != SAVED_DIR.lower():
        raise RuntimeError("Body-visibility receipt escaped Saved.")
    os.makedirs(os.path.dirname(RECEIPT_PATH), exist_ok=True)
    temporary = RECEIPT_PATH + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True, default=str)
        stream.write("\n")
    os.replace(temporary, RECEIPT_PATH)


def run(payload):
    if not TARGETS:
        raise RuntimeError("EF_CLOTHING_V4_CLEAR_BODY_HIDE_FOR must name at least one Clothing Name.")
    director = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    if director is None:
        raise RuntimeError("EF Clothing Morph Director is missing.")
    if int(prop(director, "schema_version")) != 5:
        raise RuntimeError("Expected the stable V4 Director schema 5.")
    if str(prop(director, "director_id")) != "EFClothingMorphV4":
        raise RuntimeError("Expected the stable EFClothingMorphV4 Director identity.")

    rows = list(prop(director, "garments"))
    protected = {
        "/Game/DazToUnreal/Female/Female",
        "/Game/DazToUnreal/Male/Male",
        "/Game/DazToUnreal/Multiple/Multiple",
        "/Game/FullSample/Player",
    }
    for row in rows:
        for field in ("source_garment", "body_surface"):
            mesh = prop(row, field)
            package = package_name(mesh)
            if package:
                protected.add(package)
                loaded = unreal.EditorAssetLibrary.load_asset(package)
                skeleton = prop(loaded, "skeleton") if loaded else None
                skeleton_package = package_name(skeleton)
                if skeleton_package:
                    protected.add(skeleton_package)
    protected = {
        package
        for package in protected
        if unreal.EditorAssetLibrary.does_asset_exist(package)
    }
    before = snapshot(protected)

    found = set()
    changed = []
    for row in rows:
        clothing_name = str(prop(row, "garment_id"))
        key = clothing_name.lower()
        previous_visual = names(prop(row, "body_sections_to_exclude"))
        previous_geometry = names(prop(row, "excluded_body_surface_material_slots"))
        row_changed = False

        if key in TARGETS:
            found.add(key)
            if previous_visual:
                row.set_editor_property("body_sections_to_exclude", [])
                row_changed = True

        if row_changed:
            changed.append(
                {
                    "clothing_name": clothing_name,
                    "visual_before": previous_visual,
                    "visual_after": names(prop(row, "body_sections_to_exclude")),
                    "geometry_exclusions_preserved": previous_geometry,
                }
            )

    missing = sorted(TARGETS - found)
    if missing:
        raise RuntimeError("Director does not contain requested Clothing Name(s): " + ", ".join(missing))
    director.set_editor_property("garments", rows)
    if not bool(director.is_policy_valid()):
        raise RuntimeError("Director policy became invalid: " + str(director.get_policy_validation_error()))
    if not unreal.EditorAssetLibrary.save_asset(DIRECTOR_PATH, only_if_is_dirty=False):
        raise RuntimeError("Unreal could not save the Director body-visibility update.")

    reloaded = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    if reloaded is None:
        raise RuntimeError("Director could not be reloaded after the update.")
    verified = []
    for row in list(prop(reloaded, "garments")):
        clothing_name = str(prop(row, "garment_id"))
        if clothing_name.lower() not in TARGETS:
            continue
        visual = names(prop(row, "body_sections_to_exclude"))
        geometry = names(prop(row, "excluded_body_surface_material_slots"))
        if visual:
            raise RuntimeError("Target Clothing still has gameplay-hidden body sections: " + clothing_name)
        verified.append(
            {
                "clothing_name": clothing_name,
                "body_sections_hidden_in_gameplay": visual,
                "body_sections_excluded_from_fit": geometry,
            }
        )
    after = snapshot(protected)
    if before != after:
        raise RuntimeError("A protected mesh, Player, or Skeleton changed during visibility normalization.")

    payload["targets"] = sorted(TARGETS)
    payload["changed_rows"] = changed
    payload["verified_targets"] = verified
    payload["protected_packages"] = sorted(protected)
    payload["protected_sha256_before"] = before
    payload["protected_sha256_after"] = after
    payload["protected_inputs_unchanged"] = True


def main():
    payload = {
        "schema_version": 1,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_EF_CLOTHING_MORPH_V4_BODY_VISIBILITY_NORMALIZATION_FAIL",
        "success": False,
        "director": DIRECTOR_PATH,
        "errors": [],
    }
    try:
        run(payload)
        payload["success"] = True
        payload["status"] = "UE58_EF_CLOTHING_MORPH_V4_BODY_VISIBILITY_NORMALIZATION_PASS"
    except Exception as exc:
        payload["errors"].append(str(exc))
        payload["traceback"] = traceback.format_exc()
    write_receipt(payload)
    if not payload["success"]:
        unreal.log_error(json.dumps(payload, sort_keys=True, default=str))
        raise RuntimeError(payload["errors"][-1])
    unreal.log("EF_CLOTHING_MORPH_V4_BODY_VISIBILITY_RECEIPT=" + RECEIPT_PATH)


main()
