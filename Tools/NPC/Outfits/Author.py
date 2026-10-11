"""Author only outfit definitions and roster assignments. Never rewrites character Blueprints."""
import argparse
import json
import shutil
import time
import traceback
from pathlib import Path
import unreal

parser = argparse.ArgumentParser()
parser.add_argument('--timeout', type=float, default=120)
args = parser.parse_args(globals().get('NPC_OUTFIT_ARGS', []))
root = Path(unreal.Paths.project_dir()).resolve()
out = root/'Saved/NPCOutfits'
out.mkdir(parents=True, exist_ok=True)
start = time.monotonic()
report = {'status': 'PENDING', 'assets': [], 'configured': 0, 'unconfigured': 0, 'errors': []}
eal = unreal.EditorAssetLibrary

def backup(asset):
    source = root/'Content'/asset.removeprefix('/Game/')
    source = source.with_suffix('.uasset')
    if source.exists():
        dest = out/'AuthorBackups'/str(time.time_ns())/source.relative_to(root)
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)

try:
    manifest = json.loads((root/'Tools/NPC/Outfits/Definitions.json').read_text())
    assets = {}
    for row in manifest['outfits']:
        assert time.monotonic()-start < args.timeout
        path = row['asset']
        assert path.startswith('/Game/_Game/Characters/Data/Outfits/')
        assert row['id'] not in assets, 'Duplicate outfit ID'
        backup(path)
        asset = unreal.load_asset(path) if eal.does_asset_exist(path) else None
        if asset is None:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.ProjectNPCOutfit)
            folder, name = path.rsplit('/', 1)
            asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.ProjectNPCOutfit, factory)
        assert isinstance(asset, unreal.ProjectNPCOutfit), path
        entries = []
        for garment in row['items']:
            cls = unreal.load_class(None, garment['class'])
            assert cls, garment['class']
            native_slots = list(unreal.get_default_object(cls).get_possible_item_slots())
            slots = {str(t.get_editor_property('TagName')) for t in native_slots}
            assert garment['destination'] in slots, (garment, slots)
            entry = unreal.ProjectNPCOutfitItem()
            entry.set_editor_property('item', cls)
            tag = next(t for t in native_slots if str(t.get_editor_property('TagName')) == garment['destination'])
            entry.set_editor_property('destination', tag)
            entries.append(entry)
        asset.set_editor_property('id', row['id'])
        asset.set_editor_property('items', entries)
        assert eal.save_loaded_asset(asset, only_if_is_dirty=False)
        assets[row['id']] = asset
        report['assets'].append(path)
    roster_path = '/Game/_Game/Characters/Data/DA_NPCRoster'
    roster = unreal.load_asset(roster_path)
    manifest = json.loads((root/'Tools/NPC/Roster.json').read_text())
    assignments = {row['id']: row.get('outfits', []) for row in manifest['archetypes']}
    rows = list(roster.archetypes)
    assert set(assignments) == {str(row.id) for row in rows}
    backup(roster_path)
    for row in rows:
        assert time.monotonic()-start < args.timeout
        choices = []
        for choice in assignments[str(row.id)]:
            entry = unreal.ProjectNPCOutfitChoice()
            entry.set_editor_property('outfit', assets[choice['id']])
            entry.set_editor_property('weight', choice.get('weight', 1.0))
            choices.append(entry)
        row.set_editor_property('outfits', choices)
        report['configured' if choices else 'unconfigured'] += 2
    roster.set_editor_property('archetypes', rows)
    assert eal.save_loaded_asset(roster, only_if_is_dirty=False)
    report['assets'].append(roster_path)
    report['status'] = 'PASS'
except Exception:
    report['status'] = 'FAIL'
    report['errors'].append(traceback.format_exc())
finally:
    report['seconds'] = time.monotonic()-start
    (out/'author.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))
assert report['status'] == 'PASS'
