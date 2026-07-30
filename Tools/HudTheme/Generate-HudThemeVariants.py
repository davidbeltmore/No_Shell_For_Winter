"""Generate five native HUD texture color families with Pillow.

The transform replaces hue/chroma while preserving the source texture's exact
Pillow luminance channel, alpha bytes, binary alpha silhouette, and dimensions.
Outputs are deterministic RGBA PNGs below Saved/HudThemeRework/Generated.
"""

from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path
from typing import Any

try:
    from PIL import Image, __version__ as PILLOW_VERSION
except ImportError as exc:
    raise SystemExit(
        "Pillow is required. Install Tools/HudTheme/requirements.txt into the "
        "offline Python environment."
    ) from exc


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from hud_theme_common import (  # noqa: E402
    PipelineError,
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
    write_bytes_if_changed,
    write_json_if_changed,
)


SOURCE_MANIFEST_NAME = "HudThemeSourceManifest.json"
VARIANT_MANIFEST_NAME = "HudThemeVariantManifest.json"
LUMA_WEIGHTS = (299, 587, 114)
LUMA_DENOMINATOR = 1000
LUMA_ROUNDING = 500


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root")
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run the in-memory transform invariant test and exit.",
    )
    return parser.parse_args()


def parse_hex_rgb(value: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or len(value) != 7 or not value.startswith("#"):
        raise PipelineError(f"Invalid RGB color: {value!r}")
    try:
        return tuple(int(value[index : index + 2], 16) for index in (1, 3, 5))
    except ValueError as exc:
        raise PipelineError(f"Invalid RGB color: {value!r}") from exc


def integer_luma(rgb: tuple[int, int, int]) -> int:
    return (
        sum(weight * channel for weight, channel in zip(LUMA_WEIGHTS, rgb))
        + LUMA_ROUNDING
    ) // LUMA_DENOMINATOR


def interpolate_rgb(
    first: tuple[int, int, int],
    second: tuple[int, int, int],
    amount: float,
) -> tuple[float, float, float]:
    return tuple(
        first_channel + (second_channel - first_channel) * amount
        for first_channel, second_channel in zip(first, second)
    )


def profile_color(profile: dict[str, Any], luminance: int) -> tuple[float, float, float]:
    shadow = parse_hex_rgb(profile["shadow"])
    midtone = parse_hex_rgb(profile["midtone"])
    highlight = parse_hex_rgb(profile["highlight"])
    if luminance <= 128:
        return interpolate_rgb(shadow, midtone, luminance / 128.0)
    return interpolate_rgb(midtone, highlight, (luminance - 128) / 127.0)


def closest_exact_luma(
    candidate: tuple[int, int, int], target_luma: int
) -> tuple[int, int, int]:
    if integer_luma(candidate) == target_luma:
        return candidate

    for radius in (1, 2, 4, 8):
        best: tuple[tuple[int, int, int], tuple[int, int, int]] | None = None
        for red_delta in range(-radius, radius + 1):
            red = candidate[0] + red_delta
            if not 0 <= red <= 255:
                continue
            for green_delta in range(-radius, radius + 1):
                green = candidate[1] + green_delta
                if not 0 <= green <= 255:
                    continue
                for blue_delta in range(-radius, radius + 1):
                    blue = candidate[2] + blue_delta
                    if not 0 <= blue <= 255:
                        continue
                    corrected = (red, green, blue)
                    if integer_luma(corrected) != target_luma:
                        continue
                    cost = (
                        red_delta * red_delta
                        + green_delta * green_delta
                        + blue_delta * blue_delta,
                        abs(red_delta) + abs(green_delta) + abs(blue_delta),
                        abs(green_delta),
                    )
                    if best is None or cost < best[0]:
                        best = (cost, corrected)
        if best is not None:
            return best[1]
    raise PipelineError(
        f"Could not construct exact-luminance RGB for L={target_luma}: {candidate}"
    )


def build_profile_luts(profile: dict[str, Any]) -> tuple[list[int], list[int], list[int]]:
    saturation = float(profile["saturation"])
    if not 0.0 <= saturation <= 1.0:
        raise PipelineError(
            f"Profile {profile['id']} saturation must be between zero and one"
        )

    channels: list[list[int]] = [[], [], []]
    for luminance in range(256):
        anchor = profile_color(profile, luminance)
        anchor_luma = sum(
            weight * channel
            for weight, channel in zip(LUMA_WEIGHTS, anchor)
        ) / LUMA_DENOMINATOR
        chroma = tuple(channel - anchor_luma for channel in anchor)

        scale = saturation
        for delta in chroma:
            if delta > 0.0:
                scale = min(scale, (255.0 - luminance) / delta)
            elif delta < 0.0:
                scale = min(scale, luminance / -delta)
        scale = max(0.0, scale)

        candidate = tuple(
            max(0, min(255, int(round(luminance + scale * delta))))
            for delta in chroma
        )
        corrected = closest_exact_luma(candidate, luminance)
        for channel_index, channel in enumerate(corrected):
            channels[channel_index].append(channel)

    # Validate against Pillow's implementation, not just the integer model.
    ramp = Image.new("L", (256, 1))
    ramp.putdata(range(256))
    preview = Image.merge(
        "RGB",
        tuple(ramp.point(channel_lut) for channel_lut in channels),
    )
    if preview.convert("L").tobytes() != ramp.tobytes():
        raise PipelineError(
            f"Profile {profile['id']} LUT does not preserve Pillow luminance"
        )
    return channels[0], channels[1], channels[2]


def alpha_silhouette(alpha: Image.Image) -> Image.Image:
    return alpha.point(lambda value: 255 if value > 0 else 0)


def invariant_hashes(image: Image.Image) -> dict[str, str]:
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A")
    luminance = rgba.convert("RGB").convert("L")
    return {
        "alpha_sha256": sha256_bytes(alpha.tobytes()),
        "luminance_sha256": sha256_bytes(luminance.tobytes()),
        "silhouette_sha256": sha256_bytes(alpha_silhouette(alpha).tobytes()),
    }


def make_variant(
    source_rgba: Image.Image,
    luts: tuple[list[int], list[int], list[int]],
) -> Image.Image:
    source_rgb = source_rgba.convert("RGB")
    luminance = source_rgb.convert("L")
    alpha = source_rgba.getchannel("A")
    themed_rgb = Image.merge(
        "RGB",
        tuple(luminance.point(channel_lut) for channel_lut in luts),
    )
    themed = themed_rgb.convert("RGBA")
    themed.putalpha(alpha)

    source_hashes = invariant_hashes(source_rgba)
    themed_hashes = invariant_hashes(themed)
    if source_rgba.size != themed.size:
        raise PipelineError("Variant dimensions changed")
    for key in ("alpha_sha256", "luminance_sha256", "silhouette_sha256"):
        if source_hashes[key] != themed_hashes[key]:
            raise PipelineError(f"Variant invariant changed: {key}")
    return themed


def encode_png(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(
        buffer,
        format="PNG",
        optimize=False,
        compress_level=9,
    )
    return buffer.getvalue()


def run_self_test(config: dict[str, Any]) -> None:
    source = Image.new("RGBA", (256, 3))
    pixels = []
    for row in range(3):
        for value in range(256):
            pixels.append(
                (
                    value,
                    (value * (row + 2) + 17) % 256,
                    255 - value,
                    (value * 37 + row * 53) % 256,
                )
            )
    source.putdata(pixels)
    source_hashes = invariant_hashes(source)

    for profile in config["themes"]:
        variant = make_variant(source, build_profile_luts(profile))
        if invariant_hashes(variant) != source_hashes:
            raise PipelineError(f"Self-test failed for {profile['id']}")
    print("HUD theme Pillow transform self-test: PASS")


def main() -> int:
    arguments = parse_arguments()
    config = load_config()
    if arguments.self_test:
        run_self_test(config)
        return 0

    project_root = find_project_root(arguments.project_root)
    generated_root = output_root(project_root, config)
    source_manifest_path = generated_root / SOURCE_MANIFEST_NAME
    variant_manifest_path = generated_root / VARIANT_MANIFEST_NAME
    generated_images_root = (generated_root / "Generated").resolve()
    ensure_within(generated_images_root, project_root / "Saved")

    if not source_manifest_path.is_file():
        raise PipelineError(
            f"Source manifest is absent: {source_manifest_path}. Run the Unreal "
            "export phase first."
        )
    source_manifest = load_json(source_manifest_path)
    if source_manifest.get("status") != "PASS":
        raise PipelineError("Source manifest does not have PASS status")
    if source_manifest.get("config_sha256") != config_sha256():
        raise PipelineError(
            "Pipeline configuration changed after export; rerun the export phase"
        )
    if source_manifest.get("common_script_sha256") != sha256_file(
        SCRIPT_DIR / "hud_theme_common.py"
    ):
        raise PipelineError(
            "Shared pipeline code changed after export; rerun the export phase"
        )
    if source_manifest.get("exporter_script_sha256") != sha256_file(
        SCRIPT_DIR / "Export-HudThemeSources.py"
    ):
        raise PipelineError(
            "Exporter code changed after export; rerun the export phase"
        )

    profiles = {profile["id"]: profile for profile in config["themes"]}
    if tuple(profiles) != THEME_IDS:
        raise PipelineError("The five required theme profiles are incomplete")
    profile_luts = {
        profile_id: build_profile_luts(profile)
        for profile_id, profile in profiles.items()
    }
    profile_hashes = {
        profile_id: sha256_bytes(stable_json_bytes(profile))
        for profile_id, profile in profiles.items()
    }

    variants: list[dict[str, Any]] = []
    errors: list[dict[str, str]] = []
    destination_objects: set[str] = set()
    assets = source_manifest.get("assets", [])
    if not assets:
        raise PipelineError("Source manifest contains no textures")

    for source_entry in assets:
        source_package = source_entry.get("package_name", "<unknown>")
        try:
            asset_name = source_entry["asset_name"]
            validate_asset_name(asset_name)
            source_package_file = game_package_file(
                project_root, source_package
            )
            if source_entry.get(
                "source_package_file"
            ) != source_package_file.relative_to(project_root).as_posix():
                raise PipelineError("Source package file path mismatch")
            if (
                source_entry.get("source_package_file_sha256")
                != sha256_file(source_package_file)
            ):
                raise PipelineError(
                    "Source .uasset changed after export; rerun the export phase"
                )
            source_png = (
                generated_root / Path(source_entry["source_png"])
            ).resolve()
            ensure_within(source_png, generated_root / "SourceTextures")
            if not source_png.is_file():
                raise PipelineError(f"Source PNG is absent: {source_png}")
            if sha256_file(source_png) != source_entry["source_png_sha256"]:
                raise PipelineError(f"Source PNG hash changed: {source_png}")

            with Image.open(source_png) as opened:
                opened.load()
                source_rgba = opened.convert("RGBA")
                source_mode = opened.mode

            expected_size = (
                int(source_entry["width"]),
                int(source_entry["height"]),
            )
            if source_rgba.size != expected_size:
                raise PipelineError(
                    f"Source PNG dimensions {source_rgba.size} differ from Unreal "
                    f"metadata {expected_size}: {source_package}"
                )
            source_hashes = invariant_hashes(source_rgba)
            relative_package = source_entry["theme_relative_package"]
            if Path(relative_package).name != asset_name:
                raise PipelineError(
                    f"Destination leaf must preserve AssetName: {relative_package}"
                )

            for theme_id in THEME_IDS:
                destination_package = join_package(
                    config["destination_root"], theme_id, relative_package
                )
                validate_unreal_package(
                    destination_package, config["destination_root"]
                )
                destination_object = f"{destination_package}.{asset_name}"
                if destination_object.casefold() in destination_objects:
                    raise PipelineError(
                        f"Generated destination collision: {destination_object}"
                    )
                destination_objects.add(destination_object.casefold())

                variant = make_variant(source_rgba, profile_luts[theme_id])
                variant_hashes = invariant_hashes(variant)
                variant_relative = (
                    Path("Generated")
                    / theme_id
                    / Path(relative_package + ".png")
                )
                variant_path = (generated_root / variant_relative).resolve()
                ensure_within(variant_path, generated_images_root)
                png_bytes = encode_png(variant)
                write_bytes_if_changed(variant_path, png_bytes)
                png_sha256 = sha256_bytes(png_bytes)
                import_fingerprint = sha256_bytes(
                    stable_json_bytes(
                        {
                            "png_sha256": png_sha256,
                            "profile_sha256": profile_hashes[theme_id],
                            "source_asset": source_entry["object_path"],
                            "source_package_file_sha256": source_entry[
                                "source_package_file_sha256"
                            ],
                            "source_settings": source_entry.get(
                                "source_settings", {}
                            ),
                            "texture_policy": config[
                                "variant_texture_policy"
                            ],
                        }
                    )
                )

                variants.append(
                    {
                        "alpha_sha256": variant_hashes["alpha_sha256"],
                        "asset_name": asset_name,
                        "destination_object": destination_object,
                        "destination_package": destination_package,
                        "height": variant.height,
                        "import_fingerprint_sha256": import_fingerprint,
                        "luminance_sha256": variant_hashes["luminance_sha256"],
                        "png_sha256": png_sha256,
                        "profile_sha256": profile_hashes[theme_id],
                        "silhouette_sha256": variant_hashes[
                            "silhouette_sha256"
                        ],
                        "source_alpha_sha256": source_hashes["alpha_sha256"],
                        "source_asset": source_entry["object_path"],
                        "source_luminance_sha256": source_hashes[
                            "luminance_sha256"
                        ],
                        "source_mode": source_mode,
                        "source_package": source_package,
                        "source_package_file_sha256": source_entry[
                            "source_package_file_sha256"
                        ],
                        "source_silhouette_sha256": source_hashes[
                            "silhouette_sha256"
                        ],
                        "theme": theme_id,
                        "texture_policy": config[
                            "variant_texture_policy"
                        ],
                        "variant_png": variant_relative.as_posix(),
                        "width": variant.width,
                    }
                )
        except Exception as exc:
            errors.append({"source_package": source_package, "error": str(exc)})

    payload = {
        "schema_version": 1,
        "status": "PASS" if not errors else "FAIL",
        "method": (
            "Pillow RGB chroma LUT with exact Pillow-L preservation; RGBA alpha "
            "copied byte-for-byte"
        ),
        "config_sha256": config_sha256(),
        "common_script_sha256": sha256_file(
            SCRIPT_DIR / "hud_theme_common.py"
        ),
        "generator_script_sha256": sha256_file(Path(__file__)),
        "pillow_version": PILLOW_VERSION,
        "source_manifest_sha256": sha256_file(source_manifest_path),
        "source_asset_count": len(assets),
        "theme_ids": list(THEME_IDS),
        "theme_profile_sha256": profile_hashes,
        "variant_count": len(variants),
        "variants": sorted(
            variants,
            key=lambda item: (
                THEME_IDS.index(item["theme"]),
                item["source_package"].casefold(),
            ),
        ),
        "errors": errors,
    }
    write_json_if_changed(variant_manifest_path, payload)

    if errors:
        raise PipelineError(
            f"{len(errors)} source texture(s) failed; inspect {variant_manifest_path}"
        )
    expected_count = len(assets) * len(THEME_IDS)
    if len(variants) != expected_count:
        raise PipelineError(
            f"Expected {expected_count} variants, generated {len(variants)}"
        )
    print(
        f"HUD theme generation PASS: {len(assets)} sources x "
        f"{len(THEME_IDS)} themes = {len(variants)} variants"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
