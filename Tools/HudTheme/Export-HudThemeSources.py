"""Export in-scope project HUD Texture2D assets to deterministic PNG inputs.

Run this script inside the open UE 5.8 *target* editor. It only reads assets and
writes PNG/JSON files below Saved/HudThemeRework; it never saves or mutates an
Unreal package.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Any

import unreal


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from hud_theme_common import (  # noqa: E402
    PipelineError,
    assert_target_project,
    canonical_unreal_property_value,
    config_sha256,
    destination_relative_path,
    ensure_within,
    game_package_file,
    load_config,
    normalize_relative_path,
    output_root,
    parse_texture_dimensions,
    png_dimensions,
    select_source_root,
    sha256_file,
    validate_asset_name,
    write_json_if_changed,
)


MANIFEST_NAME = "HudThemeSourceManifest.json"
TEXTURE_PROPERTIES = (
    "srgb",
    "compression_settings",
    "compression_quality",
    "compression_no_alpha",
    "mip_gen_settings",
    "mip_load_options",
    "filter",
    "lod_group",
    "lod_bias",
    "address_x",
    "address_y",
    "never_stream",
    "virtual_texture_streaming",
    "flip_green_channel",
    "preserve_border",
    "pad_with_border_color",
    "power_of_two_mode",
    "max_texture_size",
    "downscale",
    "downscale_options",
    "composite_texture_mode",
    "composite_texture",
)


def log(message: str) -> None:
    unreal.log("HUD_THEME_EXPORT: " + message)


def fail(message: str) -> None:
    unreal.log_error("HUD_THEME_EXPORT_FAIL: " + message)
    raise PipelineError(message)


def package_name(package: Any) -> str:
    try:
        return str(package.get_name())
    except Exception as exc:
        fail(f"Could not resolve a dirty Unreal package name: {exc}")
    raise AssertionError("fail() always raises")


def assert_safe_editor_state() -> dict[str, Any]:
    """Fail closed when export could observe transient editor-owned state."""
    try:
        editor_subsystem = unreal.get_editor_subsystem(
            unreal.UnrealEditorSubsystem
        )
    except Exception as exc:
        fail(f"Could not query UnrealEditorSubsystem for PIE state: {exc}")
    if editor_subsystem is None:
        fail("UnrealEditorSubsystem is unavailable; PIE state is unknown")

    try:
        game_world = editor_subsystem.get_game_world()
    except Exception as exc:
        fail(f"Could not query the active PIE game world: {exc}")
    if game_world is not None:
        fail(
            "Refusing to export while a PIE/game world exists: "
            f"{game_world.get_path_name()}"
        )

    try:
        dirty_content = sorted(
            {
                package_name(package)
                for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
            },
            key=str.casefold,
        )
        dirty_maps = sorted(
            {
                package_name(package)
                for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
            },
            key=str.casefold,
        )
    except PipelineError:
        raise
    except Exception as exc:
        fail(f"Could not query dirty editor packages: {exc}")

    if dirty_content or dirty_maps:
        fail(
            "Refusing to export from a dirty editor state. Save or discard the "
            f"packages first. content={dirty_content!r}, maps={dirty_maps!r}"
        )
    return {
        "pie_game_world_present": False,
        "dirty_content_packages": dirty_content,
        "dirty_map_packages": dirty_maps,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--project-root")
    parser.add_argument("--force", action="store_true")
    arguments, _unknown = parser.parse_known_args()
    return arguments


def unreal_project_root(arguments: argparse.Namespace, config: dict[str, Any]) -> Path:
    root = Path(unreal.Paths.project_dir()).resolve()
    assert_target_project(root, config["project_file"])
    if arguments.project_root:
        requested = Path(arguments.project_root).resolve()
        if requested != root:
            fail(
                "The open editor project differs from --project-root: "
                f"{root} != {requested}"
            )
    return root


def asset_class_name(asset_data: Any) -> str:
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        try:
            return str(asset_data.asset_class)
        except Exception:
            return ""


def capture_texture_settings(texture: Any) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for property_name in TEXTURE_PROPERTIES:
        try:
            result[property_name] = canonical_unreal_property_value(
                texture.get_editor_property(property_name)
            )
        except Exception:
            # Properties vary slightly by engine minor version. Import copies
            # live settings directly from the source asset, so this remains
            # audit metadata rather than a lossy restoration mechanism.
            continue
    return result


def export_png(texture: Any, destination: Path, force: bool) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".exporting.png")
    if temporary.exists():
        temporary.unlink()

    try:
        exporter = unreal.TextureExporterPNG()
    except Exception as exc:
        raise PipelineError(
            "TextureExporterPNG is unavailable in this UE 5.8 editor"
        ) from exc

    task = unreal.AssetExportTask()
    task.set_editor_property("object", texture)
    task.set_editor_property("exporter", exporter)
    task.set_editor_property("filename", str(temporary))
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    task.set_editor_property("write_empty_files", False)

    if not unreal.Exporter.run_asset_export_task(task):
        raise PipelineError(f"Texture export returned false: {texture.get_path_name()}")
    if not temporary.is_file() or temporary.stat().st_size == 0:
        raise PipelineError(f"Texture exporter did not create PNG: {temporary}")

    try:
        if (
            not force
            and destination.is_file()
            and sha256_file(destination) == sha256_file(temporary)
        ):
            temporary.unlink()
        else:
            os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def main() -> int:
    arguments = parse_arguments()
    config = load_config()
    project_root = unreal_project_root(arguments, config)
    generated_root = output_root(project_root, config)
    export_root = (generated_root / "SourceTextures").resolve()
    ensure_within(export_root, project_root / "Saved")
    manifest_path = generated_root / MANIFEST_NAME

    engine_version = unreal.SystemLibrary.get_engine_version()
    if not engine_version.startswith("5.8."):
        fail(f"Expected UE 5.8, found {engine_version}")
    editor_safety = assert_safe_editor_state()

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_rows: dict[str, Any] = {}
    root_counts: dict[str, int] = {}
    missing_required_roots: list[str] = []
    missing_optional_roots: list[str] = []

    for root_entry in config["source_roots"]:
        root = root_entry["root"]
        rows = list(
            registry.get_assets_by_path(
                root,
                recursive=True,
                include_only_on_disk_assets=True,
            )
        )
        texture_rows = [
            row for row in rows if asset_class_name(row).casefold() == "texture2d"
        ]
        root_counts[root] = len(texture_rows)
        if not texture_rows:
            if root_entry.get("required", False):
                missing_required_roots.append(root)
            else:
                missing_optional_roots.append(root)
        for row in texture_rows:
            asset_rows[str(row.package_name)] = row

    if missing_required_roots:
        fail(
            "Required source roots contain no Texture2D assets: "
            + ", ".join(missing_required_roots)
        )

    audited_packages = {
        package_name.casefold(): package_name
        for package_name in config["native_texture_packages"]
    }
    missing_audited_packages = sorted(
        package_name
        for folded_name, package_name in audited_packages.items()
        if folded_name not in {
            existing_package.casefold() for existing_package in asset_rows
        }
    )
    if missing_audited_packages:
        fail(
            "Audited baked-color Texture2D packages are absent: "
            + ", ".join(missing_audited_packages)
        )
    asset_rows = {
        package_name: row
        for package_name, row in asset_rows.items()
        if package_name.casefold() in audited_packages
    }

    assets: list[dict[str, Any]] = []
    errors: list[dict[str, str]] = []
    destination_packages: set[str] = set()
    roots = config["source_roots"]

    for package_name in sorted(asset_rows, key=str.casefold):
        row = asset_rows[package_name]
        root_entry = select_source_root(package_name, roots)
        if root_entry is None:
            errors.append(
                {"package": package_name, "error": "No configured source root"}
            )
            continue

        asset_name = str(row.asset_name)
        try:
            validate_asset_name(asset_name)
            destination_relative = destination_relative_path(
                package_name, root_entry
            )
            if Path(destination_relative).name != asset_name:
                raise PipelineError(
                    "Texture package leaf and asset name differ; reversible lookup "
                    f"would be ambiguous: {package_name} vs {asset_name}"
                )
            export_relative = normalize_relative_path(
                f"SourceTextures/{destination_relative}.png"
            )
            export_path = (generated_root / Path(export_relative)).resolve()
            ensure_within(export_path, export_root)

            texture = unreal.EditorAssetLibrary.load_asset(package_name)
            if texture is None or texture.get_class().get_name() != "Texture2D":
                raise PipelineError(f"Failed to load Texture2D {package_name}")

            export_png(texture, export_path, arguments.force)
            width, height = png_dimensions(export_path)
            registry_width, registry_height = parse_texture_dimensions(
                row.get_tag_value("Dimensions")
            )
            if (registry_width, registry_height) != (width, height):
                raise PipelineError(
                    "Exported source-art dimensions differ from the Texture2D "
                    f"Asset Registry tag: {(width, height)} != "
                    f"{(registry_width, registry_height)} for {package_name}"
                )
            png_hash = sha256_file(export_path)
            object_path = f"{package_name}.{asset_name}"
            package_file = game_package_file(project_root, package_name)

            # Detect mapping collisions before emitting the manifest. Theme is
            # intentionally omitted because the same relative package is used
            # once under each distinct theme directory.
            if destination_relative.casefold() in destination_packages:
                raise PipelineError(
                    f"Destination collision for {destination_relative}"
                )
            destination_packages.add(destination_relative.casefold())

            assets.append(
                {
                    "asset_name": asset_name,
                    "asset_registry_dimensions": (
                        f"{registry_width}x{registry_height}"
                    ),
                    "class": "Texture2D",
                    "height": height,
                    "object_path": object_path,
                    "package_name": package_name,
                    "root_label": root_entry["label"],
                    "source_root": root_entry["root"],
                    "source_png": export_relative,
                    "source_png_sha256": png_hash,
                    "source_package_file": package_file.relative_to(
                        project_root
                    ).as_posix(),
                    "source_package_file_sha256": sha256_file(package_file),
                    "source_settings": capture_texture_settings(texture),
                    "theme_relative_package": destination_relative,
                    "width": width,
                }
            )
        except Exception as exc:
            errors.append({"package": package_name, "error": str(exc)})

    payload = {
        "schema_version": 1,
        "status": "PASS" if not errors else "FAIL",
        "config_sha256": config_sha256(),
        "common_script_sha256": sha256_file(
            SCRIPT_DIR / "hud_theme_common.py"
        ),
        "exporter_script_sha256": sha256_file(Path(__file__)),
        "engine_version": engine_version,
        "editor_safety": editor_safety,
        "project_file": config["project_file"],
        "asset_count": len(assets),
        "audited_native_texture_count": len(audited_packages),
        "root_texture_counts": dict(sorted(root_counts.items())),
        "missing_optional_roots": sorted(missing_optional_roots),
        "assets": sorted(assets, key=lambda item: item["package_name"].casefold()),
        "errors": errors,
    }
    write_json_if_changed(manifest_path, payload)

    if errors:
        fail(
            f"{len(errors)} texture export(s) failed; inspect "
            f"{manifest_path.relative_to(project_root)}"
        )
    log(
        f"PASS: {len(assets)} source textures exported; manifest "
        f"{manifest_path.relative_to(project_root)}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        unreal.log_error("HUD_THEME_EXPORT_FATAL: " + str(exception))
        raise
