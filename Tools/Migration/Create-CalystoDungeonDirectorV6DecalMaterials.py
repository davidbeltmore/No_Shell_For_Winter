"""Create or validate Calysto-owned blood decal materials for V6.

This tool consumes exactly two read-only Realistic Blood textures and creates
only project-owned EFProcedural plugin assets. It never duplicates vendor
materials, Blueprints, Niagara systems, or texture packages. First execution is
create-once; later executions are read-only validation. Missing or incompatible
MaterialEditingLibrary APIs fail before creation with explicit diagnostics.
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import traceback
from pathlib import Path
from typing import Any

import unreal


EXPECTED_ROOT = Path(r"D:\Projects UE5\NoShellForWinter").resolve()
PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
PROJECT_FILE = Path(unreal.Paths.get_project_file_path()).resolve()
PROJECT_CONTENT = Path(unreal.Paths.project_content_dir()).resolve()
PLUGIN_CONTENT = (PROJECT_ROOT / "Plugins" / "EFProcedural" / "Content").resolve()
SAVED_ROOT = Path(unreal.Paths.project_saved_dir()).resolve()

SOURCE_TEXTURES = {
    "BloodMaskTexture": {
        "package": "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04",
        "object": "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04.T_Splat_04",
        "file": PROJECT_CONTENT / "RealisticBlood/_Commons/Textures/Decals/T_Splat_04.uasset",
        "sha256": "33B6BD0949AA4F7EC0A6D0FB87307BE1924967A4AE019287F82806649B35DB3D",
    },
    "BloodNormalTexture": {
        "package": "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04",
        "object": "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.T_Splat_N_04",
        "file": PROJECT_CONTENT / "RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.uasset",
        "sha256": "0DE564AFB5AEEF377A7DB46CCC903B4AB144BC1A77E5DF01EC5798C6E853DB74",
    },
}

DESTINATION = "/EFProcedural/Calysto/Internal/Materials/Decals"
BASE_PACKAGE = f"{DESTINATION}/M_CalystoBloodDecal"
INSTANCE_SPECS = {
    f"{DESTINATION}/MI_CalystoBloodDecal_Wall": {
        "tint": (0.24, 0.006, 0.004, 1.0), "roughness": 0.36, "opacity": 0.94,
    },
    f"{DESTINATION}/MI_CalystoBloodDecal_Floor": {
        "tint": (0.19, 0.003, 0.002, 1.0), "roughness": 0.42, "opacity": 0.90,
    },
    f"{DESTINATION}/MI_CalystoBloodDecal_Ceiling": {
        "tint": (0.21, 0.004, 0.003, 1.0), "roughness": 0.38, "opacity": 0.92,
    },
}
ALL_PACKAGES = (BASE_PACKAGE,) + tuple(INSTANCE_SPECS)
RECEIPT = (
    SAVED_ROOT / "Migration" / "CalystoDungeonDirectorV6" / "CreateDecalMaterialsV6.json"
)
PACKAGE_SUFFIXES = (".uasset", ".uexp", ".ubulk", ".uptnl")


class AuthoringBlocked(RuntimeError):
    pass


def fail(message: str) -> None:
    raise RuntimeError(message)


def block(message: str) -> None:
    raise AuthoringBlocked(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def plugin_package_file(package: str) -> Path:
    prefix = "/EFProcedural/"
    if not package.startswith(prefix):
        fail(f"Destination package escapes EFProcedural: {package}")
    return PLUGIN_CONTENT / Path(package[len(prefix) :] + ".uasset")


def snapshot_package_roots() -> dict[str, tuple[int, int, str]]:
    result: dict[str, tuple[int, int, str]] = {}
    for label, root in (("Game", PROJECT_CONTENT), ("EFProcedural", PLUGIN_CONTENT)):
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix.casefold() not in PACKAGE_SUFFIXES:
                continue
            stat = path.stat()
            result[f"{label}/{path.relative_to(root).as_posix()}"] = (
                int(stat.st_size), int(stat.st_mtime_ns), sha256(path)
            )
    return result


def changed_packages(before: dict[str, Any], after: dict[str, Any]) -> tuple[list[str], list[str]]:
    files = sorted(path for path in set(before) | set(after) if before.get(path) != after.get(path))
    packages: set[str] = set()
    for path in files:
        mount, relative = path.split("/", 1)
        packages.add(f"/{mount}/" + relative.rsplit(".", 1)[0])
    return files, sorted(packages)


def dirty_packages() -> set[str]:
    values = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    values += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return {str(value.get_path_name()) for value in values}


def write_receipt_append_only(path: Path, payload: dict[str, Any]) -> Path:
    """Preserve every creator result before updating the canonical latest receipt."""
    stamp = "".join(
        character if character.isalnum() else "_"
        for character in str(payload.get("generated_utc", "unknown"))
    ).strip("_")
    mode = "".join(
        character if character.isalnum() else "_"
        for character in str(payload.get("mode", "UNKNOWN"))
    ).strip("_")
    history = path.parent / "ReceiptHistory" / path.stem / f"{stamp}_{mode}.json"
    history.parent.mkdir(parents=True, exist_ok=True)
    payload["append_only_receipt"] = str(history)
    serialized = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    with history.open("x", encoding="utf-8") as stream:
        stream.write(serialized)
    path.write_text(serialized, encoding="utf-8")
    return history


def validate_context() -> None:
    if not str(unreal.SystemLibrary.get_engine_version()).startswith("5.8."):
        fail("Calysto V6 decal authoring requires UE 5.8")
    if PROJECT_FILE.name.casefold() != "noshellforwinter.uproject":
        fail(f"Wrong project: {PROJECT_FILE}")
    if str(PROJECT_ROOT).casefold() != str(EXPECTED_ROOT).casefold():
        fail(f"Wrong writable target: {PROJECT_ROOT}")
    descriptor = PROJECT_ROOT / "Plugins" / "EFProcedural" / "EFProcedural.uplugin"
    data = json.loads(descriptor.read_text(encoding="utf-8-sig"))
    if data.get("CanContainContent") is not True:
        block(f"EFProcedural CanContainContent must be true: {descriptor}")


def require_authoring_api() -> dict[str, str]:
    required_library = (
        "create_material_expression",
        "connect_material_expressions",
        "connect_material_property",
        "recompile_material",
        "set_material_instance_parent",
        "set_material_instance_scalar_parameter_value",
        "set_material_instance_vector_parameter_value",
        "get_material_expressions",
        "get_material_property_input_node",
        "get_material_instance_scalar_parameter_value",
        "get_material_instance_vector_parameter_value",
    )
    missing = [name for name in required_library if not callable(getattr(unreal.MaterialEditingLibrary, name, None))]
    required_types = (
        "Material", "MaterialFactoryNew", "MaterialInstanceConstant",
        "MaterialInstanceConstantFactoryNew", "MaterialExpressionTextureSampleParameter2D",
        "MaterialExpressionVectorParameter", "MaterialExpressionScalarParameter",
        "MaterialExpressionMultiply", "MaterialDomain", "BlendMode", "MaterialProperty",
    )
    missing += [name for name in required_types if getattr(unreal, name, None) is None]
    if missing:
        block("BLOCKED_MATERIAL_AUTHORING_API_MISSING: " + ", ".join(sorted(missing)))
    enum_requirements = {
        "MaterialDomain.MD_DEFERRED_DECAL": getattr(unreal.MaterialDomain, "MD_DEFERRED_DECAL", None),
        "BlendMode.BLEND_TRANSLUCENT": getattr(unreal.BlendMode, "BLEND_TRANSLUCENT", None),
        "MaterialProperty.MP_BASE_COLOR": getattr(unreal.MaterialProperty, "MP_BASE_COLOR", None),
        "MaterialProperty.MP_NORMAL": getattr(unreal.MaterialProperty, "MP_NORMAL", None),
        "MaterialProperty.MP_ROUGHNESS": getattr(unreal.MaterialProperty, "MP_ROUGHNESS", None),
        "MaterialProperty.MP_OPACITY": getattr(unreal.MaterialProperty, "MP_OPACITY", None),
    }
    missing_enums = [name for name, value in enum_requirements.items() if value is None]
    if missing_enums:
        block("BLOCKED_MATERIAL_ENUM_MISSING: " + ", ".join(missing_enums))
    bridge = getattr(unreal, "EFCalystoMaterialEditorLibrary", None)
    if bridge is None or not callable(
        getattr(bridge, "get_material_expression_input_source_path", None)
    ):
        block(
            "BLOCKED_MATERIAL_GRAPH_INSPECTION_API_MISSING: "
            "cold-build EFCalystoMaterialEditorLibrary before authoring"
        )
    report = {name: "AVAILABLE" for name in required_library}
    report["get_material_expression_input_source_path"] = "AVAILABLE"
    return report


def validate_sources() -> dict[str, Any]:
    dirty = dirty_packages()
    report: dict[str, Any] = {}
    for parameter, spec in SOURCE_TEXTURES.items():
        path = Path(spec["file"])
        if not path.is_file():
            fail(f"Required Realistic Blood texture is missing: {path}")
        actual = sha256(path)
        if actual != spec["sha256"]:
            fail(
                f"Read-only texture drift for {spec['package']}: "
                f"actual={actual} expected={spec['sha256']}"
            )
        if spec["package"] in dirty:
            fail(f"Read-only texture package is dirty: {spec['package']}")
        texture = unreal.load_asset(spec["package"])
        if texture is None or str(texture.get_path_name()) != spec["object"]:
            fail(f"Could not load exact texture object: {spec['object']}")
        report[parameter] = {
            "object": spec["object"], "sha256": actual, "dirty": False,
            "class": str(texture.get_class().get_path_name()),
        }
    return report


def dependency_options() -> Any:
    value = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_hard_package_references", "include_soft_package_references",
        "include_hard_management_references", "include_soft_management_references",
    ):
        value.set_editor_property(name, True)
    return value


def normalized_token(value: Any) -> str:
    return "".join(character for character in str(value).upper() if character.isalnum())


def expression_array(material: Any) -> list[Any]:
    try:
        return list(unreal.MaterialEditingLibrary.get_material_expressions(material))
    except Exception as exc:
        fail(f"Unable to inspect decal material expressions through MaterialEditingLibrary: {exc}")


def expression_parameter_name(expression: Any) -> str:
    try:
        return str(expression.get_editor_property("parameter_name"))
    except Exception:
        return ""


def expression_input_source(expression: Any, input_name: str) -> str:
    input_index = {"a": 0, "b": 1}.get(input_name.casefold())
    if input_index is None:
        fail(f"Unsupported material expression input name: {input_name}")
    return str(
        unreal.EFCalystoMaterialEditorLibrary.get_material_expression_input_source_path(
            expression, input_index
        )
    )


def color_tuple(value: Any) -> tuple[float, float, float, float]:
    return tuple(float(getattr(value, name)) for name in ("r", "g", "b", "a"))


def close_float(actual: float, expected: float, tolerance: float = 1.0e-5) -> bool:
    return abs(float(actual) - float(expected)) <= tolerance


def close_color(actual: Any, expected: tuple[float, float, float, float]) -> bool:
    return all(close_float(a, b) for a, b in zip(color_tuple(actual), expected))


def validate_base_material_semantics(material: Any) -> dict[str, Any]:
    if material.get_editor_property("material_domain") != unreal.MaterialDomain.MD_DEFERRED_DECAL:
        fail("Calysto blood master is not a deferred-decal material")
    if material.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        fail("Calysto blood master does not use translucent blending")

    expressions = expression_array(material)
    by_class: dict[str, list[Any]] = {}
    for expression in expressions:
        by_class.setdefault(str(expression.get_class().get_name()), []).append(expression)
    expected_counts = {
        "MaterialExpressionTextureSampleParameter2D": 2,
        "MaterialExpressionVectorParameter": 1,
        "MaterialExpressionScalarParameter": 2,
        "MaterialExpressionMultiply": 2,
    }
    unexpected = sorted(set(by_class) - set(expected_counts))
    wrong_counts = {
        name: len(by_class.get(name, []))
        for name, expected in expected_counts.items()
        if len(by_class.get(name, [])) != expected
    }
    if unexpected or wrong_counts or len(expressions) != 7:
        fail(
            "Calysto blood master expression topology drifted: "
            f"unexpected={unexpected} wrong_counts={wrong_counts} total={len(expressions)}"
        )

    parameters = {
        expression_parameter_name(expression): expression
        for expression in expressions
        if expression_parameter_name(expression)
    }
    expected_parameters = {
        "BloodMaskTexture", "BloodNormalTexture", "BloodTint",
        "BloodOpacity", "BloodRoughness",
    }
    if set(parameters) != expected_parameters:
        fail(f"Calysto blood master parameter set drifted: {sorted(parameters)}")

    for parameter, source in SOURCE_TEXTURES.items():
        expression = parameters[parameter]
        texture = expression.get_editor_property("texture")
        if texture is None or str(texture.get_path_name()) != source["object"]:
            fail(f"{parameter} points to an unexpected texture")
    if "NORMAL" not in normalized_token(
        parameters["BloodNormalTexture"].get_editor_property("sampler_type")
    ):
        fail("BloodNormalTexture is not configured with a normal-map sampler")
    if not close_color(
        parameters["BloodTint"].get_editor_property("default_value"),
        (0.22, 0.004, 0.003, 1.0),
    ):
        fail("BloodTint master default drifted")
    if not close_float(
        parameters["BloodOpacity"].get_editor_property("default_value"), 0.92
    ):
        fail("BloodOpacity master default drifted")
    if not close_float(
        parameters["BloodRoughness"].get_editor_property("default_value"), 0.38
    ):
        fail("BloodRoughness master default drifted")

    lib = unreal.MaterialEditingLibrary
    property_nodes = {
        "base_color": lib.get_material_property_input_node(
            material, unreal.MaterialProperty.MP_BASE_COLOR
        ),
        "normal": lib.get_material_property_input_node(
            material, unreal.MaterialProperty.MP_NORMAL
        ),
        "roughness": lib.get_material_property_input_node(
            material, unreal.MaterialProperty.MP_ROUGHNESS
        ),
        "opacity": lib.get_material_property_input_node(
            material, unreal.MaterialProperty.MP_OPACITY
        ),
    }
    if any(node is None for node in property_nodes.values()):
        fail("Calysto blood master has an unconnected required material property")
    if str(property_nodes["normal"].get_path_name()) != str(
        parameters["BloodNormalTexture"].get_path_name()
    ):
        fail("Calysto blood master Normal input is wired incorrectly")
    if str(property_nodes["roughness"].get_path_name()) != str(
        parameters["BloodRoughness"].get_path_name()
    ):
        fail("Calysto blood master Roughness input is wired incorrectly")
    for label, parameter_names in (
        ("base_color", {"BloodMaskTexture", "BloodTint"}),
        ("opacity", {"BloodMaskTexture", "BloodOpacity"}),
    ):
        multiply = property_nodes[label]
        if str(multiply.get_class().get_name()) != "MaterialExpressionMultiply":
            fail(f"Calysto blood master {label} is not driven by Multiply")
        actual_sources = {
            expression_input_source(multiply, "a"),
            expression_input_source(multiply, "b"),
        }
        expected_sources = {
            str(parameters[name].get_path_name()) for name in parameter_names
        }
        if actual_sources != expected_sources:
            fail(
                f"Calysto blood master {label} Multiply inputs drifted: "
                f"actual={sorted(actual_sources)} expected={sorted(expected_sources)}"
            )
    return {
        "material_domain": str(material.get_editor_property("material_domain")),
        "blend_mode": str(material.get_editor_property("blend_mode")),
        "expression_count": len(expressions),
        "parameters": sorted(parameters),
        "required_inputs_connected": sorted(property_nodes),
    }


def validate_instance_semantics(instance: Any, spec: dict[str, Any]) -> dict[str, Any]:
    lib = unreal.MaterialEditingLibrary
    tint = lib.get_material_instance_vector_parameter_value(instance, "BloodTint")
    roughness = float(
        lib.get_material_instance_scalar_parameter_value(instance, "BloodRoughness")
    )
    opacity = float(
        lib.get_material_instance_scalar_parameter_value(instance, "BloodOpacity")
    )
    if not close_color(tint, tuple(spec["tint"])):
        fail(f"Material instance BloodTint drifted: {instance.get_path_name()}")
    if not close_float(roughness, float(spec["roughness"])):
        fail(f"Material instance BloodRoughness drifted: {instance.get_path_name()}")
    if not close_float(opacity, float(spec["opacity"])):
        fail(f"Material instance BloodOpacity drifted: {instance.get_path_name()}")
    return {
        "tint": color_tuple(tint),
        "roughness": roughness,
        "opacity": opacity,
    }


def validate_assets(registry: Any) -> dict[str, Any]:
    allowed_vendor = {str(spec["package"]) for spec in SOURCE_TEXTURES.values()}
    report: dict[str, Any] = {}
    for package in ALL_PACKAGES:
        asset = unreal.load_asset(package)
        if asset is None:
            fail(f"Created decal asset is missing: {package}")
        expected_class = (
            "/Script/Engine.Material" if package == BASE_PACKAGE
            else "/Script/Engine.MaterialInstanceConstant"
        )
        actual_class = str(asset.get_class().get_path_name())
        if actual_class != expected_class:
            fail(f"Class mismatch for {package}: actual={actual_class} expected={expected_class}")
        dependencies = sorted(
            {str(value) for value in registry.get_dependencies(package, dependency_options())}
        )
        vendor_dependencies = sorted(
            dep for dep in dependencies if dep.startswith("/Game/RealisticBlood/")
        )
        unexpected_vendor = sorted(set(vendor_dependencies) - allowed_vendor)
        if unexpected_vendor:
            fail(f"Unexpected Realistic Blood dependency for {package}: {unexpected_vendor}")
        if package == BASE_PACKAGE and set(vendor_dependencies) != allowed_vendor:
            fail(
                "Base decal material must depend on exactly T_Splat_04 and T_Splat_N_04: "
                f"actual={vendor_dependencies}"
            )
        if package != BASE_PACKAGE:
            parent = asset.get_editor_property("parent")
            if parent is None or str(parent.get_path_name()).split(".", 1)[0] != BASE_PACKAGE:
                fail(f"Material instance parent mismatch: {package}")
            semantic_validation = validate_instance_semantics(
                asset, INSTANCE_SPECS[package]
            )
        else:
            semantic_validation = validate_base_material_semantics(asset)
        disk = plugin_package_file(package)
        if not disk.is_file():
            fail(f"Plugin material package is missing on disk: {disk}")
        report[package] = {
            "class": actual_class,
            "dependencies": dependencies,
            "realistic_blood_dependencies": vendor_dependencies,
            "package_sha256": sha256(disk),
            "dirty": package in dirty_packages(),
            "semantic_validation": semantic_validation,
        }
        if report[package]["dirty"]:
            fail(f"Decal material remains dirty: {package}")
    return report


def create_base(textures: dict[str, Any]) -> Any:
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = asset_tools.create_asset(
        BASE_PACKAGE.rsplit("/", 1)[-1], DESTINATION,
        unreal.Material, unreal.MaterialFactoryNew(),
    )
    if material is None:
        fail(f"Could not create {BASE_PACKAGE}")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

    lib = unreal.MaterialEditingLibrary
    mask = lib.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -720, -180
    )
    mask.set_editor_property("parameter_name", "BloodMaskTexture")
    mask.set_editor_property("texture", textures["BloodMaskTexture"])
    normal = lib.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -720, 140
    )
    normal.set_editor_property("parameter_name", "BloodNormalTexture")
    normal.set_editor_property("texture", textures["BloodNormalTexture"])
    try:
        normal.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    except Exception:
        # Texture compression metadata still drives normal sampling; recordable
        # graph validation and compilation remain authoritative.
        pass

    tint = lib.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -470, -40
    )
    tint.set_editor_property("parameter_name", "BloodTint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.22, 0.004, 0.003, 1.0))
    color_multiply = lib.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -210, -120
    )
    opacity = lib.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -470, 330
    )
    opacity.set_editor_property("parameter_name", "BloodOpacity")
    opacity.set_editor_property("default_value", 0.92)
    opacity_multiply = lib.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -210, 300
    )
    roughness = lib.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -210, 450
    )
    roughness.set_editor_property("parameter_name", "BloodRoughness")
    roughness.set_editor_property("default_value", 0.38)

    lib.connect_material_expressions(mask, "RGB", color_multiply, "A")
    lib.connect_material_expressions(tint, "", color_multiply, "B")
    lib.connect_material_expressions(mask, "A", opacity_multiply, "A")
    lib.connect_material_expressions(opacity, "", opacity_multiply, "B")
    lib.connect_material_property(color_multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.connect_material_property(opacity_multiply, "", unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(material)
    return material


def create_instances(parent: Any) -> list[Any]:
    result: list[Any] = []
    lib = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for package, spec in INSTANCE_SPECS.items():
        instance = tools.create_asset(
            package.rsplit("/", 1)[-1], DESTINATION,
            unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew(),
        )
        if instance is None:
            fail(f"Could not create {package}")
        lib.set_material_instance_parent(instance, parent)
        tint = spec["tint"]
        lib.set_material_instance_vector_parameter_value(
            instance, "BloodTint", unreal.LinearColor(*tint)
        )
        lib.set_material_instance_scalar_parameter_value(
            instance, "BloodRoughness", float(spec["roughness"])
        )
        lib.set_material_instance_scalar_parameter_value(
            instance, "BloodOpacity", float(spec["opacity"])
        )
        result.append(instance)
    return result


validate_context()
before = snapshot_package_roots()
created: list[str] = []
saved: list[str] = []
result: dict[str, Any] = {
    "receipt_schema_version": 1,
    "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "project": str(PROJECT_FILE),
    "engine_version": str(unreal.SystemLibrary.get_engine_version()),
    "status": "FAIL",
    "mode": "PREFLIGHT",
    "destination": DESTINATION,
    "source_contract": "EXACTLY_T_SPLAT_04_AND_T_SPLAT_N_04",
    "created": [], "saved": [], "asset_mutations": [],
}

try:
    result["authoring_api"] = require_authoring_api()
    result["sources"] = validate_sources()
    # A plugin mount can be active before the Asset Registry has announced
    # packages created by a previous unattended editor process. Disk identity
    # is authoritative for create-once mode. Exact load, class, dependency,
    # graph and hash validation below remains mandatory before acceptance.
    presence = [plugin_package_file(package).is_file() for package in ALL_PACKAGES]
    if any(presence) and not all(presence):
        fail(f"Partial Calysto decal material set exists: {dict(zip(ALL_PACKAGES, presence))}")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    registry.scan_paths_synchronous([DESTINATION], True)
    registry.wait_for_completion()

    if all(presence):
        result["mode"] = "VALIDATE_EXISTING_READ_ONLY"
    else:
        result["mode"] = "CREATE_ONCE"
        unreal.EditorAssetLibrary.make_directory(DESTINATION)
        textures = {
            name: unreal.load_asset(str(spec["package"]))
            for name, spec in SOURCE_TEXTURES.items()
        }
        base = create_base(textures)
        created.append(BASE_PACKAGE)
        instances = create_instances(base)
        created.extend(INSTANCE_SPECS)
        for asset in [base] + instances:
            package = str(asset.get_outermost().get_path_name())
            if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
                fail(f"Could not save decal material asset: {package}")
            saved.append(package)
        registry.scan_paths_synchronous([DESTINATION], True)
        registry.wait_for_completion()

    result["assets"] = validate_assets(registry)
    after = snapshot_package_roots()
    files, packages = changed_packages(before, after)
    expected = sorted(ALL_PACKAGES) if created else []
    if packages != expected:
        fail(f"Unexpected package delta: actual={packages} expected={expected}")
    if sorted(saved) != expected:
        fail(f"Unexpected save set: actual={saved} expected={expected}")
    source_after = validate_sources()
    if source_after != result["sources"]:
        fail("A read-only Realistic Blood texture changed during authoring")
    result["changed_files"] = files
    result["asset_mutations"] = packages
    result["created"] = created
    result["saved"] = saved
    result["status"] = "PASS"
except AuthoringBlocked as exc:
    result["status"] = "BLOCKED_MATERIAL_AUTHORING_UNAVAILABLE"
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
except Exception as exc:
    result["error"] = str(exc)
    result["traceback"] = traceback.format_exc()
finally:
    if result["status"] != "PASS" and created:
        cleanup: dict[str, bool] = {}
        for package in reversed(created):
            try:
                cleanup[package] = bool(unreal.EditorAssetLibrary.delete_asset(package))
            except Exception:
                cleanup[package] = False
        result["failed_creation_cleanup"] = cleanup
    final = snapshot_package_roots()
    files, packages = changed_packages(before, final)
    result["changed_files_final"] = files
    result["asset_mutations_final"] = packages
    RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    write_receipt_append_only(RECEIPT, result)
    unreal.log("CALYSTO_V6_DECAL_MATERIAL_RESULT=" + json.dumps(result, sort_keys=True))

if result["status"] != "PASS":
    raise RuntimeError(f"{result['status']}: {result.get('error', 'unknown failure')}")
