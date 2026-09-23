"""Repair five obsolete item references in the existing ACF sample catalog."""
import json, shutil
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir())
out=root/'Saved/EquipmentQA/StorageRework'
package='/Game/FullSample/GameplayTagTables/ItemsDB'
table=unreal.load_asset(package)
rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
remap={
 '/Game/FullSample/Blueprints/ItemSystem/Materials/ACFWoodBP.ACFWoodBP_C':'/Game/FullSample/Blueprints/Items/Materials/ACFWoodBP.ACFWoodBP_C',
 '/Game/FullSample/Blueprints/ItemSystem/Materials/ACFMetalBP.ACFMetalBP_C':'/Game/FullSample/Blueprints/Items/Materials/ACFMetalBP.ACFMetalBP_C',
 '/Game/FullSample/Blueprints/ItemSystem/Weapons/ACFSwordBP.ACFSwordBP_C':'/Game/FullSample/Blueprints/Items/Weapons/ACFSwordBP.ACFSwordBP_C',
 '/Game/FullSample/Blueprints/ItemSystem/Weapons/ACFLeftSwordBP.ACFLeftSwordBP_C':'/Game/FullSample/Blueprints/Items/Weapons/ACFLeftSwordBP.ACFLeftSwordBP_C',
 '/Game/FullSample/Blueprints/Items/Weapons/ACFBowActorBP.ACFBowActorBP_C':'/Game/FullSample/Blueprints/Items/Weapons/ACFBowBP.ACFBowBP_C',
}
changes=[]
for row in rows:
    old=row['ItemClass'];new=remap.get(old,old)
    cls=unreal.load_class(None,new)
    assert cls and isinstance(unreal.get_default_object(cls),unreal.ACFItem),new
    if new!=old:
        row['ItemClass']=new;changes.append({'row':row['Name'],'before':old,'after':new})
if changes:
    backup=out/'ItemsDB.before_storage_reference_repair.uasset'
    if not backup.exists(): shutil.copy2(root/'Content/FullSample/GameplayTagTables/ItemsDB.uasset',backup)
    assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows))
    assert unreal.EditorAssetLibrary.save_loaded_asset(table,False)
(out/'catalog_repair.json').write_text(json.dumps({'status':'PASS','changes':changes,'rows_validated':len(rows)},indent=2))
print('PASS: existing ACF item catalog references validated; no category or rarity changes.')
