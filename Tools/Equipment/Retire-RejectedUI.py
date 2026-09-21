"""One-time retirement of this task's rejected, uncommitted gray-panel assets."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import unreal

ROOT = pathlib.Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/EquipmentQA/RejectedUIRetirement01'
assert ROOT.name == 'NoShellForWinter', 'Wrong target project'
OUT.mkdir(parents=True, exist_ok=False)
BASE = '/Game/_Game/Widgets/Equipment/'
PATHS = [BASE + 'Global/' + name for name in
         ('WBP_EquipmentPanel', 'WBP_EquipmentTransferRow', 'WBP_EquipmentSlot')]
PATHS += [BASE + 'Slots/WBP_ItemSlot_' + suffix for suffix in
          ('Armor_Cape', 'Armor_Chest', 'Armor_Feet', 'Armor_Forearms', 'Armor_Gloves',
           'Armor_Helmet', 'Armor_Legs', 'Armor_UpperArms', 'Necklace', 'Ring', 'Ring2',
           'Satchel', 'Underwear_Legs', 'Underwear_Torso')]
REPORT = {'status': 'PENDING', 'assets': [], 'head': subprocess.check_output(
    ['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()}

def write():
    (OUT / 'receipt.json').write_text(json.dumps(REPORT, indent=2), encoding='utf-8')

write()
try:
    for asset in PATHS:
        if not unreal.EditorAssetLibrary.does_asset_exist(asset):
            continue
        relative = 'Content/' + asset.removeprefix('/Game/') + '.uasset'
        tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', '--', relative],
                                 cwd=ROOT, capture_output=True).returncode == 0
        if tracked:
            raise RuntimeError('Refusing to retire a committed asset: ' + asset)
        references = unreal.EditorAssetLibrary.find_package_referencers_for_asset(asset, True)
        outside = [str(ref) for ref in references if str(ref).split('.')[0] not in PATHS]
        if outside:
            raise RuntimeError('Asset still has retained referencers: ' + asset + ': ' + repr(outside))
        source = ROOT / relative
        backup = OUT / 'backup' / relative
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, backup)
        REPORT['assets'].append({'asset': asset, 'references': list(references),
                                 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
    write()
    # Delete parents before their template children; every inbound reference was
    # checked against the complete retired set before the first mutation.
    for asset in PATHS:
        if unreal.EditorAssetLibrary.does_asset_exist(asset):
            if not unreal.EditorAssetLibrary.delete_asset(asset):
                raise RuntimeError('Editor could not retire ' + asset)
    REPORT['status'] = 'PASS'
except Exception as error:
    REPORT['status'] = 'FAIL'
    REPORT['error'] = str(error)
    raise
finally:
    write()
