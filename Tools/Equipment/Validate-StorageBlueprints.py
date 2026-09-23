"""Compile the project-owned storage and lockpick Blueprint surface."""
import json
from pathlib import Path
import unreal

paths=[
 '/Game/_Game/Items/Chests/BP_EquipmentDeposit',
 '/Game/_Game/Items/Chests/BP_EquipmentLockedChest',
 '/Game/_Game/Items/Equipment/BP_StarterBag',
 '/Game/_Game/Widgets/Equipment/Global/WBP_EquipmentBag',
]
registry=unreal.AssetRegistryHelpers.get_asset_registry()
for asset in registry.get_assets_by_path('/Game/_Game/Widgets/LockPick',True):
    if str(asset.asset_class_path.asset_name)=='WidgetBlueprint': paths.append(str(asset.package_name))
result={'status':'PENDING','assets':[]}
out=Path(unreal.Paths.project_dir())/'Saved/EquipmentQA/StorageRework/blueprints-final.json'
try:
    for path in sorted(set(paths)):
        bp=unreal.load_asset(path)
        assert isinstance(bp,unreal.Blueprint),path
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        status=str(bp.get_editor_property('status'))
        result['assets'].append({'path':path,'status':status})
        assert 'UP_TO_DATE' in status,path+': '+status
    result['status']='PASS'
finally: out.write_text(json.dumps(result,indent=2))
print('Storage Blueprint compile: '+result['status'])
