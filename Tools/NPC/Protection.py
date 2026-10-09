"""Independent before/after protected baseline for the NPC DAZ rollout."""
import importlib.util
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('body_protection',root/'Tools/BodyPresets/CaptureBaseline.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.OUT=root/'Saved/NPCDAZ/Baseline'
module.main()
