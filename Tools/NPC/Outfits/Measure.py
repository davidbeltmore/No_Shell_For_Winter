"""Compare native outfit performance JSON receipts against the frozen budget.

Consumes measurements, never infers a PASS from missing CPU/GPU/frame samples.
Input rows: count, dressed, loading (cold/warm), cpu_ms, gpu_ms, frame_ms,
spawn_peak_ms, outfit_ready_seconds, pending_after_destroy, seconds.
"""
import argparse
import json
import math
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('undressed', type=Path)
parser.add_argument('dressed', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
left = json.loads(args.undressed.read_text(encoding='utf-8-sig'))
right = json.loads(args.dressed.read_text(encoding='utf-8-sig'))

def p95(values):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered)*.95)-1)]

def mean(values):
    return sum(values)/len(values)

result = {'status': 'PENDING', 'budget': {'cpu_increment_ms': 2, 'gpu_increment_ms': 3, 'frame_p95_increment_ms': 4, 'warm_ready_seconds': 2},
          'sources': [str(args.undressed.resolve()), str(args.dressed.resolve())], 'reasons': []}
try:
    for key in ['count', 'seed', 'body', 'archetype', 'resolution', 'loading', 'camera', 'quality']:
        assert left[key] == right[key], 'Unmatched comparison: '+key
    assert left['dressed'] is False and right['dressed'] is True, 'Expected undressed then dressed'
    assert left['seconds'] >= 30 and right['seconds'] >= 30, 'Capture shorter than 30 seconds'
    for row in [left, right]:
        for key in ['cpu_ms', 'gpu_ms', 'frame_ms']:
            assert len(row[key]) >= 30 and all(math.isfinite(v) and v > 0 for v in row[key]), 'Missing/invalid samples: '+key
    result['count'] = right['count']
    result['loading'] = right['loading']
    result['measurements'] = {
        'cpu_increment_ms': mean(right['cpu_ms'])-mean(left['cpu_ms']),
        'gpu_increment_ms': mean(right['gpu_ms'])-mean(left['gpu_ms']),
        'frame_p95_increment_ms': p95(right['frame_ms'])-p95(left['frame_ms']),
        'dressed_frame_p95_ms': p95(right['frame_ms']),
        'baseline_frame_p95_ms': p95(left['frame_ms']),
        'spawn_peak_ms': right['spawn_peak_ms'],
        'warm_ready_seconds': right['outfit_ready_seconds'],
        'pending_after_destroy': right['pending_after_destroy']}
    diagnostic = right['count'] == 50 or right['loading'] != 'warm'
    result['acceptance_applies'] = not diagnostic
    if right['pending_after_destroy'] != 0:
        result['status'] = 'FAIL'; result['reasons'].append('Outstanding outfit work after teardown')
    elif diagnostic:
        result['status'] = 'PENDING'; result['reasons'].append('Diagnostic measurement; no crowd-capacity acceptance claim')
    else:
        for key, limit in result['budget'].items():
            if result['measurements'][key] > limit: result['reasons'].append(key+' exceeds frozen budget')
        result['status'] = 'FAIL' if result['reasons'] else 'PASS'
except (KeyError, AssertionError, TypeError, ZeroDivisionError) as error:
    result['reasons'].append(str(error))
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['status'] == 'PASS' else 1)
