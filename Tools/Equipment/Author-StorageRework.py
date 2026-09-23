"""Author only project-owned storage widgets; run with the protected editor launcher."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/StorageRework'
out.mkdir(parents=True, exist_ok=True)
result = unreal.EFEquipmentAuthoringLibrary.build_storage_rework()
(out / 'author_ui.json').write_text(json.dumps({'result': result}, indent=2))
assert result.startswith('PASS:'), result
for name in ['ACF_Deposit_BP', 'ACF_Chest_BP']:
    asset = unreal.load_asset('/AscentCombatFramework/Integrations/Actors/' + name)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(out / (name + '.copy'))
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    unreal.Exporter.run_asset_export_task(task)
print(result)
