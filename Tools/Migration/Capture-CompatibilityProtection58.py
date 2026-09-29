"""Task-specific hashes; source project is never opened or hydrated."""
import hashlib, json, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Analysis/CompatibilityClosure'
OUT.mkdir(parents=True, exist_ok=True)
phase = sys.argv[1]
engine = Path('D:/Unreal Engine 5/Library/UE_5.8/Engine')
roots = [engine/'Plugins/Marketplace/ACFUAsce5ab7c1439afbV5', engine/'Plugins/DazToUnreal']
files = [p for root in roots for p in root.rglob('*') if p.is_file()]
files += [ROOT/p for p in ['Content/FullSample/Player.uasset', 'Content/DazToUnreal/Female/Female.uasset',
          'Content/DazToUnreal/Male/Male.uasset', 'Content/DazToUnreal/Multiple/Multiple.uasset', 'Content/_Game/Hub/HUB.umap']]
files += [p for p in (ROOT/'Content').rglob('*Frederick*') if p.is_file()]
hashes = {}
for p in sorted(set(files)):
    with p.open('rb') as f: hashes[str(p)] = hashlib.file_digest(f, 'sha256').hexdigest()
(OUT/f'protected_{phase}.json').write_text(json.dumps(hashes, indent=2), encoding='utf8')
head = subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()
if phase == 'before':
    (OUT/'git_before.txt').write_bytes(subprocess.check_output(['git','status','--short'],cwd=ROOT))
    result = dict(snapshot=head, checked=len(hashes), status='BASELINE')
else:
    before = json.loads((OUT/'protected_before.json').read_text(encoding='utf8'))
    differences = [p for p in before.keys()|hashes.keys() if before.get(p)!=hashes.get(p)]
    result = dict(snapshot=head, checked=len(hashes), differences=differences, status='FAIL' if differences else 'PASS')
(OUT/f'protection_{phase}.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result))
if result['status']=='FAIL': sys.exit(1)
