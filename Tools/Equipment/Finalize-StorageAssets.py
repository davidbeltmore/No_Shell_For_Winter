"""Retire exact superseded chest/UI assets through Unreal after reference checks."""
import json
import shutil
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
assert root == Path('D:/Projects UE5/NoShellForWinter').resolve()
out = root / 'Saved/EquipmentQA/StorageClosure'
out.mkdir(parents=True, exist_ok=True)
paths = ['/Game/_Game/Lockpicking/' + n for n in ('Locked', 'LockedChest', 'LockPickChest')]
paths += ['/Game/_Game/Items/Chests/' + n for n in ('BP_CalystoLockedChest', 'BP_CalystoLockPickChest')]
paths += ['/Game/_Game/Widgets/Equipment/Global/' + n for n in
          ('WBP_SatchelWindow', 'WBP_SatchelTransfer', 'WBP_SatchelContents', 'WBP_EquipmentSaleConfirmation')]
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.wait_for_completion()
dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
dirty += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
assert not dirty, 'Preserve dirty packages before retirement.'
existing = {p for p in paths if unreal.EditorAssetLibrary.does_asset_exist(p)}
options = unreal.AssetRegistryDependencyOptions(True, True, True, True, True)
references = {p: sorted(str(x) for x in registry.get_referencers(p, options)) for p in existing}
external = {p: [x for x in refs if x not in existing] for p, refs in references.items()}
external = {p: refs for p, refs in external.items() if refs}
report = {'status': 'PENDING', 'references': references, 'external': external, 'deleted': []}
(out / 'retirement.json').write_text(json.dumps(report, indent=2))
assert not external, f'Active referencers prevent retirement: {external}'
for p in sorted(existing):
    source = root / 'Content' / (p[6:] + '.uasset')
    backup = out / 'AssetBackup' / (p[6:] + '.uasset')
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(source, backup)
# Delete the closed set together so generated-class references between members
# cannot keep an already reported deletion's package on disk.
assets = [unreal.load_asset(p) for p in sorted(existing)]
assert all(assets)
if assets:
    assert unreal.EditorAssetLibrary.delete_loaded_assets(assets)
report['deleted'] = sorted(existing)
report['remaining_files'] = [p for p in paths if (root / 'Content' / (p[6:] + '.uasset')).exists()]
if report['remaining_files']:
    report['status'] = 'FAIL'
    (out / 'retirement.json').write_text(json.dumps(report, indent=2))
    raise RuntimeError('Unreal left package files after deletion: ' + str(report['remaining_files']))
report['status'] = 'PASS'
(out / 'retirement.json').write_text(json.dumps(report, indent=2))
print('PASS: Exact legacy storage assets retired; LockPick and minigame preserved.')
