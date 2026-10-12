"""Export a bounded stable tail of an outfit diagnostic trace with UE Insights.

This is attribution, not acceptance. UE 5.8's statistics exporter includes GPU
queues even with a CPU thread filter; summarize only the explicitly owned CPU
scopes, and retain the unfiltered CSV for inspection.
"""
import argparse
import csv
import hashlib
import json
import math
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('name')
parser.add_argument('--seconds', type=float, default=6)
parser.add_argument('--tail-margin', type=float, default=2)
args = parser.parse_args()
assert args.name.replace('_', '').replace('-', '').isalnum()
assert args.seconds > 0 and args.tail_margin >= 2
root = Path(__file__).resolve().parents[3]
out = root / 'Saved/NPCOutfits/Native' / args.name
host = json.loads((out / 'host.json').read_text(encoding='utf-8-sig'))
runtime = json.loads((out / 'runtime.json').read_text(encoding='utf-8-sig'))
assert host['status'] == runtime['status'] == 'PASS' and host['trace']
assert runtime['seconds'] >= args.seconds + args.tail_margin + 2
assert not (out / 'cpu-attribution.json').exists(), 'Preserve previous exports'
trace = out / 'capture.utrace'
insights = Path('D:/Unreal Engine 5/Library/UE_5.8/Engine/Binaries/Win64/UnrealInsights.exe')


def export(label, commands):
    script = out / (label + '-commands.txt')
    script.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    # Unreal parses its raw Windows command line: quotes belong around the
    # value after '=', whereas subprocess's list quoting wraps the whole token.
    command = (f'"{insights}" -OpenTraceFile="{trace}" -AutoQuit -NoUI '
               f'-abslog="{out / (label + "-insights.log")}" '
               f'-ExecOnAnalysisCompleteCmd="@={script}"')
    result = subprocess.run(command, shell=False, startupinfo=startup,
                            timeout=120, capture_output=True)
    assert result.returncode == 0, result.returncode


frames_file = out / 'cpu-frames.csv'
export('cpu-frames', [
    f'TimingInsights.ExportTimingEvents "{frames_file.as_posix()}" '
    '-threads=GameThread -timers=FEngineLoop::Tick -columns=StartTime,EndTime,Duration'
])
with frames_file.open(encoding='utf-8-sig', newline='') as handle:
    frames = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(handle)]
frames = [row for row in frames if math.isfinite(row['EndTime'])]
end_limit = frames[-1]['EndTime'] - args.tail_margin
selected = [row for row in frames
            if row['StartTime'] >= end_limit - args.seconds and row['EndTime'] <= end_limit]
assert len(selected) >= 30
start, end = selected[0]['StartTime'], selected[-1]['EndTime']
assert end - start >= args.seconds * .9
commands = []
for label, thread in [('game', 'GameThread'), ('render', 'RenderThread')]:
    dest = out / ('cpu-' + label + '.csv')
    commands.append(f'TimingInsights.ExportTimerStatistics "{dest.as_posix()}" '
                    f'-threads={thread} -startTime={start:.6f} -endTime={end:.6f} '
                    '-sortBy=TotalInclusiveTime')
export('cpu-statistics', commands)
scopes = {}
with (out / 'cpu-game.csv').open(encoding='utf-8-sig', newline='') as handle:
    for row in csv.DictReader(handle):
        if row['Name'].startswith('EFClothing_') or row['Name'] == 'EFClothingRuntime':
            scopes[row['Name']] = {
                'count': int(row['Count']),
                'inclusive_ms_per_frame': float(row['Incl']) * 1000 / len(selected),
                'exclusive_ms_per_frame': float(row['Excl']) * 1000 / len(selected),
            }
assert scopes, 'No owned CPU scopes exported'
receipt = {
    'status': 'DIAGNOSTIC', 'name': args.name,
    'trace_sha256': hashlib.sha256(trace.read_bytes()).hexdigest(),
    'start_seconds': start, 'end_seconds': end, 'frames': len(selected),
    'tail_margin_seconds': args.tail_margin,
    'scope': 'All scene garments, including map NPCs; nested inclusive times are not additive.',
    'exporter_caveat': 'UE 5.8 statistics CSV includes GPU queues despite CPU thread filter.',
    'scopes': scopes,
}
(out / 'cpu-attribution.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
print(json.dumps(receipt, indent=2))
