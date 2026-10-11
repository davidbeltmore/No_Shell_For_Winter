"""Author only the owned bow overlay/profiles, using the shared bounded policy."""
import runpy,unreal
from pathlib import Path
runpy.run_path(str(Path(unreal.Paths.project_dir())/'Tools/Animation/AuthorDAZWeaponOverlays.py'),init_globals={'DAZ_WEAPONS':['Bow']})
runpy.run_path(str(Path(unreal.Paths.project_dir())/'Tools/Animation/AuthorDAZBowAimOffset.py'))
