import json
import os

import unreal


BLUEPRINT_PATH = "/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar"
PARENT_CLASS_PATH = "/Game/FullSample/Player.Player_C"
INTERFACE_CLASS_PATH = (
    "/Game/TattooShop/Blueprints/Interface/"
    "BPI_CharacterCustomization.BPI_CharacterCustomization_C"
)


def path_name(value):
    return value.get_path_name() if value else ""


report = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "blueprint_path": BLUEPRINT_PATH,
    "target_parent_class": PARENT_CLASS_PATH,
    "interface_class": INTERFACE_CLASS_PATH,
    "status": "FAIL",
}

blueprint = unreal.load_asset(BLUEPRINT_PATH)
parent_class = unreal.load_object(None, PARENT_CLASS_PATH)
interface_class = unreal.load_object(None, INTERFACE_CLASS_PATH)
report["blueprint_loaded"] = bool(blueprint)
report["parent_loaded"] = bool(parent_class)
report["interface_loaded"] = bool(interface_class)

if not blueprint or not parent_class or not interface_class:
    raise RuntimeError(
        "Required Blueprint contract failed to load: "
        f"bp={blueprint} parent={parent_class} interface={interface_class}"
    )

generated_before = blueprint.generated_class()
report["generated_class_before"] = path_name(generated_before)

repair_ok = unreal.ProjectBlueprintMigrationLibrary.repair_tattoo_shop_character_blueprint(
    blueprint,
    parent_class,
    interface_class,
)
report["repair_helper_returned"] = bool(repair_ok)
if not repair_ok:
    raise RuntimeError("Project Blueprint repair helper reported failure")

generated_after = blueprint.generated_class()
report["generated_class_after_compile"] = path_name(generated_after)
if not generated_after:
    raise RuntimeError("BP_TSChar still has no generated class after reparent/compile")

default_object = unreal.get_default_object(generated_after)
report["default_object"] = path_name(default_object)
report["generated_class_name"] = generated_after.get_name()

if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
    raise RuntimeError("Failed to save BP_TSChar after successful compile")

reloaded = unreal.load_asset(BLUEPRINT_PATH)
reloaded_class = reloaded.generated_class() if reloaded else None
report["generated_class_after_save"] = path_name(reloaded_class)
report["status"] = "PASS" if reloaded_class else "FAIL"

output_path = os.environ.get(
    "CODEX_TATTOOSHOP_REPAIR_REPORT",
    os.path.join(unreal.Paths.project_saved_dir(), "TattooShopQA", "BP_TSCharRepair58.json"),
)
os.makedirs(os.path.dirname(output_path), exist_ok=True)
with open(output_path, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2, ensure_ascii=False)

unreal.log(f"TATTOOSHOP_BP_TSCHAR_REPAIR={output_path} STATUS={report['status']}")
if report["status"] != "PASS":
    raise RuntimeError("BP_TSChar repair validation failed")
