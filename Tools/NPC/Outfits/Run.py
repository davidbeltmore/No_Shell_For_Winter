"""Execute bounded outfit editor authoring/audit with parameters via the existing localhost bridge.

Example: python Tools/NPC/Outfits/Run.py Audit --compile --timeout 120
The editor remains open. Runtime suites must also have a fresh completion receipt.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('action', choices=['Inspect', 'Author', 'Audit', 'RunPIE', 'RunNativePIE', 'SaveQA', 'LifecycleQA', 'ClothingRegression', 'LogicQA', 'ProbeGarmentLOD'])
parser.add_argument('--host-timeout', type=float, default=150)
args, remaining = parser.parse_known_args()
out = root/'Saved/NPCOutfits/Invocations'/str(time.time_ns())
out.mkdir(parents=True)
script = root/'Tools/NPC/Outfits'/f'{args.action}.py'
assert script.is_file()
launcher = out/'invoke.py'
launcher.write_text(f"NPC_OUTFIT_ARGS = {remaining!r}\nexec(compile(open({str(script)!r}, encoding='utf-8').read(), {str(script)!r}, 'exec'))\n", encoding='utf-8')
start = time.monotonic()
with (out/'host.log').open('w', encoding='utf-8') as log:
    try:
        result = subprocess.run([sys.executable, str(root/'Tools/InnerState/RunEditorPython58.py'), str(launcher)],
                                cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=args.host_timeout)
        status = 'PASS' if result.returncode == 0 else 'FAIL'
        reason = 'Editor invocation returned; inspect action receipt' if status == 'PASS' else 'See host.log'
    except subprocess.TimeoutExpired:
        status, reason = 'FAIL', 'Host timeout; editor operation may still be running. Inspect before another dependent operation.'
receipt = {'status': status, 'seconds': time.monotonic()-start, 'reason': reason, 'action': args.action, 'arguments': remaining}
(out/'host.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
print(json.dumps(receipt))
print(out)
if status != 'PASS':
    print((out/'host.log').read_text(encoding='utf-8'))
    raise SystemExit(1)
