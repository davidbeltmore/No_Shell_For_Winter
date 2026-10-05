"""Hash protected TARGET inputs for Inner State. Never opens the source project."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Migration/InnerState20261005'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phase')
    args = parser.parse_args()
    baseline = json.loads((ROOT / 'Docs/Migration/Evidence/Phase0_Target_Invariant_Hashes.json').read_text(encoding='utf-8-sig'))
    paths = set()
    allowed = [ROOT.resolve(), Path('D:/Unreal Engine 5/Library/UE_5.8').resolve()]
    for group in baseline['sets']:
        root = Path(group['root']).resolve()
        if not any(root.is_relative_to(a) for a in allowed):
            raise ValueError(f'Not a target protected root: {root}')
        paths.update(p for p in root.rglob('*') if p.is_file())
    for asset in baseline['authoritative_assets']:
        path = Path(asset['path']).resolve()
        if not path.is_relative_to(ROOT.resolve()):
            raise ValueError(f'Not a target protected asset: {path}')
        paths.add(path)
    hashes = {}
    for path in sorted(paths):
        with path.open('rb') as stream:
            hashes[str(path)] = hashlib.file_digest(stream, 'sha256').hexdigest()
    OUT.mkdir(parents=True, exist_ok=True)
    dest = OUT / f'protected_{args.phase}.json'
    if dest.exists():
        raise FileExistsError(dest)
    data = {'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'hashes': hashes, 'phase': args.phase, 'status': 'BASELINE'}
    if args.phase != 'before':
        old = json.loads((OUT / 'protected_before.json').read_text())['hashes']
        data['changed'] = [p for p in sorted(old.keys() | hashes.keys()) if old.get(p) != hashes.get(p)]
        data['status'] = 'FAIL' if data['changed'] else 'PASS'
    dest.write_text(json.dumps(data, indent=2), encoding='utf8')
    print(json.dumps({k: v for k, v in data.items() if k != 'hashes'}))
    if data['status'] == 'FAIL':
        raise SystemExit(1)

if __name__ == '__main__':
    main()
