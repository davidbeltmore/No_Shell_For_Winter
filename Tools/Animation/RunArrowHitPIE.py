"""Physical ACF arrow hits plus strong-hit regression; native 90s/host 120s."""
import runpy,unreal
from pathlib import Path
runpy.run_path(str(Path(unreal.Paths.project_dir())/'Tools/Animation/RunZombieHitPIE.py'),init_globals={'ZOMBIE_HIT_MODE':'arrow'})
