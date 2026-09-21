"""Summarize raw wall-clock observations; no claim of pixel-present timing."""
import argparse
import hashlib
import json
import math
import pathlib
import statistics

ROOT = pathlib.Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('runs', nargs='+')
args = parser.parse_args()


def stats(values):
    values = sorted(values)
    if not values:
        return None
    return dict(n=len(values), min=values[0], median=statistics.median(values),
                mean=statistics.mean(values), p95=values[math.ceil(.95*len(values))-1], max=values[-1])


output = dict(units='seconds, wall clock', ready='Render-dispatch validation observed on Slate tick, NOT pixel presentation', runs={})
for name in args.runs:
    path = ROOT/'Saved/ClothingLatencyQA'/name/'result.json'
    raw = json.loads(path.read_text(encoding='utf-8'))
    trials = [t for t in raw['trials'] if 'ready_s' in t]
    entry = dict(status=raw['status'], mode=raw['mode'], input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                 ready_s=stats([t['ready_s'] for t in trials]),
                 component_s=stats([t['component_s'] for t in trials]),
                 waiting_to_manage_s=stats([t['managed_s']-t['component_s'] for t in trials]),
                 managed_to_ready_s=stats([t['ready_s']-t['managed_s'] for t in trials]),
                 below_half_second=sum(t['ready_s'] <= .5 for t in trials),
                 observer_cost_s=stats([c for t in trials for c in t['observer_cost_s']]),
                 callback_gap_s=stats([c for t in trials for c in t['callback_gaps_s']]),
                 by_gender={}, by_repeat={}, by_garment={})
    for group, field in (('by_gender','gender'),('by_repeat','repeat'),('by_garment','garment')):
        for value in sorted(set(t[field] for t in trials)):
            entry[group][str(value)] = stats([t['ready_s'] for t in trials if t[field] == value])
    if trials and 'kind' in trials[0]:
        entry['by_kind'] = {kind: stats([t['ready_s'] for t in trials if t['kind'] == kind])
                            for kind in sorted(set(t['kind'] for t in trials))}
    output['runs'][name] = entry
dest = ROOT/'Saved/ClothingLatencyQA/summary.json'
dest.write_text(json.dumps(output, indent=2), encoding='utf-8')
for name, run in output['runs'].items():
    print(name, json.dumps({k: run[k] for k in ('status','ready_s','waiting_to_manage_s','managed_to_ready_s','below_half_second')}))
