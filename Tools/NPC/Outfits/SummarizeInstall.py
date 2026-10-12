"""Summarize a bounded ProfileInstall log, preserving component/LOD sequences."""
import argparse
import collections
import hashlib
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
raw = args.log.read_bytes()
stages = collections.defaultdict(list)
components = collections.defaultdict(list)
reasons = collections.Counter()
reuse = 0
for line in raw.decode('utf-8', errors='replace').splitlines():
    stage = re.search(r'ClothingInstallProfile stage=(\w+) ms=([\d.]+)', line)
    if stage:
        stages[stage[1]].append(float(stage[2]))
    request = re.search(r'ClothingInstallRequest component=(\S+) garment_lod=(\d+) body_lod=(\d+) binding=(\S+)', line)
    if request:
        components[request[1]].append({'garment_lod': int(request[2]),
                                       'body_lod': int(request[3]),
                                       'binding': request[4]})
    reuse += 'ClothingInstall PreparedBindingReuse' in line
    reason = re.search(r'V3 passthrough visible for .*?: (.*)', line)
    if reason:
        reasons[reason[1]] += 1
report = {
    'scope': 'Diagnostic only; includes every component in the supplied log interval',
    'log': str(args.log), 'log_sha256': hashlib.sha256(raw).hexdigest(),
    'stages': {name: {'count': len(values), 'total_ms': round(sum(values), 3),
                      'mean_ms': round(sum(values)/len(values), 3), 'max_ms': max(values)}
               for name, values in stages.items()},
    'prepared_reuse': reuse, 'component_count': len(components),
    'install_count_distribution': dict(collections.Counter(len(v) for v in components.values())),
    'lod_sequence_distribution': dict(collections.Counter(
        ' -> '.join(f"{r['garment_lod']}/{r['body_lod']}" for r in values)
        for values in components.values())),
    'passthrough_reasons': dict(reasons), 'components': dict(components),
}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps({k: v for k, v in report.items() if k != 'components'}, indent=2))
