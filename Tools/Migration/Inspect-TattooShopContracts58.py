import json
import os

import unreal


BP_PATH = "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar"
WIDGET_PATHS = [
    "/Game/TattooShop/Blueprints/Widget/WBP_TattooShop",
    "/Game/TattooShop/Blueprints/Widget/WBP_TattooCustomization",
    "/Game/TattooShop/Blueprints/Widget/WBP_TattooCard",
]
MAP_PATHS = [
    "/Game/FullSample/Test",
    "/Game/_Game/Hub/HUB",
]
PLAYER_CLASS_PATH = "/Game/FullSample/Player.Player_C"


def object_path(value):
    if not value:
        return ""
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return str(value)


def safe_property(obj, property_name):
    try:
        value = obj.get_editor_property(property_name)
    except Exception as exc:
        return {"error": str(exc)}

    if isinstance(value, (list, tuple)):
        return [object_path(item) if hasattr(item, "get_path_name") else str(item) for item in value]
    if hasattr(value, "get_path_name"):
        return object_path(value)
    return value


def describe_blueprint(asset_path):
    asset = unreal.load_asset(asset_path)
    result = {
        "asset_path": asset_path,
        "loaded": bool(asset),
        "asset_class": asset.get_class().get_name() if asset else "",
    }
    if not asset:
        return result

    parent_class = safe_property(asset, "parent_class")
    result["parent_class"] = parent_class if isinstance(parent_class, dict) else object_path(parent_class)

    generated_class = None
    try:
        generated_class = asset.generated_class()
    except Exception as exc:
        result["generated_class_error"] = str(exc)
    result["generated_class"] = object_path(generated_class)

    if generated_class:
        result["super_class"] = safe_property(generated_class, "super_struct")
        try:
            result["implemented_interfaces"] = [
                object_path(interface_class)
                for interface_class in generated_class.get_interfaces()
            ]
        except Exception as exc:
            result["implemented_interfaces"] = safe_property(generated_class, "interfaces")
            result["implemented_interfaces_error"] = str(exc)

        default_object = unreal.get_default_object(generated_class)
        result["default_object"] = object_path(default_object)
        if default_object:
            result["known_properties"] = {
                name: safe_property(default_object, name)
                for name in (
                    "tattoo_shop_parent_wgt",
                    "tat_base_comp",
                    "new_dynamic_material_inst",
                    "mesh",
                )
            }
            result["python_attributes"] = sorted(
                name
                for name in dir(default_object)
                if any(token in name.lower() for token in ("tat", "material", "custom"))
            )
    return result


def describe_map(map_path):
    result = {"map_path": map_path, "loaded": False, "door_actors": []}
    try:
        world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
        result["loaded"] = bool(world)
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        for actor in actor_subsystem.get_all_level_actors():
            class_path = object_path(actor.get_class())
            actor_path = object_path(actor)
            label = actor.get_actor_label()
            combined = f"{class_path} {actor_path} {label}".lower()
            if "door" in combined:
                result["door_actors"].append(
                    {
                        "label": label,
                        "actor_path": actor_path,
                        "class_path": class_path,
                        "location": str(actor.get_actor_location()),
                    }
                )
    except Exception as exc:
        result["error"] = str(exc)
    return result


def describe_actor_class(class_path):
    result = {"class_path": class_path, "loaded": False, "components": []}
    actor_class = unreal.load_object(None, class_path)
    result["loaded"] = bool(actor_class)
    if not actor_class:
        return result
    default_object = unreal.get_default_object(actor_class)
    result["default_object"] = object_path(default_object)
    try:
        components = default_object.get_components_by_class(unreal.ActorComponent)
        result["components"] = [
            {
                "name": component.get_name(),
                "path": object_path(component),
                "class": object_path(component.get_class()),
            }
            for component in components
        ]
    except Exception as exc:
        result["components_error"] = str(exc)
    return result


report = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "bp_tschar": describe_blueprint(BP_PATH),
    "widgets": [describe_blueprint(path) for path in WIDGET_PATHS],
    "player_class": describe_actor_class(PLAYER_CLASS_PATH),
    "maps": [describe_map(path) for path in MAP_PATHS],
}

output_path = os.environ.get(
    "CODEX_TATTOOSHOP_CONTRACT_REPORT",
    os.path.join(unreal.Paths.project_saved_dir(), "TattooShopQA", "TattooShopContracts.json"),
)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, ensure_ascii=False, default=str)

unreal.log(f"TATTOOSHOP_CONTRACT_REPORT={output_path}")
