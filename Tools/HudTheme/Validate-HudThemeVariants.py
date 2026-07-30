"""Validate native HUD theme PNGs without loading or changing Unreal assets."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

try:
    from PIL import Image, ImageChops
except ImportError as exc:
    raise SystemExit(
        "Pillow is required. Install Tools/HudTheme/requirements.txt."
    ) from exc


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from hud_theme_common import (  # noqa: E402
    PipelineError,
    REQUIRED_NATIVE_TEXTURE_COUNT,
    REQUIRED_VARIANT_COUNT,
    THEME_IDS,
    config_sha256,
    ensure_within,
    find_project_root,
    game_package_file,
    join_package,
    load_config,
    load_json,
    output_root,
    sha256_bytes,
    sha256_file,
    stable_json_bytes,
    validate_asset_name,
    validate_unreal_package,
    write_json_if_changed,
)


SOURCE_MANIFEST_NAME = "HudThemeSourceManifest.json"
VARIANT_MANIFEST_NAME = "HudThemeVariantManifest.json"
VALIDATION_NAME = "HudThemeVariantValidation.json"
PURPLE_GATE = {
    "alpha_min_byte": 16,
    "saturation_min_byte": 46,
    "value_min_byte": 20,
    "hue_byte_range_inclusive": (177, 245),
}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root")
    return parser.parse_args()


def image_invariants(image: Image.Image) -> dict[str, str]:
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A")
    luminance = rgba.convert("RGB").convert("L")
    silhouette = alpha.point(lambda value: 255 if value > 0 else 0)
    return {
        "alpha_sha256": sha256_bytes(alpha.tobytes()),
        "luminance_sha256": sha256_bytes(luminance.tobytes()),
        "silhouette_sha256": sha256_bytes(silhouette.tobytes()),
    }


def binary_lut(low: int, high: int = 255) -> list[int]:
    return [
        255 if low <= value <= high else 0
        for value in range(256)
    ]


def forbidden_purple_metrics(image: Image.Image) -> dict[str, Any]:
    rgba = image.convert("RGBA")
    hue, saturation, value = rgba.convert("RGB").convert("HSV").split()
    alpha_min = PURPLE_GATE["alpha_min_byte"]
    saturation_min = PURPLE_GATE["saturation_min_byte"]
    value_min = PURPLE_GATE["value_min_byte"]
    hue_low, hue_high = PURPLE_GATE["hue_byte_range_inclusive"]

    eligible = ImageChops.multiply(
        rgba.getchannel("A").point(binary_lut(alpha_min)),
        saturation.point(binary_lut(saturation_min)),
    )
    eligible = ImageChops.multiply(
        eligible,
        value.point(binary_lut(value_min)),
    )
    forbidden = ImageChops.multiply(
        eligible,
        hue.point(binary_lut(hue_low, hue_high)),
    )
    eligible_count = eligible.histogram()[255]
    forbidden_count = forbidden.histogram()[255]
    return {
        "eligible_chromatic_pixel_count": eligible_count,
        "forbidden_pixel_count": forbidden_count,
        "forbidden_ratio": (
            forbidden_count / eligible_count
            if eligible_count
            else 0.0
        ),
        "bounding_box_xyxy": (
            list(forbidden.getbbox())
            if forbidden_count
            else None
        ),
    }


def main() -> int:
    arguments = parse_arguments()
    config = load_config()
    project_root = find_project_root(arguments.project_root)
    generated_root = output_root(project_root, config)
    images_root = (generated_root / "Generated").resolve()
    source_manifest_path = generated_root / SOURCE_MANIFEST_NAME
    variant_manifest_path = generated_root / VARIANT_MANIFEST_NAME
    validation_path = generated_root / VALIDATION_NAME

    if not source_manifest_path.is_file():
        raise PipelineError(f"Source manifest is absent: {source_manifest_path}")
    if not variant_manifest_path.is_file():
        raise PipelineError(f"Variant manifest is absent: {variant_manifest_path}")

    source_manifest = load_json(source_manifest_path)
    variant_manifest = load_json(variant_manifest_path)
    errors: list[dict[str, str]] = []
    warnings: list[str] = []

    if source_manifest.get("status") != "PASS":
        errors.append({"scope": "source_manifest", "error": "Status is not PASS"})
    if variant_manifest.get("status") != "PASS":
        errors.append({"scope": "variant_manifest", "error": "Status is not PASS"})
    if source_manifest.get("config_sha256") != config_sha256():
        errors.append(
            {"scope": "source_manifest", "error": "Configuration hash mismatch"}
        )
    if variant_manifest.get("config_sha256") != config_sha256():
        errors.append(
            {"scope": "variant_manifest", "error": "Configuration hash mismatch"}
        )
    if source_manifest.get("common_script_sha256") != sha256_file(
        SCRIPT_DIR / "hud_theme_common.py"
    ):
        errors.append(
            {"scope": "source_manifest", "error": "Shared script hash mismatch"}
        )
    if source_manifest.get("exporter_script_sha256") != sha256_file(
        SCRIPT_DIR / "Export-HudThemeSources.py"
    ):
        errors.append(
            {"scope": "source_manifest", "error": "Exporter script hash mismatch"}
        )
    if variant_manifest.get("common_script_sha256") != sha256_file(
        SCRIPT_DIR / "hud_theme_common.py"
    ):
        errors.append(
            {"scope": "variant_manifest", "error": "Shared script hash mismatch"}
        )
    if variant_manifest.get("generator_script_sha256") != sha256_file(
        SCRIPT_DIR / "Generate-HudThemeVariants.py"
    ):
        errors.append(
            {"scope": "variant_manifest", "error": "Generator script hash mismatch"}
        )
    if (
        variant_manifest.get("source_manifest_sha256")
        != sha256_file(source_manifest_path)
    ):
        errors.append(
            {
                "scope": "variant_manifest",
                "error": "Source manifest hash mismatch",
            }
        )
    if tuple(variant_manifest.get("theme_ids", [])) != THEME_IDS:
        errors.append(
            {"scope": "variant_manifest", "error": "Required theme set mismatch"}
        )

    expected_profile_hashes = {
        profile["id"]: sha256_bytes(stable_json_bytes(profile))
        for profile in config["themes"]
    }
    if variant_manifest.get("theme_profile_sha256") != expected_profile_hashes:
        errors.append(
            {"scope": "variant_manifest", "error": "Theme profile hash mismatch"}
        )

    source_entries = source_manifest.get("assets", [])
    sources: dict[str, dict[str, Any]] = {
        entry["package_name"]: entry for entry in source_entries
    }
    configured_packages = {
        package.casefold(): package
        for package in config["native_texture_packages"]
    }
    manifest_packages = {
        package.casefold(): package
        for package in sources
    }
    missing_source_packages = sorted(
        (
            configured_packages[key]
            for key in configured_packages.keys() - manifest_packages.keys()
        ),
        key=str.casefold,
    )
    unexpected_source_packages = sorted(
        (
            manifest_packages[key]
            for key in manifest_packages.keys() - configured_packages.keys()
        ),
        key=str.casefold,
    )
    source_coverage_errors = (
        len(source_entries) != REQUIRED_NATIVE_TEXTURE_COUNT
        or len(sources) != REQUIRED_NATIVE_TEXTURE_COUNT
        or source_manifest.get("asset_count")
        != REQUIRED_NATIVE_TEXTURE_COUNT
        or source_manifest.get("audited_native_texture_count")
        != REQUIRED_NATIVE_TEXTURE_COUNT
        or bool(missing_source_packages)
        or bool(unexpected_source_packages)
    )
    if source_coverage_errors:
        errors.append(
            {
                "scope": "source_coverage",
                "error": (
                    "The source manifest must cover exactly the 74 configured "
                    "project HUD textures"
                ),
            }
        )

    expected_count = REQUIRED_VARIANT_COUNT
    variants = variant_manifest.get("variants", [])
    if variant_manifest.get("source_asset_count") != REQUIRED_NATIVE_TEXTURE_COUNT:
        errors.append(
            {
                "scope": "variant_manifest",
                "error": (
                    "source_asset_count must be exactly "
                    f"{REQUIRED_NATIVE_TEXTURE_COUNT}"
                ),
            }
        )
    if variant_manifest.get("variant_count") != REQUIRED_VARIANT_COUNT:
        errors.append(
            {
                "scope": "variant_manifest",
                "error": (
                    "variant_count must be exactly "
                    f"{REQUIRED_VARIANT_COUNT}"
                ),
            }
        )
    if len(variants) != expected_count:
        errors.append(
            {
                "scope": "variant_manifest",
                "error": f"Expected {expected_count} rows, found {len(variants)}",
            }
        )

    source_cache: dict[str, tuple[tuple[int, int], dict[str, str]]] = {}
    seen_pairs: set[tuple[str, str]] = set()
    seen_destinations: set[str] = set()
    tracked_pngs: set[Path] = set()
    validated_count = 0
    purple_gate_checked_count = 0
    purple_gate_forbidden_count = 0
    purple_gate_findings: list[dict[str, Any]] = []

    for variant in variants:
        scope = (
            f"{variant.get('theme', '<theme>')}:"
            f"{variant.get('source_package', '<source>')}"
        )
        try:
            theme = variant["theme"]
            if theme not in THEME_IDS:
                raise PipelineError(f"Unsupported theme {theme}")
            source_package = variant["source_package"]
            source = sources.get(source_package)
            if source is None:
                raise PipelineError("Variant source is absent from source manifest")
            source_package_file = game_package_file(project_root, source_package)
            expected_source_relative = source_package_file.relative_to(
                project_root
            ).as_posix()
            if source.get("source_package_file") != expected_source_relative:
                raise PipelineError("Source package file path mismatch")
            live_source_package_sha256 = sha256_file(source_package_file)
            if (
                source.get("source_package_file_sha256")
                != live_source_package_sha256
            ):
                raise PipelineError(
                    "Source .uasset changed after export; rerun the pipeline"
                )
            if (
                variant.get("source_package_file_sha256")
                != live_source_package_sha256
            ):
                raise PipelineError("Variant source .uasset hash mismatch")
            pair = (theme, source_package.casefold())
            if pair in seen_pairs:
                raise PipelineError("Duplicate theme/source variant row")
            seen_pairs.add(pair)

            asset_name = source["asset_name"]
            validate_asset_name(asset_name)
            if variant["asset_name"] != asset_name:
                raise PipelineError("AssetName changed")
            if not source_package.startswith("/Game/"):
                raise PipelineError("Source is not a /Game package")
            original_relative = source_package[len("/Game/") :]
            expected_destination = join_package(
                config["destination_root"], theme, original_relative
            )
            validate_unreal_package(
                expected_destination, config["destination_root"]
            )
            if variant["destination_package"] != expected_destination:
                raise PipelineError(
                    "Destination does not preserve the full path relative to /Game"
                )
            expected_object = f"{expected_destination}.{asset_name}"
            if variant["destination_object"] != expected_object:
                raise PipelineError("Destination object path or AssetName changed")
            if expected_object.casefold() in seen_destinations:
                raise PipelineError("Destination object collision")
            seen_destinations.add(expected_object.casefold())

            expected_variant_relative = (
                Path("Generated") / theme / Path(original_relative + ".png")
            )
            actual_variant_relative = Path(variant["variant_png"])
            if actual_variant_relative.as_posix() != expected_variant_relative.as_posix():
                raise PipelineError("Variant PNG path is not reversible from source")
            variant_path = (generated_root / actual_variant_relative).resolve()
            ensure_within(variant_path, images_root)
            tracked_pngs.add(variant_path)
            if not variant_path.is_file():
                raise PipelineError(f"Variant PNG is absent: {variant_path}")
            if sha256_file(variant_path) != variant["png_sha256"]:
                raise PipelineError("Variant PNG SHA-256 mismatch")

            if source_package not in source_cache:
                source_path = (
                    generated_root / Path(source["source_png"])
                ).resolve()
                ensure_within(source_path, generated_root / "SourceTextures")
                if not source_path.is_file():
                    raise PipelineError(f"Source PNG is absent: {source_path}")
                if sha256_file(source_path) != source["source_png_sha256"]:
                    raise PipelineError("Source PNG SHA-256 mismatch")
                with Image.open(source_path) as source_opened:
                    source_opened.load()
                    source_size = source_opened.size
                    source_invariants = image_invariants(source_opened)
                source_cache[source_package] = (source_size, source_invariants)

            source_size, source_invariants = source_cache[source_package]
            with Image.open(variant_path) as opened:
                opened.load()
                if opened.mode != "RGBA":
                    raise PipelineError(
                        f"Generated PNG must be RGBA, found {opened.mode}"
                    )
                variant_size = opened.size
                variant_invariants = image_invariants(opened)
                if theme != "Purple":
                    purple_metrics = forbidden_purple_metrics(opened)
                    purple_gate_checked_count += 1
                    forbidden_count = purple_metrics[
                        "forbidden_pixel_count"
                    ]
                    purple_gate_forbidden_count += forbidden_count
                    if forbidden_count:
                        purple_gate_findings.append(
                            {
                                "theme": theme,
                                "source_package": source_package,
                                "variant_png": actual_variant_relative.as_posix(),
                                **purple_metrics,
                            }
                        )
                        raise PipelineError(
                            "Forbidden purple hue remains in a non-Purple "
                            f"theme ({forbidden_count} pixels)"
                        )

            if variant_size != source_size:
                raise PipelineError(
                    f"Dimensions changed: {source_size} -> {variant_size}"
                )
            if variant_size != (variant["width"], variant["height"]):
                raise PipelineError("Manifest dimensions do not match PNG")
            for invariant_name in (
                "alpha_sha256",
                "luminance_sha256",
                "silhouette_sha256",
            ):
                if variant_invariants[invariant_name] != source_invariants[invariant_name]:
                    raise PipelineError(
                        f"Source/variant invariant mismatch: {invariant_name}"
                    )
                if variant[invariant_name] != variant_invariants[invariant_name]:
                    raise PipelineError(
                        f"Variant manifest mismatch: {invariant_name}"
                    )
                source_manifest_key = "source_" + invariant_name
                if variant[source_manifest_key] != source_invariants[invariant_name]:
                    raise PipelineError(
                        f"Source manifest mismatch: {source_manifest_key}"
                    )
            if variant["profile_sha256"] != expected_profile_hashes[theme]:
                raise PipelineError("Variant profile hash mismatch")
            if (
                variant.get("texture_policy")
                != config["variant_texture_policy"]
            ):
                raise PipelineError("Variant texture residency policy mismatch")
            expected_import_fingerprint = sha256_bytes(
                stable_json_bytes(
                    {
                        "png_sha256": variant["png_sha256"],
                        "profile_sha256": variant["profile_sha256"],
                        "source_asset": source["object_path"],
                        "source_package_file_sha256": source[
                            "source_package_file_sha256"
                        ],
                        "source_settings": source.get("source_settings", {}),
                        "texture_policy": config[
                            "variant_texture_policy"
                        ],
                    }
                )
            )
            if (
                variant.get("import_fingerprint_sha256")
                != expected_import_fingerprint
            ):
                raise PipelineError("Import fingerprint mismatch")
            validated_count += 1
        except Exception as exc:
            errors.append({"scope": scope, "error": str(exc)})

    expected_purple_gate_count = (
        REQUIRED_NATIVE_TEXTURE_COUNT * (len(THEME_IDS) - 1)
    )
    if purple_gate_checked_count != expected_purple_gate_count:
        errors.append(
            {
                "scope": "purple_hue_gate",
                "error": (
                    f"Expected {expected_purple_gate_count} checked variants, "
                    f"found {purple_gate_checked_count}"
                ),
            }
        )

    if images_root.is_dir():
        untracked = sorted(
            (
                path.relative_to(project_root).as_posix()
                for path in images_root.rglob("*.png")
                if path.resolve() not in tracked_pngs
            ),
            key=str.casefold,
        )
        if untracked:
            warnings.append(
                f"{len(untracked)} untracked generated PNG(s) were left untouched: "
                + ", ".join(untracked[:10])
                + (" ..." if len(untracked) > 10 else "")
            )

    payload = {
        "schema_version": 2,
        "status": "PASS" if not errors else "FAIL",
        "config_sha256": config_sha256(),
        "common_script_sha256": sha256_file(
            SCRIPT_DIR / "hud_theme_common.py"
        ),
        "validator_script_sha256": sha256_file(Path(__file__)),
        "source_manifest_sha256": sha256_file(source_manifest_path),
        "variant_manifest_sha256": sha256_file(variant_manifest_path),
        "source_asset_count": len(sources),
        "expected_variant_count": expected_count,
        "validated_variant_count": validated_count,
        "source_coverage": {
            "status": "FAIL" if source_coverage_errors else "PASS",
            "required_source_asset_count": REQUIRED_NATIVE_TEXTURE_COUNT,
            "configured_source_asset_count": len(configured_packages),
            "manifest_source_asset_count": len(source_entries),
            "covered_source_asset_count": len(sources),
            "missing_source_packages": missing_source_packages,
            "unexpected_source_packages": unexpected_source_packages,
        },
        "purple_hue_gate": {
            "status": (
                "PASS"
                if purple_gate_checked_count == expected_purple_gate_count
                and purple_gate_forbidden_count == 0
                else "FAIL"
            ),
            "allowed_theme": "Purple",
            "checked_themes": ["Red", "Blue", "Green", "Black"],
            "expected_checked_variant_count": expected_purple_gate_count,
            "checked_variant_count": purple_gate_checked_count,
            "alpha_min_byte": PURPLE_GATE["alpha_min_byte"],
            "saturation_min_byte": PURPLE_GATE[
                "saturation_min_byte"
            ],
            "value_min_byte": PURPLE_GATE["value_min_byte"],
            "hue_byte_range_inclusive": list(
                PURPLE_GATE["hue_byte_range_inclusive"]
            ),
            "hue_degrees_nominal_inclusive": [250, 345],
            "max_forbidden_pixels_per_variant": 0,
            "affected_variant_count": len(purple_gate_findings),
            "forbidden_pixel_count": purple_gate_forbidden_count,
            "findings": purple_gate_findings,
        },
        "errors": errors,
        "warnings": warnings,
    }
    write_json_if_changed(validation_path, payload)

    if errors:
        raise PipelineError(
            f"HUD texture validation failed with {len(errors)} error(s); inspect "
            f"{validation_path}"
        )
    print(
        f"HUD theme validation PASS: {validated_count}/{expected_count} PNGs; "
        "dimensions, alpha, silhouette, luminance, names and paths are exact"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
