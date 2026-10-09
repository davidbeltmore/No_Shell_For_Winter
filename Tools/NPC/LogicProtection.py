"""Preserve the current installed plugin state; report prior-release drift separately."""
import importlib.util,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('npc_protection',root/'Tools/BodyPresets/CaptureBaseline.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
module.OUT=root/'Saved/NPCLogic/Baseline'; module.main()
if sys.argv[1]=='before':
    old=json.loads((root/'Saved/NPCDAZ/Baseline/before.json').read_text())['hashes']
    now=json.loads((module.OUT/'before.json').read_text())['hashes']
    drift=[p for p in old.keys()|now.keys() if old.get(p)!=now.get(p)]
    (module.OUT/'preexisting-plugin-drift.json').write_text(json.dumps({'changed_count':len(drift),'changed':drift},indent=2))
    print('Pre-existing drift from prior task: '+str(len(drift)))
