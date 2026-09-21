"""Read-only native Blueprint consumer audit. No package compilation or saving."""
import json
import pathlib
import subprocess
import unreal

root = pathlib.Path(unreal.Paths.project_dir()).resolve()
assert (root / 'NoShellForWinter.uproject').is_file()
names = ('SaveGameWorld|SaveLocalPlayer|LoadGameWorld|TravelLocalPlayer|LoadTraveledPlayer|'
         'LoadLevelFromSaveGame|LoadCurrentLevel|LoadLocalPlayer|MoveItemsFromInventory|'
         'SellItemsToVendor|EquipItemFromInventory|UseInventoryItem|DropItem|'
         'GetCurrentInventoryTotalWeight|GetMaxInventoryWeight')
scan = subprocess.run(['rg', '-a', '-l', '--glob', '*.uasset', names,
                       'Content/FullSample', 'Content/_Game'], cwd=root, capture_output=True, text=True)
if scan.returncode not in (0, 1):
    raise RuntimeError(scan.stderr)
paths = sorted('/Game/' + str(pathlib.Path(line).relative_to('Content').with_suffix('')).replace('\\', '/')
               for line in scan.stdout.splitlines())
output = root / 'Saved/EquipmentQA/IntegrationAudit'
output.mkdir(exist_ok=True)
(output / 'candidates.json').write_text(json.dumps(paths, indent=2), encoding='utf-8')
report = unreal.EFEquipmentAuthoringLibrary.audit_blueprint_calls(paths)
(output / 'blueprint_calls.txt').write_text(report, encoding='utf-8')
(output / 'receipt.json').write_text(json.dumps({'status': 'PASS', 'scope': 'Read-only project Blueprint call audit',
                                                'candidates': len(paths)}, indent=2), encoding='utf-8')
unreal.log('EFEquipment integration audit completed without package writes.')
