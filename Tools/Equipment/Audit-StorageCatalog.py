"""Read the installed native item catalog and resolve stale project item paths."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_dir())/'Saved/EquipmentQA/StorageRework'
table=unreal.load_asset('/Game/FullSample/GameplayTagTables/ItemsDB')
data=unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
(out/'items_catalog.json').write_text(data)
registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
candidates=registry.get_assets_by_path('/Game/FullSample/Blueprints/Items',True)
(out/'item_candidates.json').write_text(json.dumps([{'name':str(a.asset_name),'path':str(a.package_name)} for a in candidates],indent=2))
print('Storage catalog audit exported.')
