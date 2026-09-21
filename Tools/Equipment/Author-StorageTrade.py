"""Route five existing storage/trade consumers without replacing their widgets."""
import json
import pathlib
import unreal

root = pathlib.Path(unreal.Paths.project_dir()).resolve()
assert (root / 'NoShellForWinter.uproject').is_file()
result = unreal.EFEquipmentAuthoringLibrary.build_storage_trade_integration()
(root / 'Saved/EquipmentQA/author_storage_trade.json').write_text(json.dumps({'result': result}, indent=2), encoding='utf-8')
unreal.log(result)
if not result.startswith('PASS:'):
    raise RuntimeError(result)
