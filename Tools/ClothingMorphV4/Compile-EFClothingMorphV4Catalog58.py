"""Strictly certify EF Clothing Morph V4 bindings without changing source meshes.

The Director and every Skeletal Mesh are read-only inputs. The commandlet only
publishes immutable V4 binding assets and the internal registry. Unlike the
ordinary row-local editor workflow, this pre-cook gate rejects every enabled
draft, duplicate, stale binding, or partial catalog.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import os
import re
import traceback

import unreal


PROJECT_FILE = os.path.realpath(unreal.Paths.get_project_file_path())
PROJECT_DIR = os.path.realpath(unreal.Paths.project_dir())
SAVED_DIR = os.path.realpath(os.path.join(PROJECT_DIR, "Saved"))
DIRECTOR_PATH = os.environ.get(
    "EF_CLOTHING_V4_DIRECTOR",
    "/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector",
)
COMPATIBILITY_PATH = os.environ.get(
    "EF_CLOTHING_V4_COMPATIBILITY",
    "/Game/DazToUnreal/Multiple/Multiple",
)
RECEIPT_PATH = os.path.realpath(
    os.environ.get(
        "EF_CLOTHING_V4_RECEIPT",
        os.path.join(SAVED_DIR, "ClothingMorphV4QA", "compiler_receipt.json"),
    )
)
OUTPUT_ROOT = "/EFClothingMorph/_Internal/Compiled/V4"


def prop(value, name):
    return value.get_editor_property(name)


def package_name(value) -> str:
    if value is None:
        return ""
    try:
        text = str(value.get_path_name())
    except Exception:
        text = str(value)
    match = re.search(r"/(?:Game|EFClothingMorph)/[A-Za-z0-9_./-]+", text)
    if not match:
        return ""
    return match.group(0).rstrip("'\"").split(".", 1)[0]


def package_file(package: str) -> str:
    if not package.startswith("/Game/"):
        raise RuntimeError("Protected package escaped /Game: " + package)
    relative = package[len("/Game/") :].replace("/", os.sep) + ".uasset"
    content = os.path.realpath(unreal.Paths.project_content_dir())
    result = os.path.realpath(os.path.join(content, relative))
    if os.path.commonpath((result, content)).lower() != content.lower():
        raise RuntimeError("Protected file escaped project Content: " + result)
    return result


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def capture(packages):
    result = {}
    for package in sorted(set(packages)):
        path = package_file(package)
        if not os.path.isfile(path):
            raise RuntimeError("Protected package is missing: " + path)
        result[package] = {
            "sha256": sha256(path),
            "size_bytes": os.path.getsize(path),
        }
    return result


def write_receipt(payload):
    if os.path.commonpath((RECEIPT_PATH, SAVED_DIR)).lower() != SAVED_DIR.lower():
        raise RuntimeError("V4 receipt escaped project Saved.")
    os.makedirs(os.path.dirname(RECEIPT_PATH), exist_ok=True)
    temporary = RECEIPT_PATH + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True, default=str)
        stream.write("\n")
    os.replace(temporary, RECEIPT_PATH)


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


def cleanup_orphan_bindings(active_packages, payload):
    """Delete only unreferenced V4 binding assets through Unreal's editor API."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([OUTPUT_ROOT], True)
    discovered = sorted(
        {
            package_name(asset_path)
            for asset_path in unreal.EditorAssetLibrary.list_assets(
                OUTPUT_ROOT, recursive=True, include_folder=False
            )
            if package_name(asset_path)
        }
    )
    candidates = [package for package in discovered if package not in active_packages]
    deleted = []
    blocked = {}
    audited = {}
    for candidate in candidates:
        if not candidate.startswith(OUTPUT_ROOT + "/"):
            raise RuntimeError("V4 cleanup candidate escaped the internal output root: " + candidate)
        asset = unreal.EditorAssetLibrary.load_asset(candidate)
        class_name = asset.get_class().get_name() if asset else ""
        if class_name != "EFClothingSurfaceBinding":
            blocked[candidate] = ["UNEXPECTED_ASSET_CLASS:" + class_name]
            continue
        registry_refs = sorted(
            {
                package_name(value)
                for value in (registry.get_referencers(candidate, dependency_options()) or [])
                if package_name(value) and package_name(value) != candidate
            }
        )
        editor_refs = sorted(
            {
                package_name(value)
                for value in (
                    unreal.EditorAssetLibrary.find_package_referencers_for_asset(
                        candidate, load_assets_to_confirm=True
                    )
                    or []
                )
                if package_name(value) and package_name(value) != candidate
            }
        )
        references = sorted(set(registry_refs) | set(editor_refs))
        audited[candidate] = {
            "asset_registry_referencers": registry_refs,
            "editor_confirmed_referencers": editor_refs,
        }
        if references:
            blocked[candidate] = references
            continue
        if unreal.EditorAssetLibrary.delete_asset(candidate):
            deleted.append(candidate)
        else:
            blocked[candidate] = ["EDITOR_DELETE_API_RETURNED_FALSE"]
    try:
        unreal.SystemLibrary.collect_garbage()
    except Exception:
        pass
    registry.scan_paths_synchronous([OUTPUT_ROOT], True)
    remaining = sorted(
        package_name(asset_path)
        for asset_path in unreal.EditorAssetLibrary.list_assets(
            OUTPUT_ROOT, recursive=True, include_folder=False
        )
        if package_name(asset_path) not in active_packages
    )
    payload["orphan_cleanup"] = {
        "audited": audited,
        "deleted": deleted,
        "blocked": blocked,
        "remaining": remaining,
    }
    if blocked or remaining:
        raise RuntimeError(
            "V4 orphan cleanup was not complete: blocked={} remaining={}".format(
                blocked, remaining
            )
        )


def run(payload):
    if os.path.basename(PROJECT_FILE).lower() != "noshellforwinter.uproject":
        raise RuntimeError("V4 compilation is restricted to NoShellForWinter.uproject.")
    engine_version = str(unreal.SystemLibrary.get_engine_version())
    if not engine_version.startswith("5.8."):
        raise RuntimeError("Expected UE 5.8, got " + engine_version)
    payload["engine_version"] = engine_version

    director = unreal.EditorAssetLibrary.load_asset(DIRECTOR_PATH)
    compatibility = unreal.EditorAssetLibrary.load_asset(COMPATIBILITY_PATH)
    if director is None or compatibility is None:
        raise RuntimeError("Director or protected Multiple compatibility mesh is missing.")
    if int(prop(director, "schema_version")) != 5:
        raise RuntimeError("Director is not schema 5 / V4.")
    payload["director_schema_version"] = int(prop(director, "schema_version"))
    payload["director_id"] = str(prop(director, "director_id"))
    if payload["director_id"] != "EFClothingMorphV4":
        raise RuntimeError("Director identity is not EFClothingMorphV4.")
    if not bool(director.is_identity_valid()):
        raise RuntimeError(
            "Director identity validation failed: "
            + str(director.get_identity_validation_error())
        )
    if not bool(director.is_policy_valid()):
        raise RuntimeError("Director validation failed: " + str(director.get_policy_validation_error()))

    rows = [row for row in list(prop(director, "garments")) if bool(prop(row, "enabled"))]
    if not rows:
        raise RuntimeError("Director has no enabled clothes.")
    ids = [str(prop(row, "garment_id")) for row in rows]
    if any(not value or value == "None" for value in ids):
        raise RuntimeError("Every enabled clothing entry needs a Clothing Name.")
    if len(ids) != len({value.lower() for value in ids}):
        raise RuntimeError("Director contains duplicate enabled Clothing Names.")
    payload["clothing_names"] = sorted(ids)

    protected = {
        DIRECTOR_PATH,
        COMPATIBILITY_PATH,
        "/Game/DazToUnreal/Female/Female",
        "/Game/DazToUnreal/Male/Male",
        "/Game/FullSample/Player",
    }
    for row in rows:
        source = prop(row, "source_garment")
        body = prop(row, "body_surface")
        for mesh in (source, body):
            mesh_package = package_name(mesh)
            if not mesh_package.startswith("/Game/"):
                raise RuntimeError("Director mesh escaped /Game: " + mesh_package)
            protected.add(mesh_package)
            loaded_mesh = unreal.EditorAssetLibrary.load_asset(mesh_package)
            skeleton = prop(loaded_mesh, "skeleton") if loaded_mesh else None
            if skeleton is None:
                raise RuntimeError("Director mesh has no Skeleton: " + mesh_package)
            protected.add(package_name(skeleton))
    compatibility_skeleton = prop(compatibility, "skeleton")
    protected.add(package_name(compatibility_skeleton))
    protected = {path for path in protected if unreal.EditorAssetLibrary.does_asset_exist(path)}
    before = capture(protected)

    options = unreal.EFClothingNativeSourceCompileOptions()
    options.set_editor_property("output_root", OUTPUT_ROOT)
    options.set_editor_property("maximum_push_cm", 2.5)
    options.set_editor_property("only_stale", True)
    options.set_editor_property("strict_catalog_certification", True)
    result = unreal.EFClothingFitCompilerLibrary.compile_native_source_catalog_v4(
        director, compatibility, options
    )
    payload["native_report"] = str(prop(result, "report"))
    payload["enabled_row_count"] = int(prop(result, "enabled_row_count"))
    payload["compiled_row_count"] = int(prop(result, "compiled_row_count"))
    payload["reused_fresh_row_count"] = int(prop(result, "reused_fresh_row_count"))
    payload["draft_row_count"] = int(prop(result, "draft_row_count"))
    payload["failed_row_count"] = int(prop(result, "failed_row_count"))
    payload["published_row_count"] = int(prop(result, "published_row_count"))
    registry = prop(result, "registry")
    payload["registry"] = str(registry.get_path_name()) if registry else ""
    if not bool(prop(result, "success")):
        raise RuntimeError("Native V4 compiler failed: " + payload["native_report"])
    if not package_name(registry).startswith(OUTPUT_ROOT + "/"):
        raise RuntimeError("V4 registry escaped its internal output root.")

    payload["rows"] = []
    valid = 0
    for row_result in list(prop(result, "rows")):
        binding = prop(row_result, "surface_binding")
        pairs = list(prop(binding, "lod_pair_bindings")) if binding else []
        pair_payload = []
        pair_valid = bool(pairs)
        for pair in pairs:
            metrics = prop(pair, "metrics")
            garment_topology = prop(pair, "garment_topology")
            body_topology = prop(pair, "body_topology")
            base_clearance_cm = float(prop(pair, "base_clearance_cm"))
            compiled_reserve_cm = float(prop(pair, "compiled_reserve_cm"))
            current_valid = (
                bool(prop(pair, "certified"))
                and abs(base_clearance_cm) <= 0.0001
                and abs(compiled_reserve_cm) <= 0.0001
                and int(prop(metrics, "invalid_anchor_count")) == 0
                and int(prop(metrics, "bound_render_vertex_count"))
                == int(prop(garment_topology, "render_vertex_count"))
            )
            pair_valid = pair_valid and current_valid
            pair_payload.append(
                {
                    "clothing_lod": int(prop(garment_topology, "lod_index")),
                    "body_lod": int(prop(body_topology, "lod_index")),
                    "clothing_vertices": int(prop(garment_topology, "render_vertex_count")),
                    "body_vertices": int(prop(body_topology, "render_vertex_count")),
                    "invalid_anchors": int(prop(metrics, "invalid_anchor_count")),
                    "base_clearance_cm": base_clearance_cm,
                    "compiled_reserve_cm": compiled_reserve_cm,
                    "collision_only_vertices": int(prop(metrics, "collision_only_vertex_count")),
                    "preserve_upstream_vertices": int(prop(metrics, "preserve_upstream_vertex_count")),
                    "witnesses": int(prop(metrics, "witness_count")),
                    "certified": bool(prop(pair, "certified")),
                }
            )
        row_id = str(prop(row_result, "garment_id"))
        binding_compiler_version = int(prop(binding, "compiler_version")) if binding else 0
        binding_schema_version = int(prop(binding, "schema_version")) if binding else 0
        row_valid = (
            bool(prop(row_result, "success"))
            and binding is not None
            and package_name(binding).startswith(OUTPUT_ROOT + "/")
            and str(prop(binding, "garment_id")) == row_id
            and binding_compiler_version == 28
            and binding_schema_version == 8
            and package_name(prop(binding, "source_garment")).startswith("/Game/")
            and not package_name(prop(binding, "fitted_garment"))
            and pair_valid
        )
        valid += int(row_valid)
        payload["rows"].append(
            {
                "clothing_name": row_id,
                "success": bool(prop(row_result, "success")),
                "reused": bool(prop(row_result, "reused_fresh_binding")),
                "binding": str(binding.get_path_name()) if binding else "",
                "compiler_version": binding_compiler_version,
                "binding_schema_version": binding_schema_version,
                "clothing_mesh": package_name(prop(binding, "source_garment")) if binding else "",
                "generated_clothing_mesh": package_name(prop(binding, "fitted_garment")) if binding else "",
                "valid": row_valid,
                "lod_pairs": pair_payload,
                "report": str(prop(row_result, "report")),
            }
        )

    profiles = list(prop(registry, "profiles"))
    native_bindings = list(prop(registry, "native_source_bindings"))
    strict_validation = unreal.EFClothingFitCompilerLibrary.validate_native_source_catalog_v4(
        director,
        registry,
        compatibility,
        options,
    )
    payload["strict_validation"] = {
        "fresh": bool(prop(strict_validation, "fresh")),
        "enabled_row_count": int(prop(strict_validation, "enabled_row_count")),
        "valid_binding_count": int(prop(strict_validation, "valid_binding_count")),
        "draft_row_count": int(prop(strict_validation, "draft_row_count")),
        "invalid_row_count": int(prop(strict_validation, "invalid_row_count")),
        "stale_row_count": int(prop(strict_validation, "stale_row_count")),
        "report": str(prop(strict_validation, "report")),
    }
    active_packages = {package_name(registry)} | {
        package_name(binding) for binding in native_bindings if package_name(binding)
    }
    cleanup_orphan_bindings(active_packages, payload)
    after = capture(protected)
    payload["protected_packages"] = sorted(protected)
    payload["protected_sha256_before"] = before
    payload["protected_sha256_after"] = after
    payload["protected_inputs_unchanged"] = before == after
    payload["valid_binding_count"] = valid
    payload["valid_clothing_names"] = sorted(
        row["clothing_name"] for row in payload["rows"] if row["valid"]
    )
    payload["registry_profile_count"] = len(profiles)
    payload["registry_native_binding_count"] = len(native_bindings)
    expected = len(rows)
    payload["catalog_equality_gate"] = (
        expected == payload["enabled_row_count"]
        # CompiledRowCount means rows resolved successfully; reused rows are a
        # subset of that count, not an additional set of rows.
        and expected == payload["compiled_row_count"]
        and payload["failed_row_count"] == 0
        and payload["published_row_count"] == expected
        and 0 <= payload["reused_fresh_row_count"] <= payload["compiled_row_count"]
        and expected == valid
        and payload["valid_clothing_names"] == payload["clothing_names"]
        and expected == len(native_bindings)
        and len(profiles) == 0
        and payload["strict_validation"]["fresh"]
        and payload["strict_validation"]["enabled_row_count"] == expected
        and payload["strict_validation"]["valid_binding_count"] == expected
        and payload["strict_validation"]["invalid_row_count"] == 0
        and payload["strict_validation"]["stale_row_count"] == 0
    )
    if not payload["catalog_equality_gate"]:
        raise RuntimeError("V4 enabled/compiled/binding equality gate failed.")
    if not payload["protected_inputs_unchanged"]:
        raise RuntimeError("A protected mesh, skeleton, Player or Director changed during V4 binding compilation.")


def main():
    payload = {
        "schema_version": 1,
        "compiler_version": 28,
        "binding_schema_version": 8,
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "UE58_EF_CLOTHING_MORPH_V4_CATALOG_COMPILE_FAIL",
        "success": False,
        "project": PROJECT_FILE,
        "director": DIRECTOR_PATH,
        "compatibility_reference": COMPATIBILITY_PATH,
        "output_root": OUTPUT_ROOT,
        "errors": [],
    }
    try:
        run(payload)
        payload["success"] = True
        payload["status"] = "UE58_EF_CLOTHING_MORPH_V4_CATALOG_COMPILE_PASS"
    except Exception as exc:
        payload["errors"].append(str(exc))
        payload["traceback"] = traceback.format_exc()
    write_receipt(payload)
    if not payload["success"]:
        unreal.log_error(json.dumps(payload, sort_keys=True, default=str))
        raise RuntimeError(payload["errors"][-1])
    unreal.log("EF_CLOTHING_MORPH_V4_CATALOG_RECEIPT=" + RECEIPT_PATH)


main()
