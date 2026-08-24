"""Read-only Python API probe for the migrated food assets."""

import unreal

mesh = unreal.EditorAssetLibrary.load_asset("/Game/Food_Props_Kit/Meshes/SM_Apple1")
registry = unreal.EditorAssetLibrary.load_asset(
    "/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry"
)
pickup = unreal.EditorAssetLibrary.load_asset(
    "/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Apple01"
)
if mesh is None or registry is None or pickup is None:
    raise RuntimeError("Probe assets failed to load")
generated = unreal.EditorAssetLibrary.load_blueprint_class(
    "/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Apple01"
)
cdo = unreal.get_default_object(generated)
components = list(cdo.get_components_by_class(unreal.ActorComponent))
storage = next(component for component in components if component.get_class().get_name() == "ACFStorageComponent")
try:
    storage_items = storage.get_editor_property("items")
except Exception as exc:
    storage_items = "ERROR:" + repr(exc)
entries = registry.get_editor_property("entries")
first_entry = entries[0]
unreal.log("CODEX_FOODKIT58_PROBE_MESH_METHODS=" + repr([name for name in dir(mesh) if "bound" in name.lower() or "material" in name.lower()]))
unreal.log("CODEX_FOODKIT58_PROBE_BOUNDS=" + repr(mesh.get_bounds()))
unreal.log("CODEX_FOODKIT58_PROBE_BOX=" + repr(mesh.get_bounding_box()))
unreal.log("CODEX_FOODKIT58_PROBE_MATERIALS=" + repr(mesh.get_editor_property("static_materials")))
unreal.log("CODEX_FOODKIT58_PROBE_COMPONENTS=" + repr([(component.get_class().get_name(), component.get_name(), [name for name in dir(component) if name in ("items", "static_mesh", "relative_scale3d")]) for component in components]))
unreal.log("CODEX_FOODKIT58_PROBE_STORAGE_ITEMS=" + repr(list(storage_items) if not isinstance(storage_items, str) else storage_items))
if not isinstance(storage_items, str) and storage_items:
    unreal.log("CODEX_FOODKIT58_PROBE_STORAGE_ITEM_METHODS=" + repr([name for name in dir(storage_items[0]) if "class" in name.lower() or "count" in name.lower() or "item" in name.lower()]))
unreal.log("CODEX_FOODKIT58_PROBE_ENTRY=" + repr(first_entry))
unreal.log("CODEX_FOODKIT58_PROBE_PROFILE=" + repr(first_entry.get_editor_property("profile")))
unreal.log("CODEX_FOODKIT58_PROBE_REGISTRY_METHODS=" + repr([name for name in dir(registry) if "entr" in name.lower()]))
unreal.log("CODEX_FOODKIT58_PROBE_PASS")
