"""Capture protected disk invariants and scoped rollback files for body onboarding."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
ENGINE = pathlib.Path('D:/Unreal Engine 5/Library/UE_5.8/Engine')
OUT = ROOT / 'Saved/BodyPresetsQA/Baseline'

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=['before', 'after'])
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if args.phase == 'before' and (OUT / 'before.json').exists():
        raise SystemExit('Existing baseline preserved; use after to compare.')
    start = time.monotonic()
    protected = []
    for folder in ['Plugins/Marketplace/ACFUAsce5ab7c1439afbV5', 'Plugins/DazToUnreal']:
        root = ENGINE / folder
        if not root.is_dir(): raise RuntimeError(f'Missing protected root: {root}')
        protected += [p for p in root.rglob('*') if p.is_file()]
    for rel in ['Content/FullSample/Player.uasset', 'Content/DazToUnreal/Female/Female.uasset',
                'Content/DazToUnreal/Male/Male.uasset', 'Content/DazToUnreal/Multiple/Multiple.uasset',
                'Content/FullSample/GASP/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin.uasset']:
        p = ROOT / rel
        if not p.is_file(): raise RuntimeError(f'Missing invariant: {p}')
        protected.append(p)
    protected += [p for p in (ROOT / 'Content').rglob('*Frederick*') if p.is_file()]
    hashes = {}
    for p in sorted(set(protected)):
        with p.open('rb') as f: hashes[str(p)] = hashlib.file_digest(f, 'sha256').hexdigest()
    result = {'hashes': hashes, 'seconds': round(time.monotonic()-start, 2),
              'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()}
    if args.phase == 'before':
        (OUT / 'git-before.txt').write_bytes(subprocess.check_output(['git', 'status', '--short'], cwd=ROOT))
        for rel in ['Content/DazToUnreal/Male2/Male2.uasset',
                    'Content/DazToUnreal/Male2/Male2_Skeleton.uasset',
                    'Content/DazToUnreal/Male2/Male2_PhysicsAsset.uasset', 'Config/DefaultGame.ini']:
            source = ROOT / rel
            dest = OUT / 'originals' / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, dest)
    else:
        before = json.loads((OUT / 'before.json').read_text())['hashes']
        result['changed'] = [p for p in before.keys() | hashes.keys() if before.get(p) != hashes.get(p)]
        result['status'] = 'FAIL' if result['changed'] else 'PASS'
    (OUT / f'{args.phase}.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k != 'hashes'}))
    print(f'{len(hashes)} protected files hashed')
    if result.get('changed'): raise SystemExit(1)

if __name__ == '__main__': main()
