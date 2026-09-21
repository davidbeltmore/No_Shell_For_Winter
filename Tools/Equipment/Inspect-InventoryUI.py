import pathlib
import unreal

out = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/inventory_ui.txt'
out.write_text(unreal.EFEquipmentAuthoringLibrary.inspect_inventory_ui(), encoding='utf-8')
print('Inventory UI inspection saved to ' + str(out))
