"""Read-only current chest/catalog audit. Run through the protected Editor wrapper."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/ProceduralChests'
table = unreal.load_asset('/Game/FullSample/GameplayTagTables/ItemsDB')
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
report = {'catalog': table.get_path_name(), 'rows': rows, 'chests': []}
report['tag_tables'] = {}
for path in unreal.EditorAssetLibrary.list_assets('/Game/FullSample/GameplayTagTables', recursive=True):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.DataTable):
        report['tag_tables'][path] = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset)
report['ammo'] = {}
for name in ['ACF_Arrow_BP', 'ACF_Bullet_BP']:
    cls = unreal.load_class(None, '/Game/FullSample/Blueprints/Items/Ammo/' + name + '.' + name + '_C')
    report['ammo'][name] = str(unreal.get_default_object(cls).get_editor_property('item_info'))
for name in ['BP_EquipmentDeposit', 'BP_EquipmentLockedChest']:
    bp = unreal.load_asset('/Game/_Game/Items/Chests/' + name)
    cls = unreal.load_class(None, bp.get_path_name() + '_C')
    cdo = unreal.get_default_object(cls)
    storage = cdo.get_component_by_class(unreal.ProjectChestInventoryComponent)
    report['chests'].append({'path': bp.get_path_name(), 'storage': storage.get_path_name(),
                             'procedural': storage.get_editor_property('procedural_mode'),
                             'profile': str(storage.get_editor_property('profile'))})
(out / 'catalog.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('Chest catalog read-only audit complete')
