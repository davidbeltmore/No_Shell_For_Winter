import json
import os

import unreal


def object_path(value):
    if value is None:
        return ""
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return str(value)


def get_property(obj, name):
    try:
        value = obj.get_editor_property(name)
        if isinstance(value, (list, tuple)):
            return [object_path(item) for item in value]
        return object_path(value)
    except Exception as exc:
        return {"error": str(exc)}


classes = {
    "bp_tschar": "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar.BP_TSChar_C",
    "customization": "/Game/TattooShop/Blueprints/Widget/WBP_TattooCustomization.WBP_TattooCustomization_C",
    "previewer": "/Game/TattooShop/Blueprints/Widget/WBP_AssetPreviewer.WBP_AssetPreviewer_C",
}

report = {"engine_version": unreal.SystemLibrary.get_engine_version(), "classes": {}}
for key, class_path in classes.items():
    cls = unreal.load_object(None, class_path)
    entry = {"class_path": class_path, "loaded": bool(cls)}
    if cls:
        cdo = unreal.get_default_object(cls)
        entry["cdo"] = object_path(cdo)
        property_names = (
            "master_tattoo_material",
            "tat_base_comp",
            "new_dynamic_material_inst",
            "active_tat_base_index",
            "tattoo_shop_parent_wgt",
            "actor_ref",
            "selected_mid",
            "selected_asset_texture",
            "target_mid",
            "pre_edit_mid",
        )
        entry["properties"] = {name: get_property(cdo, name) for name in property_names}
        if hasattr(cdo, "get_components_by_class"):
            entry["components"] = [
                {
                    "name": component.get_name(),
                    "class": object_path(component.get_class()),
                    "path": object_path(component),
                }
                for component in cdo.get_components_by_class(unreal.ActorComponent)
            ]
    report["classes"][key] = entry

output_path = os.environ.get(
    "CODEX_TATTOOSHOP_BP_RUNTIME_REPORT",
    os.path.join(unreal.Paths.project_saved_dir(), "TattooShopQA", "BP_TSCharRuntime58.json"),
)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, ensure_ascii=False)

unreal.log(f"TATTOOSHOP_BP_RUNTIME_REPORT={output_path}")
