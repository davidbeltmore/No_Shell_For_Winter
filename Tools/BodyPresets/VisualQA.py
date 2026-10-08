"""Exercise the shared clothing QA against all three catalog bodies and the return transition."""
import os
import runpy
import time
from pathlib import Path
import unreal
root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/BodyPresetsQA' / ('Visual_' + time.strftime('%Y%m%d_%H%M%S'))
out.mkdir(parents=True)
os.environ['EF_UNISEX_OUTPUT_DIR'] = str(out)
os.environ['EF_UNISEX_BODY_PRESETS'] = 'Female.01,Male.01,Male.02,Female.01'
os.environ['CODEX_EF_CLOTHING_V4_BP_REPORT'] = str(out / 'blueprints.json')
perf = unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
(root / 'Saved/BodyPresetsQA/latest-visual.txt').write_text(str(out))
runpy.run_path(str(root / 'Tools/ClothingMorphV5/Validate-UnisexPIE58.py'))
