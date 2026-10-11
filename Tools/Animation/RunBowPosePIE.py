"""Reuse the bounded native equipment/pose fixture for four directional bow cases."""
import runpy,unreal
from pathlib import Path
runpy.run_path(str(Path(unreal.Paths.project_dir())/'Tools/Animation/RunGunPosePIE.py'),init_globals={'GUN_POSE_MODE':'bow'})
