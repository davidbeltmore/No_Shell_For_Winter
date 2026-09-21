"""Bounded project save-consumer integration; native vendor assets remain read-only."""
import json
import pathlib
import unreal

root = pathlib.Path(unreal.Paths.project_dir()).resolve()
assert (root / 'NoShellForWinter.uproject').is_file()
result = unreal.EFEquipmentAuthoringLibrary.build_save_integration()
(root / 'Saved/EquipmentQA/author_save_integration.json').write_text(json.dumps({'result': result}, indent=2), encoding='utf-8')
unreal.log(result)
if not result.startswith('PASS:'):
    raise RuntimeError(result)
