"""Capture or compare a task-specific protected baseline without changing assets."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('D:/Unreal Engine 5/Library/UE_5.8/Engine')
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--capture', action='store_true')
args = parser.parse_args()
roots = [ENGINE / 'Plugins/Marketplace/ACFUAsce5ab7c1439afbV5', ENGINE / 'Plugins/DazToUnreal']
assert all(p.is_dir() for p in roots), 'Protected plugin locations must exist'
files = [p for root in roots for p in root.rglob('*') if p.is_file()]
files += [ROOT / p for p in ('Content/FullSample/Player.uasset', 'Content/DazToUnreal/Female/Female.uasset',
          'Content/DazToUnreal/Male/Male.uasset', 'Content/DazToUnreal/Multiple/Multiple.uasset')]
files += [p for p in (ROOT / 'Content').rglob('*Frederick*') if p.is_file()]
hashes = {}
for path in sorted(set(files)):
    with path.open('rb') as stream:
        hashes[str(path)] = hashlib.file_digest(stream, 'sha256').hexdigest()
if args.capture:
    args.baseline.parent.mkdir(parents=True, exist_ok=True)
    with args.baseline.open('x', encoding='utf-8') as stream:
        json.dump(hashes, stream, indent=2)
    print(json.dumps({'status': 'CAPTURED', 'files': len(hashes), 'baseline': str(args.baseline)}))
else:
    before = json.loads(args.baseline.read_text(encoding='utf-8-sig'))
    changed = sorted(p for p in before.keys() | hashes.keys() if before.get(p) != hashes.get(p))
    print(json.dumps({'status': 'FAIL' if changed else 'PASS', 'files': len(hashes), 'differences': changed}, indent=2))
    raise SystemExit(bool(changed))
