"""Scoped idempotent chest authoring. Never rewrites existing profiles or ACF assets."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/ProceduralChests'
report = {'status': 'PENDING', 'profiles': [], 'created': []}
def write(): (out/'author.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
def tag(name):
    value = unreal.GameplayTag(); value.import_text('(TagName="'+name+'")'); return value
def categories(name):
    value = unreal.GameplayTagContainer(); value.import_text('(GameplayTags=((TagName="'+name+'")))'); return value
write()
try:
    root = '/Game/_Game/Data/Equipment/Chests'
    catalog_path = root+'/DT_ChestCatalog'
    catalog = unreal.load_asset(catalog_path) if unreal.EditorAssetLibrary.does_asset_exist(catalog_path) else None
    if not catalog:
        catalog = unreal.EditorAssetLibrary.duplicate_asset('/Game/FullSample/GameplayTagTables/ItemsDB', catalog_path)
        assert catalog
        rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(catalog))
        rows.append({'Name':'Arrow', 'Category':{'TagName':'Item.Ammunition'}, 'Rarity':{'TagName':'Rarity.Common'},
                     'ItemClass':'/Game/FullSample/Blueprints/Items/Ammo/ACF_Arrow_BP.ACF_Arrow_BP_C'})
        assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(catalog, json.dumps(rows))
        assert unreal.EditorAssetLibrary.save_loaded_asset(catalog, only_if_is_dirty=False)
        report['created'].append(catalog_path)
    specs = [('Wealth','Item.Material',['NewRow_0'],1,3,1,1,[('Rarity.Common',100)]),
             ('Armament','Item.Weapon',[],1,2,1,1,[('Rarity.Common',70),('Rarity.Rare',30)]),
             ('Ammunition','Item.Ammunition',[],1,3,5,15,[('Rarity.Common',100)])]
    for kind,category,rows,minimum,maximum,units_min,units_max,chances in specs:
        path=root+'/DA_Chest_'+kind
        profile=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if not profile:
            factory=unreal.DataAssetFactory(); factory.set_editor_property('data_asset_class',unreal.ProjectChestLootProfile)
            profile=unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.split('/')[-1],root,unreal.ProjectChestLootProfile,factory)
            settings=unreal.ProjectChestContentSettings()
            for key,value in {'content_type':kind,'catalog':catalog,'categories':categories(category),'allowed_rows':rows,
                              'use_selection_range':True,'min_selections':minimum,'max_selections':maximum,
                              'min_units_per_stack':units_min,'max_units_per_stack':units_max}.items(): settings.set_editor_property(key,value)
            tiers=[]
            for rarity,percentage in chances:
                tier=unreal.ProjectChestRarityChance();tier.set_editor_property('rarity',tag(rarity));tier.set_editor_property('percentage',percentage);tiers.append(tier)
            settings.set_editor_property('item_rarities',tiers);profile.set_editor_property('content',settings)
            assert unreal.EditorAssetLibrary.save_loaded_asset(profile,only_if_is_dirty=False)
            report['created'].append(path)
        report['profiles'].append({'path':path,'content':str(profile.get_editor_property('content'))})
    result=unreal.ProjectLockpickingEditorLibrary.add_procedural_chest_authoring()
    report['component_authoring']=result; assert result.startswith('PASS:'),result
    report['blueprints']=[]
    for name in ['BP_EquipmentDeposit','BP_EquipmentLockedChest']:
        bp=unreal.load_asset('/Game/_Game/Items/Chests/'+name)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        status=str(bp.get_editor_property('status')); assert 'UP_TO_DATE' in status and 'WARNINGS' not in status,status
        report['blueprints'].append({'path':bp.get_path_name(),'status':status})
    report['status']='PASS'; write()
except Exception as error:
    report['status']='FAIL';report['error']=str(error);write();raise
