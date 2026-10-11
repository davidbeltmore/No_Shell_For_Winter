"""Reuse the shared player fitting/coverage regression after shared graph changes.

This supplements NPC fixtures; it does not certify NPC AI. SaveGuard is required.
"""
import argparse
import os
import runpy
import time
from pathlib import Path
import unreal

parser = argparse.ArgumentParser()
parser.add_argument('--bodies', default='Female.01,Male.01')
parser.add_argument('--timeout', type=float, default=120)
parser.add_argument('--immutable-buffers', type=int, choices=[0, 1], default=1)
args = parser.parse_args(globals().get('NPC_OUTFIT_ARGS', []))
root = Path(unreal.Paths.project_dir()).resolve()
bodies = args.bodies.split(',')
assert bodies and 0 < args.timeout <= 120
out = root/'Saved/NPCOutfits/ClothingRegression'/str(time.time_ns())
options = {
    'EF_UNISEX_OUTPUT_DIR': str(out),
    'EF_UNISEX_BODY_PRESETS': ','.join(bodies+[bodies[0]]),
    'EF_UNISEX_EXTENDED_ONLY': '1',
    'EF_UNISEX_KEEP_EDITOR': '1',
    'EF_UNISEX_TIMEOUT': str(args.timeout),
    'EF_UNISEX_IMMUTABLE_BUFFERS': str(args.immutable_buffers),
}
previous = {key: os.environ.get(key) for key in options}
try:
    os.environ.update(options)
    runpy.run_path(str(root/'Tools/ClothingMorphV5/Validate-UnisexPIE58.py'))
finally:
    for key, value in previous.items():
        if value is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = value
print('Shared clothing regression scheduled: '+str(out))
