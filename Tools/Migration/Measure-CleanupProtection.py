"""Hash productive files without changing them; compare with a fresh baseline."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
p.add_argument('--baseline', type=Path)
p.add_argument('--expected-change', action='append', type=Path, default=[])
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
roots = [root / n for n in ('Content', 'Config', 'Source')]
roots += [root / 'Plugins']
roots += [Path('D:/Unreal Engine 5/Library/UE_5.8/Engine/Plugins/DazToUnreal')]
rows = {}
for folder in roots:
    for f in folder.rglob('*'):
        if not f.is_file() or any(x in ('Binaries', 'Intermediate', '__pycache__') for x in f.relative_to(folder).parts):
            continue
        with f.open('rb') as stream:
            rows[str(f)] = hashlib.file_digest(stream, 'sha256').hexdigest()
for f in root.glob('*.uproject'):
    rows[str(f)] = hashlib.sha256(f.read_bytes()).hexdigest()
result = {'root': str(root), 'files': rows}
if a.baseline:
    old = json.loads(a.baseline.read_text())['files']
    result['differences'] = [k for k in old.keys() | rows.keys() if old.get(k) != rows.get(k)]
    expected = {str(p.resolve()) for p in a.expected_change}
    result['expected_differences'] = [k for k in result['differences'] if k in expected]
    result['unexpected_differences'] = [k for k in result['differences'] if k not in expected]
    result['result'] = 'PASS' if not result['unexpected_differences'] else 'FAIL'
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(result, indent=2))
print(json.dumps({'files': len(rows), 'result': result.get('result', 'BASELINE'), 'differences': result.get('differences', [])}))
