"""Read-only outfit/roster contract audit; optional compile never saves protected assets."""
import argparse
import json
import math
import time
import traceback
from pathlib import Path
import unreal

parser = argparse.ArgumentParser()
parser.add_argument('--timeout', type=float, default=120)
parser.add_argument('--compile', action='store_true')
parser.add_argument('--require-all', action='store_true')
args = parser.parse_args(globals().get('NPC_OUTFIT_ARGS', []))
root = Path(unreal.Paths.project_dir()).resolve()
out = root/'Saved/NPCOutfits'
out.mkdir(parents=True, exist_ok=True)
start = time.monotonic()
report = {'status': 'PENDING', 'configured': 0, 'unconfigured': 0, 'failed': 0, 'checks': [], 'errors': []}

def check(name, ok):
    report['checks'].append({'name': name, 'status': 'PASS' if ok else 'FAIL'})
    if not ok:
        report['errors'].append(name)

try:
    manifest = json.loads((root/'Tools/NPC/Roster.json').read_text())
    source = {r['id']: r for r in manifest['archetypes']}
    report['excluded_nonhumanoid'] = manifest.get('excluded_nonhumanoid', [])
    report['excluded_non_npc_categories'] = manifest.get('excluded_non_npc', [])
    report['failed_variants'] = 0
    roster = unreal.load_asset('/Game/_Game/Characters/Data/DA_NPCRoster')
    check('Roster IDs', set(source) == {str(row.id) for row in roster.archetypes})
    for row in roster.archetypes:
        assert time.monotonic()-start < args.timeout
        row_id = str(row.id)
        errors_before = len(report['errors'])
        choices = list(row.outfits)
        check(row_id+' choices', len(choices) == len(source[row_id].get('outfits', [])))
        report['configured' if choices else 'unconfigured'] += 2
        if args.require_all: check(row_id+' enabled', bool(choices))
        if choices: check(row_id+' positive total weight', sum(c.weight for c in choices) > 0)
        for index, choice in enumerate(choices):
            asset = choice.outfit
            check(row_id+' valid choice', bool(asset) and math.isfinite(choice.weight) and choice.weight >= 0)
            if not asset: continue
            expected = source[row_id].get('outfits', [])
            check(row_id+' exact authored choice '+str(index), index < len(expected)
                  and str(asset.id) == expected[index]['id']
                  and math.isclose(choice.weight, expected[index].get('weight',1),rel_tol=1e-6))
            check(row_id+' item count', 1 <= len(asset.items) <= 16)
            slots = []
            for item in asset.items:
                slot = str(item.destination.get_editor_property('TagName'))
                cls = item.item
                valid_slots = {str(t.get_editor_property('TagName')) for t in unreal.get_default_object(cls).get_possible_item_slots()}
                check(row_id+' destination '+slot, slot in valid_slots and all(not (slot==old or slot.startswith(old+'.') or old.startswith(slot+'.')) for old in slots))
                slots.append(slot)
        shared_failed = len(report['errors']) > errors_before
        for cls in [row.male, row.female]:
            class_errors_before = len(report['errors'])
            path = cls.get_path_name().split('.')[0]
            check(path+' owned', path.startswith('/Game/_Game/Characters/'))
            cdo = unreal.get_default_object(cls)
            check(path+' variation', 'EF.BodyVariation' in {str(t) for t in cdo.tags})
            if args.compile: check(path+' compile', unreal.BlueprintEditorLibrary.compile_blueprint(unreal.load_asset(path)))
            if shared_failed or len(report['errors']) > class_errors_before: report['failed_variants'] += 1
    report['status'] = 'FAIL' if report['errors'] else 'PASS'
except Exception:
    report['status'] = 'FAIL'
    report['errors'].append(traceback.format_exc())
report['seconds'] = time.monotonic()-start
report['failed'] = len(report['errors'])
(out/'audit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k != 'checks'}))
assert report['status'] == 'PASS'
