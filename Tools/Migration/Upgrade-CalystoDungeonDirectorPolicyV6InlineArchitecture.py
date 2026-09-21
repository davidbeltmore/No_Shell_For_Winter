"""One-time, exact-package V6 upgrade to the inline Architecture contract.

Run only in the protected NoShellForWinter UE 5.8 editor after a cold native
build. The operation fills newly introduced Architecture fields and applies
the requested Forge/Shrine colors, preserving existing weights, catalogs,
layout, budgets and decals. It saves only the policy and the project-owned
instancing-safe orange Material Instance. It never saves or mutates Calysto
vendor assets or Engine materials.
"""

from __future__ import annotations

import datetime
import hashlib
import json
from pathlib import Path
from typing import Any

import unreal


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
CONTENT_ROOT = Path(unreal.Paths.project_content_dir()).resolve()
V6_PACKAGE = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy"
V6_OBJECT = V6_PACKAGE + ".DA_CalystoDungeonDirectorPolicy"
V6_CLASS = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"
ORANGE_SOURCE = "/Engine/TemplateResources/MI_Template_BaseOrange"
ORANGE_TARGET = "/EFProcedural/Calysto/Internal/Materials/Architecture/MI_Template_BaseOrange"
V6_FILE = CONTENT_ROOT / "_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.uasset"
FORBIDDEN_ROOM_ASSETS = {
    "/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge",
    "/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine",
}
PROTECTED_FILES = {
    "BP_MassiveDungeon": CONTENT_ROOT / "Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset",
    "DA_RoomForge": CONTENT_ROOT / "Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge.uasset",
    "DA_RoomShrine": CONTENT_ROOT / "Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine.uasset",
    "TemplateOrange": Path(unreal.Paths.engine_content_dir()).resolve() / "TemplateResources/MI_Template_BaseOrange.uasset",
    "TemplateMaster": Path(unreal.Paths.engine_content_dir()).resolve() / "TemplateResources/M_Template_Master.uasset",
}
RECEIPT = (
    Path(unreal.Paths.project_saved_dir()).resolve()
    / "Migration/CalystoDungeonDirectorV6/InlineArchitectureUpgrade.json"
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def dirty_packages() -> set[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return {str(value.get_path_name()) for value in values}


def dependency_options() -> Any:
    value = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references",
        "include_soft_package_references",
        "include_hard_management_references",
        "include_soft_management_references",
    ):
        value.set_editor_property(name, True)
    return value


def call_bool(owner: Any, method_name: str, label: str) -> None:
    method = getattr(owner, method_name, None)
    if not callable(method):
        fail(f"{label}: missing native method {method_name}()")
    result = method()
    if isinstance(result, tuple):
        succeeded = bool(result[0]) if result else False
        detail = str(result[1]) if len(result) > 1 else ""
    elif isinstance(result, bool):
        succeeded, detail = result, ""
    elif result is None:
        succeeded, detail = True, ""
    else:
        succeeded, detail = not bool(str(result)), str(result)
    if not succeeded:
        fail(f"{label} failed" + (f": {detail}" if detail else ""))


def inspect_dependencies(registry: Any) -> list[str]:
    registry.scan_paths_synchronous(["/Game/_Game/Data/CalystoDungeon/V6"], True)
    registry.wait_for_completion()
    return sorted(
        {str(value) for value in registry.get_dependencies(V6_PACKAGE, dependency_options())}
    )


result: dict[str, Any] = {
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project": str(PROJECT_FILE),
    "asset": V6_OBJECT,
    "status": "FAIL",
    "saved_packages": [],
}

try:
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail(f"UE 5.8 is required: {unreal.SystemLibrary.get_engine_version()}")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject" or PROJECT_ROOT != EXPECTED_ROOT:
        fail(f"Wrong writable target: {PROJECT_FILE}")
    if not V6_FILE.is_file():
        fail(f"The definitive V6 policy is missing: {V6_FILE}")
    dirty_before = dirty_packages()
    if V6_PACKAGE in dirty_before:
        fail("Save or discard the currently dirty V6 policy before its one-time upgrade")

    protected_before = {}
    for label, path in PROTECTED_FILES.items():
        if not path.is_file():
            fail(f"Protected asset is missing: {path}")
        protected_before[label] = sha256(path)

    source_orange = unreal.load_asset(ORANGE_SOURCE)
    if not isinstance(source_orange, unreal.MaterialInstanceConstant):
        fail("The orange preview source is not a Material Instance Constant")
    usages = (
        unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES,
        unreal.MaterialUsage.MATUSAGE_NANITE,
    )
    result["orange_source_usage"] = {
        str(usage): unreal.MaterialEditingLibrary.has_material_usage(source_orange, usage)
        for usage in usages
    }
    orange = unreal.load_asset(ORANGE_TARGET) if unreal.EditorAssetLibrary.does_asset_exist(ORANGE_TARGET) else None
    if orange is None:
        orange = unreal.EditorAssetLibrary.duplicate_asset(ORANGE_SOURCE, ORANGE_TARGET)
    if not isinstance(orange, unreal.MaterialInstanceConstant):
        fail("Could not create the project-owned instancing-safe orange material")
    for usage in usages:
        if not unreal.MaterialEditingLibrary.has_material_usage(orange, usage):
            unreal.MaterialEditingLibrary.set_material_usage_override(orange, usage, True, True)
    if not all(unreal.MaterialEditingLibrary.has_material_usage(orange, usage) for usage in usages):
        fail("The project-owned orange material is not ready for instancing and Nanite")
    if ORANGE_TARGET in dirty_packages():
        if not unreal.EditorAssetLibrary.save_loaded_asset(orange, only_if_is_dirty=True):
            fail("Could not save the project-owned orange material")
        result["saved_packages"].append(ORANGE_TARGET)
    result["orange_runtime_material"] = str(orange.get_path_name())

    asset = unreal.load_asset(V6_PACKAGE)
    if asset is None or str(asset.get_path_name()) != V6_OBJECT:
        fail(f"Could not load exact V6 object: {V6_OBJECT}")
    if str(asset.get_class().get_path_name()) != V6_CLASS:
        fail(f"Unexpected V6 class: {asset.get_class().get_path_name()}")

    hash_before = sha256(V6_FILE)
    defaults = unreal.new_object(unreal.EFCalystoDungeonDirectorPolicyV6Asset)
    call_bool(defaults, "initialize_v6_defaults", "Native Architecture defaults")
    default_styles = list(defaults.get_editor_property("styles"))
    default_themes = {
        str(theme.get_editor_property("theme_id")).casefold(): theme
        for theme in defaults.get_editor_property("room_themes")
    }
    styles = list(asset.get_editor_property("styles"))
    themes = list(asset.get_editor_property("room_themes"))
    asset.modify()
    for style in styles:
        architecture = style.get_editor_property("architecture")
        if not any(list(architecture.get_editor_property(field)) for field in (
            "floor", "wall", "roof", "wall_door", "door_frame", "door",
            "ramp_top", "ramp_bottom", "wall_lights", "wall_bottom_objects",
            "wall_middle_objects", "wall_top_objects", "roof_objects"
        )):
            style.set_editor_property("architecture", default_styles[0].get_editor_property("architecture"))
    for theme in themes:
        default = default_themes.get(str(theme.get_editor_property("theme_id")).casefold())
        if default is None:
            continue
        architecture = theme.get_editor_property("architecture")
        if not any(list(architecture.get_editor_property(field)) for field in (
            "wall_bottom", "wall_middle", "wall_top", "floor", "corner_bottom",
            "corner_middle", "corner_top", "roof"
        )):
            theme.set_editor_property("architecture", default.get_editor_property("architecture"))
        theme.set_editor_property("room_materials", default.get_editor_property("room_materials"))
        theme.set_editor_property("preview_color", default.get_editor_property("preview_color"))
    asset.set_editor_property("styles", styles)
    asset.set_editor_property("room_themes", themes)
    call_bool(asset, "validate_policy", "Inline Architecture validation")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        fail(f"Could not save exact policy package: {V6_PACKAGE}")
    result["saved_packages"].append(V6_PACKAGE)

    protected_after = {label: sha256(path) for label, path in PROTECTED_FILES.items()}
    if protected_after != protected_before:
        fail("A protected Calysto asset changed during the V6-only upgrade")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    dependencies = inspect_dependencies(registry)
    forbidden = sorted(FORBIDDEN_ROOM_ASSETS.intersection(dependencies))
    if forbidden:
        fail(f"V6 still depends on authored Calysto Room Data Assets: {forbidden}")
    if V6_PACKAGE in dirty_packages():
        fail("The upgraded V6 policy remained dirty after the exact save")

    result.update(
        {
            "status": "PASS",
            "package_sha256_before": hash_before,
            "package_sha256_after": sha256(V6_FILE),
            "dependencies": dependencies,
            "forbidden_room_asset_dependencies": forbidden,
            "protected_hashes_before": protected_before,
            "protected_hashes_after": protected_after,
            "unrelated_dirty_packages_preserved": sorted(dirty_before - {V6_PACKAGE}),
        }
    )
except Exception as exc:
    result["error"] = str(exc)
finally:
    RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    RECEIPT.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unreal.log("CALYSTO_V6_INLINE_ARCHITECTURE_UPGRADE=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(result.get("error", "Inline Architecture upgrade failed"))
