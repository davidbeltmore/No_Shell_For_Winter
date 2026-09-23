"""Create the two native deposit variants, then audit exact legacy referencers."""
import json
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_dir())/'Saved/EquipmentQA/StorageRework'
result=unreal.ProjectLockpickingEditorLibrary.build_chest_family()
(out/'author_chests.json').write_text(json.dumps({'result':result},indent=2))
assert result.startswith('PASS:'),result
registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
paths=['/Game/_Game/Lockpicking/'+n for n in ['Locked','LockedChest','LockPickChest']]
paths+=['/Game/_Game/Items/Chests/'+n for n in ['BP_CalystoLockedChest','BP_CalystoLockPickChest']]
refs={p:[str(r) for r in registry.get_referencers(p,unreal.AssetRegistryDependencyOptions(True,True,True,True,True))] for p in paths}
(out/'legacy_references.json').write_text(json.dumps(refs,indent=2))
for path in ['/Game/_Game/Items/Chests/BP_EquipmentDeposit','/Game/_Game/Items/Chests/BP_EquipmentLockedChest']:
    bp=unreal.load_asset(path)
    task=unreal.AssetExportTask();task.object=bp;task.filename=str(out/(path.split('/')[-1]+'.copy'))
    task.automated=True;task.prompt=False;task.replace_identical=True
    unreal.Exporter.run_asset_export_task(task)
print(result)
