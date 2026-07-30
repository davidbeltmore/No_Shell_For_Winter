"""Import validated HUD theme PNGs into the isolated project-owned theme tree.

Run inside the open UE 5.8 target editor only after offline validation passes.
The importer never deletes, renames, or moves assets. Existing assets are
updated only when they carry this pipeline's ownership metadata.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

import unreal


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from hud_theme_common import (  # noqa: E402
    PipelineError,
    THEME_IDS,
    assert_target_project,
    canonical_unreal_property_value,
    config_sha256,
    ensure_within,
    game_package_file,
    load_config,
    load_json,
    output_root,
    parse_texture_dimensions,
    sha256_file,
    validate_asset_name,
    validate_unreal_package,
    write_json_if_changed,
)


SOURCE_MANIFEST_NAME = "HudThemeSourceManifest.json"
VARIANT_MANIFEST_NAME = "HudThemeVariantManifest.json"
VALIDATION_NAME = "HudThemeVariantValidation.json"
IMPORT_MANIFEST_NAME = "HudThemeImportManifest.json"
DRY_RUN_MANIFEST_NAME = "HudThemeImportDryRun.json"

OWNER_TAG = "HudThemePipeline.Owner"
OWNER_VALUE = "NoShellForWinter.HudThemePipeline.v1"
FINGERPRINT_TAG = "HudThemePipeline.ImportFingerprint"
SOURCE_TAG = "HudThemePipeline.SourceObject"
THEME_TAG = "HudThemePipeline.Theme"
PROFILE_TAG = "HudThemePipeline.ProfileSha256"

REQUIRED_TEXTURE_PROPERTIES = (
    "srgb",
    "compression_settings",
    "lod_group",
)
OPTIONAL_TEXTURE_PROPERTIES = (
    "compression_quality",
    "compression_no_alpha",
    "mip_gen_settings",
    "mip_load_options",
    "filter",
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
    unreal.log("HUD_THEME_IMPORT: " + message)


def package_name(package: Any) -> str:
    try:
        return str(package.get_name())
    except Exception as exc:
        raise PipelineError(
            f"Could not resolve a dirty Unreal package name: {exc}"
        ) from exc


def dirty_editor_packages() -> dict[str, list[str]]:
    try:
        content = sorted(
            {
                package_name(package)
                for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
            },
            key=str.casefold,
        )
        maps = sorted(
            {
                package_name(package)
                for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
            },
            key=str.casefold,
        )
    except PipelineError:
        raise
    except Exception as exc:
        raise PipelineError(
            f"Could not query dirty editor packages: {exc}"
        ) from exc
    return {"content": content, "maps": maps}


def assert_safe_editor_state() -> dict[str, Any]:
    """Fail closed before any theme destination can be imported or replaced."""
    try:
        editor_subsystem = unreal.get_editor_subsystem(
            unreal.UnrealEditorSubsystem
        )
    except Exception as exc:
        raise PipelineError(
            f"Could not query UnrealEditorSubsystem for PIE state: {exc}"
        ) from exc
    if editor_subsystem is None:
        raise PipelineError(
            "UnrealEditorSubsystem is unavailable; PIE state is unknown"
        )

    try:
        game_world = editor_subsystem.get_game_world()
    except Exception as exc:
        raise PipelineError(
            f"Could not query the active PIE game world: {exc}"
        ) from exc
    if game_world is not None:
        raise PipelineError(
            "Refusing to import while a PIE/game world exists: "
            f"{game_world.get_path_name()}"
        )

    dirty = dirty_editor_packages()
    if dirty["content"] or dirty["maps"]:
        raise PipelineError(
            "Refusing to import from a dirty editor state. Save or discard the "
            "packages first. "
            f"content={dirty['content']!r}, maps={dirty['maps']!r}"
        )
    return {
        "pie_game_world_present": False,
        "dirty_content_packages": dirty["content"],
        "dirty_map_packages": dirty["maps"],
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--project-root")
    parser.add_argument("--theme", choices=THEME_IDS)
    parser.add_argument("--dry-run", action="store_true")
    arguments, _unknown = parser.parse_known_args()
    return arguments


def capture_settings(texture: Any) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for property_name in REQUIRED_TEXTURE_PROPERTIES + OPTIONAL_TEXTURE_PROPERTIES:
        try:
            result[property_name] = canonical_unreal_property_value(
                texture.get_editor_property(property_name)
            )
        except Exception:
            continue
    return result


def texture_size(texture: Any) -> tuple[int, int]:
    object_path = texture.get_path_name()
    asset_data = unreal.AssetRegistryHelpers.get_asset_registry().get_asset_by_object_path(
        object_path
    )
    if not asset_data:
        raise PipelineError(
            f"Asset Registry row is absent for Texture2D: {object_path}"
        )
    return parse_texture_dimensions(asset_data.get_tag_value("Dimensions"))


def metadata_value(asset: Any, tag: str) -> str:
    try:
        value = unreal.EditorAssetLibrary.get_metadata_tag(asset, tag)
        return "" if value is None else str(value)
    except Exception:
        return ""


def set_pipeline_metadata(asset: Any, variant: dict[str, Any]) -> None:
    values = {
        OWNER_TAG: OWNER_VALUE,
        FINGERPRINT_TAG: variant["import_fingerprint_sha256"],
        SOURCE_TAG: variant["source_asset"],
        THEME_TAG: variant["theme"],
        PROFILE_TAG: variant["profile_sha256"],
    }
    for tag, value in values.items():
        unreal.EditorAssetLibrary.set_metadata_tag(asset, tag, value)


def asset_exists(package_name: str, object_path: str) -> bool:
    return bool(
        unreal.EditorAssetLibrary.does_asset_exist(package_name)
        or unreal.EditorAssetLibrary.does_asset_exist(object_path)
    )


def copy_texture_settings(
    source: Any, destination: Any, warnings: list[str]
) -> None:
    for property_name in REQUIRED_TEXTURE_PROPERTIES:
        try:
            destination.set_editor_property(
                property_name, source.get_editor_property(property_name)
            )
        except Exception as exc:
            raise PipelineError(
                f"Could not copy required texture property {property_name}: {exc}"
            ) from exc
    for property_name in OPTIONAL_TEXTURE_PROPERTIES:
        try:
            destination.set_editor_property(
                property_name, source.get_editor_property(property_name)
            )
        except Exception as exc:
            warnings.append(
                f"Optional property {property_name} was not copied: {exc}"
            )


def apply_variant_texture_policy(
    destination: Any, policy: dict[str, Any]
) -> None:
    if policy != {
        "lod_group": "TEXTUREGROUP_UI",
        "never_stream": True,
        "virtual_texture_streaming": False,
    }:
        raise PipelineError("Unsupported native HUD texture policy")
    try:
        ui_texture_group = unreal.TextureGroup.TEXTUREGROUP_UI
    except Exception as exc:
        raise PipelineError(
            f"UE 5.8 Python does not expose TEXTUREGROUP_UI: {exc}"
        ) from exc
    try:
        destination.set_editor_property(
            "lod_group", ui_texture_group
        )
        destination.set_editor_property("never_stream", True)
        destination.set_editor_property("virtual_texture_streaming", False)
    except Exception as exc:
        raise PipelineError(
            f"Could not apply native HUD texture residency policy: {exc}"
        ) from exc


def validate_variant_texture_policy_support(
    policy: dict[str, Any]
) -> None:
    """Exercise non-mutating Python API requirements during preflight."""
    if policy != {
        "lod_group": "TEXTUREGROUP_UI",
        "never_stream": True,
        "virtual_texture_streaming": False,
    }:
        raise PipelineError("Unsupported native HUD texture policy")
    try:
        ui_texture_group = unreal.TextureGroup.TEXTUREGROUP_UI
    except Exception as exc:
        raise PipelineError(
            f"UE 5.8 Python does not expose TEXTUREGROUP_UI: {exc}"
        ) from exc
    if "TEXTUREGROUP_UI" not in str(ui_texture_group).upper():
        raise PipelineError(
            f"Unexpected UE 5.8 UI texture-group enum: {ui_texture_group}"
        )


def validate_variant_texture_policy(destination: Any) -> None:
    try:
        lod_group = str(destination.get_editor_property("lod_group"))
        never_stream = bool(
            destination.get_editor_property("never_stream")
        )
        virtual_streaming = bool(
            destination.get_editor_property("virtual_texture_streaming")
        )
    except Exception as exc:
        raise PipelineError(
            f"Could not validate native HUD texture residency policy: {exc}"
        ) from exc
    if "TEXTUREGROUP_UI" not in lod_group.upper():
        raise PipelineError(
            f"Imported native HUD texture is not TEXTUREGROUP_UI: {lod_group}"
        )
    if not never_stream or virtual_streaming:
        raise PipelineError(
            "Imported native HUD texture is not fully resident"
        )


def import_one(
    variant: dict[str, Any],
    source_entry: dict[str, Any],
    variant_png: Path,
    existing: bool,
    warnings: list[str],
) -> Any:
    destination_package = variant["destination_package"]
    destination_path, destination_name = destination_package.rsplit("/", 1)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(variant_png))
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", existing)
    task.set_editor_property("replace_existing_settings", False)
    task.set_editor_property("save", False)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported_paths = [
        str(path) for path in task.get_editor_property("imported_object_paths")
    ]
    if variant["destination_object"] not in imported_paths:
        # Some engine importers omit imported_object_paths on a managed
        # replacement; loading and validating the exact destination is the
        # authoritative gate in that case.
        warnings.append(
            "Importer did not report the expected object path; validating the "
            f"loaded destination instead: {imported_paths!r}"
        )

    destination = unreal.EditorAssetLibrary.load_asset(destination_package)
    if (
        destination is None
        or destination.get_class().get_name() != "Texture2D"
    ):
        raise PipelineError(
            f"Imported destination is not a Texture2D: {destination_package}"
        )
    source = unreal.EditorAssetLibrary.load_asset(source_entry["object_path"])
    if source is None or source.get_class().get_name() != "Texture2D":
        raise PipelineError(
            f"Source Texture2D cannot be loaded: {source_entry['object_path']}"
        )

    copy_texture_settings(source, destination, warnings)
    apply_variant_texture_policy(
        destination, variant["texture_policy"]
    )
    set_pipeline_metadata(destination, variant)
    try:
        destination.post_edit_change()
    except Exception:
        pass
    if not unreal.EditorAssetLibrary.save_loaded_asset(
        destination, only_if_is_dirty=False
    ):
        raise PipelineError(f"Failed to save {destination_package}")
    return destination


def main() -> int:
    arguments = parse_arguments()
    config = load_config()
    project_root = Path(unreal.Paths.project_dir()).resolve()
    assert_target_project(project_root, config["project_file"])
    if arguments.project_root and Path(arguments.project_root).resolve() != project_root:
        raise PipelineError(
            "The open editor project differs from --project-root"
        )

    engine_version = unreal.SystemLibrary.get_engine_version()
    if not engine_version.startswith("5.8."):
        raise PipelineError(f"Expected UE 5.8, found {engine_version}")
    editor_safety = assert_safe_editor_state()
    validate_variant_texture_policy_support(
        config["variant_texture_policy"]
    )

    generated_root = output_root(project_root, config)
    images_root = (generated_root / "Generated").resolve()
    source_manifest_path = generated_root / SOURCE_MANIFEST_NAME
    variant_manifest_path = generated_root / VARIANT_MANIFEST_NAME
    validation_path = generated_root / VALIDATION_NAME
    report_path = generated_root / (
        DRY_RUN_MANIFEST_NAME if arguments.dry_run else IMPORT_MANIFEST_NAME
    )

    for required_file in (
        source_manifest_path,
        variant_manifest_path,
        validation_path,
    ):
        if not required_file.is_file():
            raise PipelineError(f"Required pipeline file is absent: {required_file}")

    source_manifest = load_json(source_manifest_path)
    variant_manifest = load_json(variant_manifest_path)
    validation = load_json(validation_path)
    if source_manifest.get("status") != "PASS":
        raise PipelineError("Source manifest status is not PASS")
    if variant_manifest.get("status") != "PASS":
        raise PipelineError("Variant manifest status is not PASS")
    if validation.get("status") != "PASS":
        raise PipelineError("Offline variant validation status is not PASS")
    if config_sha256() != variant_manifest.get("config_sha256"):
        raise PipelineError("Configuration changed after PNG generation")
    if source_manifest.get("common_script_sha256") != sha256_file(
        SCRIPT_DIR / "hud_theme_common.py"
    ):
        raise PipelineError("Shared pipeline code changed after export")
    if source_manifest.get("exporter_script_sha256") != sha256_file(
        SCRIPT_DIR / "Export-HudThemeSources.py"
    ):
        raise PipelineError("Exporter code changed after export")
    if variant_manifest.get("common_script_sha256") != sha256_file(
        SCRIPT_DIR / "hud_theme_common.py"
    ):
        raise PipelineError("Shared pipeline code changed after generation")
    if variant_manifest.get("generator_script_sha256") != sha256_file(
        SCRIPT_DIR / "Generate-HudThemeVariants.py"
    ):
        raise PipelineError("Generator code changed after PNG generation")
    if validation.get("validator_script_sha256") != sha256_file(
        SCRIPT_DIR / "Validate-HudThemeVariants.py"
    ):
        raise PipelineError("Validator code changed after offline validation")
    if (
        validation.get("source_manifest_sha256")
        != sha256_file(source_manifest_path)
    ):
        raise PipelineError("Validated source manifest hash changed")
    if (
        validation.get("variant_manifest_sha256")
        != sha256_file(variant_manifest_path)
    ):
        raise PipelineError("Validated variant manifest hash changed")

    sources = {
        entry["package_name"]: entry for entry in source_manifest.get("assets", [])
    }
    selected_variants = [
        row
        for row in variant_manifest.get("variants", [])
        if arguments.theme is None or row["theme"] == arguments.theme
    ]
    if not selected_variants:
        raise PipelineError("No validated variants matched the requested import")

    content_root = (project_root / "Content").resolve()
    protected_output_root = (
        content_root / "_Game" / "Textures" / "UI" / "Themes"
    ).resolve()

    # Validate the complete requested pack before the first Unreal asset is
    # imported or replaced. This prevents a deterministic late validation
    # error (ownership, hash, settings, dimensions, or path) from leaving a
    # partially updated color family on disk.
    preflight_errors: list[dict[str, str]] = []
    for variant in selected_variants:
        destination = variant.get("destination_package", "<destination>")
        try:
            source_entry = sources.get(variant["source_package"])
            if source_entry is None:
                raise PipelineError("Source entry is absent from source manifest")
            validate_asset_name(variant["asset_name"])
            validate_unreal_package(destination, config["destination_root"])
            expected_object = f"{destination}.{variant['asset_name']}"
            if variant["destination_object"] != expected_object:
                raise PipelineError("Destination AssetName changed")
            if (
                variant.get("texture_policy")
                != config["variant_texture_policy"]
            ):
                raise PipelineError(
                    "Validated variant texture policy changed"
                )

            source = unreal.EditorAssetLibrary.load_asset(
                source_entry["object_path"]
            )
            if source is None or source.get_class().get_name() != "Texture2D":
                raise PipelineError(
                    f"Source Texture2D cannot be loaded: {source_entry['object_path']}"
                )
            live_source_settings = capture_settings(source)
            if live_source_settings != source_entry.get("source_settings", {}):
                raise PipelineError(
                    "Source Texture2D settings changed after export; rerun export, "
                    "generation, and validation"
                )
            if texture_size(source) != (
                source_entry["width"],
                source_entry["height"],
            ):
                raise PipelineError(
                    "Source Texture2D dimensions changed after export"
                )
            source_package_file = game_package_file(
                project_root, variant["source_package"]
            )
            if source_entry.get(
                "source_package_file"
            ) != source_package_file.relative_to(project_root).as_posix():
                raise PipelineError("Source package file path changed after export")
            live_source_package_sha256 = sha256_file(source_package_file)
            if (
                source_entry.get("source_package_file_sha256")
                != live_source_package_sha256
                or variant.get("source_package_file_sha256")
                != live_source_package_sha256
            ):
                raise PipelineError(
                    "Source .uasset changed after export; rerun export, "
                    "generation, and validation"
                )

            variant_path = (
                generated_root / Path(variant["variant_png"])
            ).resolve()
            ensure_within(variant_path, images_root)
            if not variant_path.is_file():
                raise PipelineError(f"Validated PNG is absent: {variant_path}")
            if sha256_file(variant_path) != variant["png_sha256"]:
                raise PipelineError("Validated PNG hash changed before import")

            package_relative = destination[len("/Game/") :] + ".uasset"
            physical_package = (content_root / Path(package_relative)).resolve()
            ensure_within(physical_package, protected_output_root)

            if asset_exists(destination, variant["destination_object"]):
                existing_asset = unreal.EditorAssetLibrary.load_asset(destination)
                if (
                    existing_asset is None
                    or existing_asset.get_class().get_name() != "Texture2D"
                ):
                    raise PipelineError(
                        "Existing destination is not a Texture2D"
                    )
                owner = metadata_value(existing_asset, OWNER_TAG)
                if owner != OWNER_VALUE:
                    raise PipelineError(
                        "Refusing to overwrite an existing asset not owned by "
                        f"{OWNER_VALUE}: owner={owner!r}"
                    )
        except Exception as exc:
            preflight_errors.append(
                {"destination": destination, "error": str(exc)}
            )

    if preflight_errors:
        preflight_payload = {
            "schema_version": 1,
            "status": "FAIL",
            "mode": "DRY_RUN" if arguments.dry_run else "IMPORT",
            "phase": "PREFLIGHT",
            "engine_version": engine_version,
            "editor_safety": editor_safety,
            "requested_theme": arguments.theme or "ALL",
            "owner_metadata": OWNER_VALUE,
            "selected_variant_count": len(selected_variants),
            "successful_count": 0,
            "assets": [],
            "errors": preflight_errors,
            "warnings": [],
        }
        write_json_if_changed(report_path, preflight_payload)
        raise PipelineError(
            "HUD theme import preflight failed before mutation with "
            f"{len(preflight_errors)} error(s); inspect {report_path}"
        )

    imported: list[dict[str, Any]] = []
    errors: list[dict[str, str]] = []
    global_warnings: list[str] = []

    for variant in selected_variants:
        destination = variant.get("destination_package", "<destination>")
        try:
            source_entry = sources.get(variant["source_package"])
            if source_entry is None:
                raise PipelineError("Source entry is absent from source manifest")
            validate_asset_name(variant["asset_name"])
            validate_unreal_package(destination, config["destination_root"])
            expected_object = f"{destination}.{variant['asset_name']}"
            if variant["destination_object"] != expected_object:
                raise PipelineError("Destination AssetName changed")
            if (
                variant.get("texture_policy")
                != config["variant_texture_policy"]
            ):
                raise PipelineError(
                    "Validated variant texture policy changed"
                )

            source = unreal.EditorAssetLibrary.load_asset(
                source_entry["object_path"]
            )
            if source is None or source.get_class().get_name() != "Texture2D":
                raise PipelineError(
                    f"Source Texture2D cannot be loaded: {source_entry['object_path']}"
                )
            live_source_settings = capture_settings(source)
            if live_source_settings != source_entry.get("source_settings", {}):
                raise PipelineError(
                    "Source Texture2D settings changed after export; rerun export, "
                    "generation, and validation"
                )
            if texture_size(source) != (
                source_entry["width"],
                source_entry["height"],
            ):
                raise PipelineError(
                    "Source Texture2D dimensions changed after export"
                )
            source_package_file = game_package_file(
                project_root, variant["source_package"]
            )
            if source_entry.get(
                "source_package_file"
            ) != source_package_file.relative_to(project_root).as_posix():
                raise PipelineError("Source package file path changed after export")
            live_source_package_sha256 = sha256_file(source_package_file)
            if (
                source_entry.get("source_package_file_sha256")
                != live_source_package_sha256
                or variant.get("source_package_file_sha256")
                != live_source_package_sha256
            ):
                raise PipelineError(
                    "Source .uasset changed after export; rerun export, "
                    "generation, and validation"
                )

            variant_path = (
                generated_root / Path(variant["variant_png"])
            ).resolve()
            ensure_within(variant_path, images_root)
            if not variant_path.is_file():
                raise PipelineError(f"Validated PNG is absent: {variant_path}")
            if sha256_file(variant_path) != variant["png_sha256"]:
                raise PipelineError("Validated PNG hash changed before import")

            package_relative = destination[len("/Game/") :] + ".uasset"
            physical_package = (content_root / Path(package_relative)).resolve()
            ensure_within(physical_package, protected_output_root)

            object_path = variant["destination_object"]
            existing = asset_exists(destination, object_path)
            per_asset_warnings: list[str] = []
            existing_asset = (
                unreal.EditorAssetLibrary.load_asset(destination)
                if existing
                else None
            )
            action = "WOULD_IMPORT" if arguments.dry_run else "IMPORTED"

            if existing:
                if (
                    existing_asset is None
                    or existing_asset.get_class().get_name() != "Texture2D"
                ):
                    raise PipelineError(
                        "Existing destination is not a Texture2D"
                    )
                owner = metadata_value(existing_asset, OWNER_TAG)
                if owner != OWNER_VALUE:
                    raise PipelineError(
                        "Refusing to overwrite an existing asset not owned by "
                        f"{OWNER_VALUE}: owner={owner!r}"
                    )
                # Ownership metadata is an overwrite boundary, not proof that
                # the Texture2D pixels still match the generated PNG. Unreal
                # metadata can survive a manual reimport, so every managed
                # destination is deliberately refreshed from the validated
                # native source rather than risking a false "unchanged" skip.
                if arguments.dry_run:
                    action = "WOULD_UPDATE_MANAGED"
                    imported_asset = existing_asset
                else:
                    action = "UPDATED_MANAGED"
                    imported_asset = import_one(
                        variant,
                        source_entry,
                        variant_path,
                        True,
                        per_asset_warnings,
                    )
            elif arguments.dry_run:
                imported_asset = None
            else:
                imported_asset = import_one(
                    variant,
                    source_entry,
                    variant_path,
                    False,
                    per_asset_warnings,
                )

            if not arguments.dry_run:
                if texture_size(imported_asset) != (
                    variant["width"],
                    variant["height"],
                ):
                    raise PipelineError("Imported Texture2D dimensions changed")
                validate_variant_texture_policy(imported_asset)
                if metadata_value(imported_asset, OWNER_TAG) != OWNER_VALUE:
                    raise PipelineError("Pipeline ownership metadata was not saved")
                if (
                    metadata_value(imported_asset, FINGERPRINT_TAG)
                    != variant["import_fingerprint_sha256"]
                ):
                    raise PipelineError("Import fingerprint metadata was not saved")
                if not physical_package.is_file():
                    raise PipelineError(
                        f"Imported package file is absent: {physical_package}"
                    )

            imported.append(
                {
                    "action": action,
                    "asset": object_path,
                    "package_file": (
                        physical_package.relative_to(project_root).as_posix()
                        if physical_package.is_file()
                        else None
                    ),
                    "package_file_sha256": (
                        sha256_file(physical_package)
                        if not arguments.dry_run and physical_package.is_file()
                        else None
                    ),
                    "source": variant["source_asset"],
                    "theme": variant["theme"],
                    "variant_png_sha256": variant["png_sha256"],
                    "warnings": per_asset_warnings,
                }
            )
            global_warnings.extend(
                f"{object_path}: {warning}" for warning in per_asset_warnings
            )
        except Exception as exc:
            errors.append({"destination": destination, "error": str(exc)})

    dirty_after = dirty_editor_packages()
    if dirty_after["content"] or dirty_after["maps"]:
        errors.append(
            {
                "destination": "<editor-state>",
                "error": (
                    "Pipeline left dirty packages after "
                    f"{'dry run' if arguments.dry_run else 'import'}: "
                    f"content={dirty_after['content']!r}, "
                    f"maps={dirty_after['maps']!r}"
                ),
            }
        )

    payload = {
        "schema_version": 1,
        "status": "PASS" if not errors else "FAIL",
        "mode": "DRY_RUN" if arguments.dry_run else "IMPORT",
        "engine_version": engine_version,
        "editor_safety": {
            **editor_safety,
            "dirty_content_packages_after": dirty_after["content"],
            "dirty_map_packages_after": dirty_after["maps"],
        },
        "common_script_sha256": sha256_file(
            SCRIPT_DIR / "hud_theme_common.py"
        ),
        "importer_script_sha256": sha256_file(Path(__file__)),
        "requested_theme": arguments.theme or "ALL",
        "owner_metadata": OWNER_VALUE,
        "source_manifest_sha256": sha256_file(source_manifest_path),
        "variant_manifest_sha256": sha256_file(variant_manifest_path),
        "validation_sha256": sha256_file(validation_path),
        "selected_variant_count": len(selected_variants),
        "successful_count": len(imported),
        "assets": imported,
        "errors": errors,
        "warnings": global_warnings,
    }
    write_json_if_changed(report_path, payload)

    if errors:
        raise PipelineError(
            f"HUD theme import {'dry run ' if arguments.dry_run else ''}failed "
            f"with {len(errors)} error(s); inspect {report_path}"
        )
    log(
        f"PASS ({payload['mode']}): {len(imported)} validated theme textures; "
        f"report {report_path.relative_to(project_root)}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exception:
        unreal.log_error("HUD_THEME_IMPORT_FATAL: " + str(exception))
        raise
