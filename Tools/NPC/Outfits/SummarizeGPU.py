"""Extract the last graphics ProfileGPU frame; diagnostic, never an FPS gate."""
import argparse
import hashlib
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
raw = args.log.read_bytes()
lines = raw.decode('utf-8', errors='replace').splitlines()
starts = [i for i, line in enumerate(lines)
          if re.search(r'GPU Profile for Frame \d+ - Graphics pipeline 0 - GPU 0', line)]
assert starts, 'No detailed graphics frame: use RunNativePIE --profile-gpu first'
frame = [line for line in lines[starts[-1]:] if 'LogRHI: Display:' in line]
kernels = {}
for line in frame:
    match = re.search(r'([0-9.]+)ms\s+([^:]+):UpdateGraph_ComputeGraph:(EF_\w+)_OptimusNode', line)
    if not match:
        continue
    milliseconds = float(match[1])
    row = kernels.setdefault(match[3], {'count': 0, 'total_ms': 0.0, 'max_ms': 0.0})
    row['count'] += 1
    row['total_ms'] += milliseconds
    row['max_ms'] = max(row['max_ms'], milliseconds)
assert kernels, 'No named owned garment kernels found in the last graphics frame'
frame_text = '\n'.join(frame)+'\n'
for row in kernels.values():
    row['total_ms'] = round(row['total_ms'], 6)
result = {
    'status': 'PASS',
    'scope': 'Last graphics GPU 0 frame; includes background HUB garments. '
             'Rounded 0.01 ms event times; diagnostic only, not crowd acceptance.',
    'source': str(args.log.resolve()),
    'source_sha256': hashlib.sha256(raw).hexdigest(),
    'retained_frame_sha256': hashlib.sha256(frame_text.encode('utf-8')).hexdigest(),
    'frame': frame[0],
    'kernels': kernels,
}
assert not args.output.exists(), 'Preserve prior receipts; use a fresh output path'
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.with_suffix('.log').write_text(frame_text, encoding='utf-8')
args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result))
