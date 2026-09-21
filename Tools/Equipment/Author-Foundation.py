import json
import pathlib
import unreal

root = pathlib.Path(unreal.Paths.project_dir())
out = root / 'Saved/EquipmentQA'
out.mkdir(parents=True, exist_ok=True)
result = unreal.EFEquipmentAuthoringLibrary.build_foundation()
(out / 'author_foundation.json').write_text(json.dumps({'result': result}, indent=2))
(out / 'blueprint_consumers.txt').write_text(unreal.EFEquipmentAuthoringLibrary.audit_consumers())
if not result.startswith('PASS:'):
    raise RuntimeError(result)
print(result)
