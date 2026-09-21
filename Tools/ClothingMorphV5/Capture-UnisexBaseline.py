"""Capture a current-session baseline; never compare against an old migration state."""
import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/ClothingUnisexQA'
OUT.mkdir(parents=True, exist_ok=True)
phase = sys.argv[1] if len(sys.argv) > 1 else 'before'
engine = pathlib.Path('D:/Unreal Engine 5/Library/UE_5.8/Engine')
roots = [engine / 'Plugins/Marketplace/ACFUAsce5ab7c1439afbV5', engine / 'Plugins/DazToUnreal']
files = [p for directory in roots for p in directory.rglob('*') if p.is_file()]
files += [ROOT / p for p in ['Content/FullSample/Player.uasset', 'Content/DazToUnreal/Female/Female.uasset', 'Content/DazToUnreal/Male/Male.uasset', 'Content/DazToUnreal/Multiple/Multiple.uasset']]
files += list((ROOT / 'Content').rglob('*Frederick*'))
hashes = {}
for path in sorted(set(files)):
    if not path.is_file():
        continue
    with path.open('rb') as stream:
        hashes[str(path)] = hashlib.file_digest(stream, 'sha256').hexdigest()
(OUT / f'protected_{phase}.json').write_text(json.dumps(hashes, indent=2))
if phase == 'before':
    (OUT / 'git_before.txt').write_bytes(subprocess.check_output(['git', 'status', '--short'], cwd=ROOT))
    (OUT / 'head.txt').write_bytes(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT))
else:
    baseline = json.loads((OUT / 'protected_before.json').read_text())
    differences = [p for p in baseline.keys() | hashes.keys() if baseline.get(p) != hashes.get(p)]
    print(json.dumps({'status': 'FAIL' if differences else 'PASS', 'differences': differences}))
    if differences:
        sys.exit(1)
print(f'{phase}: {len(hashes)} protected files hashed')
