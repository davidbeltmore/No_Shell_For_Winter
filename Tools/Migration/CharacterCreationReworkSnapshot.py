"""Snapshot protected target inputs and compare subsequent CC rework gates."""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Migration/CharacterCreationRework'
OUT.mkdir(parents=True, exist_ok=True)
baseline = json.loads((ROOT / 'Docs/Migration/Evidence/Phase0_Target_Invariant_Hashes.json').read_text(encoding='utf-8-sig'))
paths = set()
for group in baseline['sets']:
    paths.update(p for p in Path(group['root']).rglob('*') if p.is_file())
paths.update(Path(a['path']) for a in baseline['authoritative_assets'])
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()
state = {str(p): digest(p) for p in sorted(paths)}
phase = sys.argv[1] if len(sys.argv) > 1 else 'before'
target = OUT / (phase + '.json')
if target.exists():
    raise RuntimeError('Snapshot already exists: ' + str(target))
payload = {'phase': phase, 'project': str(ROOT), 'hashes': state,
           'frederick_asset_resolution': baseline.get('frederick_asset_resolution', 'PENDING'),
           'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()}
if phase != 'before':
    before = json.loads((OUT / 'before.json').read_text())['hashes']
    payload['changed'] = [p for p in sorted(set(before) | set(state)) if before.get(p) != state.get(p)]
    payload['result'] = 'FAIL' if payload['changed'] else 'PASS'
else:
    payload['worktree'] = subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True)
target.write_text(json.dumps(payload, indent=2), encoding='utf-8')
print(json.dumps({'path': str(target), 'files': len(state), 'result': payload.get('result', 'BASELINE'), 'changed': payload.get('changed', [])}))
