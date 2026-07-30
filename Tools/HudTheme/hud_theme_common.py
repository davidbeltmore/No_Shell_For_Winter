"""Shared, Unreal-independent helpers for the HUD native texture pipeline."""

from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
CONFIG_PATH = SCRIPT_DIR / "HudThemePipeline.json"
UNREAL_SEGMENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
TEXTURE_DIMENSIONS_RE = re.compile(r"^([1-9][0-9]*)x([1-9][0-9]*)$")
UNREAL_TRANSIENT_ADDRESS_RE = re.compile(
    r"\s+\(0x[0-9A-Fa-f]+\)(?=\s*[{>])"
)
THEME_IDS = ("Red", "Blue", "Purple", "Green", "Black")
REQUIRED_NATIVE_TEXTURE_COUNT = 74
REQUIRED_VARIANT_COUNT = REQUIRED_NATIVE_TEXTURE_COUNT * len(THEME_IDS)


class PipelineError(RuntimeError):
    """Raised when a pipeline safety or integrity contract is violated."""


def stable_json_bytes(payload: Any) -> bytes:
    return (
        json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest().upper()


def sha256_file(path: os.PathLike[str] | str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def png_dimensions(path: os.PathLike[str] | str) -> tuple[int, int]:
    """Read the canonical width/height fields from a PNG IHDR chunk."""
    with open(path, "rb") as handle:
        header = handle.read(24)
    if (
        len(header) != 24
        or header[:8] != b"\x89PNG\r\n\x1a\n"
        or header[12:16] != b"IHDR"
    ):
        raise PipelineError(f"Invalid PNG IHDR header: {path}")
    width = int.from_bytes(header[16:20], "big")
    height = int.from_bytes(header[20:24], "big")
    if width <= 0 or height <= 0:
        raise PipelineError(f"Invalid PNG dimensions {width}x{height}: {path}")
    return width, height


def parse_texture_dimensions(value: Any) -> tuple[int, int]:
    """Parse the exact Texture2D `Dimensions` Asset Registry tag."""
    match = TEXTURE_DIMENSIONS_RE.fullmatch(str(value or "").strip())
    if not match:
        raise PipelineError(f"Invalid Texture2D Dimensions tag: {value!r}")
    return int(match.group(1)), int(match.group(2))


def canonical_unreal_property_value(value: Any) -> Any:
    """Serialize editor properties without per-process wrapper addresses."""
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    return UNREAL_TRANSIENT_ADDRESS_RE.sub("", str(value))


def write_bytes_if_changed(path: Path, payload: bytes) -> bool:
    """Atomically write bytes only when content differs. Returns True on change."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_bytes() == payload:
        return False

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=path.name + ".", suffix=".tmp", dir=str(path.parent)
    )
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary_name, path)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)
    return True


def write_json_if_changed(path: Path, payload: Any) -> bool:
    return write_bytes_if_changed(path, stable_json_bytes(payload))


def load_json(path: os.PathLike[str] | str) -> Any:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def load_config() -> dict[str, Any]:
    config = load_json(CONFIG_PATH)
    if config.get("schema_version") != 1:
        raise PipelineError("Unsupported HudThemePipeline.json schema_version")

    themes = config.get("themes", [])
    theme_ids = tuple(theme.get("id") for theme in themes)
    if theme_ids != THEME_IDS:
        raise PipelineError(
            "Theme profiles must be exactly, and in order: "
            + ", ".join(THEME_IDS)
        )

    destination_root = normalize_package_path(config.get("destination_root", ""))
    if destination_root != "/Game/_Game/Textures/UI/Themes":
        raise PipelineError(
            "destination_root must remain project-owned at "
            "/Game/_Game/Textures/UI/Themes"
        )

    texture_policy = config.get("variant_texture_policy")
    required_texture_policy = {
        "lod_group": "TEXTUREGROUP_UI",
        "never_stream": True,
        "virtual_texture_streaming": False,
    }
    if texture_policy != required_texture_policy:
        raise PipelineError(
            "variant_texture_policy must keep every native HUD variant in "
            "TEXTUREGROUP_UI, fully resident, and outside virtual-texture "
            "streaming"
        )

    roots = config.get("source_roots", [])
    if not roots:
        raise PipelineError("No source_roots configured")
    for entry in roots:
        entry["root"] = normalize_package_path(entry.get("root", ""))
        if not entry["root"].startswith("/Game/"):
            raise PipelineError("Only project /Game roots may be scanned")

    native_packages = config.get("native_texture_packages", [])
    if not native_packages:
        raise PipelineError("No audited native_texture_packages configured")
    normalized_packages = [
        normalize_package_path(package_name)
        for package_name in native_packages
    ]
    if len({name.casefold() for name in normalized_packages}) != len(
        normalized_packages
    ):
        raise PipelineError("native_texture_packages contains duplicates")
    if len(normalized_packages) != REQUIRED_NATIVE_TEXTURE_COUNT:
        raise PipelineError(
            "native_texture_packages must contain exactly "
            f"{REQUIRED_NATIVE_TEXTURE_COUNT} audited HUD textures"
        )
    for package_name in normalized_packages:
        if not package_name.startswith("/Game/"):
            raise PipelineError(
                "Only project /Game packages may receive native variants"
            )
        if select_source_root(package_name, roots) is None:
            raise PipelineError(
                f"Audited package is outside all source_roots: {package_name}"
            )
    config["native_texture_packages"] = normalized_packages
    return config


def find_project_root(explicit: str | None = None) -> Path:
    if explicit:
        candidates = [Path(explicit)]
    elif os.environ.get("HUD_THEME_PROJECT_ROOT"):
        candidates = [Path(os.environ["HUD_THEME_PROJECT_ROOT"])]
    else:
        candidates = list(SCRIPT_DIR.parents)

    config = load_config()
    project_file = config["project_file"]
    for candidate in candidates:
        root = candidate.resolve()
        if root.is_file():
            root = root.parent
        if (root / project_file).is_file():
            assert_target_project(root, project_file)
            return root
    raise PipelineError(
        f"Could not locate {project_file}; pass --project-root or set "
        "HUD_THEME_PROJECT_ROOT"
    )


def assert_target_project(root: Path, project_file: str = "NoShellForWinter.uproject") -> None:
    root = root.resolve()
    if root.name.casefold() == "lustasdeadlysin":
        raise PipelineError("Refusing to operate in the read-only source project")
    if not (root / project_file).is_file():
        raise PipelineError(f"Target project file is absent: {root / project_file}")


def output_root(project_root: Path, config: dict[str, Any]) -> Path:
    relative = normalize_relative_path(config["output_directory"])
    result = (project_root / Path(relative)).resolve()
    ensure_within(result, project_root / "Saved")
    return result


def ensure_within(path: Path, parent: Path) -> Path:
    resolved_path = path.resolve()
    resolved_parent = parent.resolve()
    try:
        resolved_path.relative_to(resolved_parent)
    except ValueError as exc:
        raise PipelineError(
            f"Path escapes allowed root: {resolved_path} is not under {resolved_parent}"
        ) from exc
    return resolved_path


def game_package_file(project_root: Path, package_name: str) -> Path:
    """Resolve a /Game package to its physical target-project .uasset file."""
    normalized = normalize_package_path(package_name)
    if not normalized.startswith("/Game/"):
        raise PipelineError(f"Only /Game packages are supported: {package_name}")
    relative = normalize_relative_path(normalized[len("/Game/") :])
    content_root = (project_root / "Content").resolve()
    package_file = (content_root / Path(relative + ".uasset")).resolve()
    ensure_within(package_file, content_root)
    if not package_file.is_file():
        raise PipelineError(f"Physical source package is absent: {package_file}")
    return package_file


def normalize_package_path(value: str) -> str:
    if not isinstance(value, str):
        raise PipelineError("Package path must be a string")
    normalized = "/" + value.strip().replace("\\", "/").strip("/")
    if ".." in PurePosixPath(normalized).parts:
        raise PipelineError(f"Package path contains traversal: {value}")
    return normalized.rstrip("/")


def normalize_relative_path(value: str) -> str:
    if not isinstance(value, str):
        raise PipelineError("Relative path must be a string")
    normalized = value.strip().replace("\\", "/").strip("/")
    parts = PurePosixPath(normalized).parts if normalized else ()
    if any(part in ("", ".", "..") for part in parts):
        raise PipelineError(f"Unsafe relative path: {value}")
    return "/".join(parts)


def join_package(*parts: str) -> str:
    cleaned = [part.strip().replace("\\", "/").strip("/") for part in parts if part]
    result = "/" + "/".join(cleaned)
    return normalize_package_path(result)


def validate_unreal_package(package_name: str, required_prefix: str) -> None:
    package_name = normalize_package_path(package_name)
    required_prefix = normalize_package_path(required_prefix)
    if not (
        package_name == required_prefix
        or package_name.startswith(required_prefix + "/")
    ):
        raise PipelineError(
            f"Destination package escapes {required_prefix}: {package_name}"
        )
    for segment in package_name.strip("/").split("/"):
        if not UNREAL_SEGMENT_RE.fullmatch(segment):
            raise PipelineError(
                f"Unreal package segment is not import-safe: {segment!r}"
            )


def validate_asset_name(asset_name: str) -> None:
    if not UNREAL_SEGMENT_RE.fullmatch(asset_name):
        raise PipelineError(f"Unreal asset name is not import-safe: {asset_name!r}")


def select_source_root(
    package_name: str, roots: Iterable[dict[str, Any]]
) -> dict[str, Any] | None:
    matches = []
    for entry in roots:
        root = entry["root"]
        if package_name == root or package_name.startswith(root + "/"):
            matches.append(entry)
    return max(matches, key=lambda item: len(item["root"])) if matches else None


def source_relative_path(
    package_name: str, root_entry: dict[str, Any]
) -> str:
    root = root_entry["root"]
    relative = package_name[len(root) :].strip("/")
    if not relative:
        raise PipelineError(f"Texture package cannot equal a source root: {package_name}")
    return normalize_relative_path(relative)


def destination_relative_path(
    package_name: str, root_entry: dict[str, Any]
) -> str:
    # Runtime lookup is deliberately reversible:
    # /Game/<original> -> /Game/_Game/Textures/UI/Themes/<Theme>/<original>.
    # The selected root is accepted to keep callers honest about scope, but the
    # destination always preserves the full package path relative to /Game.
    if select_source_root(package_name, [root_entry]) is None:
        raise PipelineError(
            f"Package {package_name} is outside selected root {root_entry['root']}"
        )
    game_prefix = "/Game/"
    if not package_name.startswith(game_prefix):
        raise PipelineError(f"Only /Game packages are supported: {package_name}")
    return normalize_relative_path(package_name[len(game_prefix) :])


def config_sha256() -> str:
    return sha256_file(CONFIG_PATH)
