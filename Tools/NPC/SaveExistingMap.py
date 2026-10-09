"""Preserve pre-existing HUB editor changes before the first cold build."""
from pathlib import Path
import shutil,unreal
root=Path(unreal.Paths.project_dir())
maps=unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert all(p.get_name()=='/Game/_Game/Hub/HUB' for p in maps)
src=root/'Content/_Game/Hub/HUB.umap'; dst=root/'Saved/NPCDAZ/Baseline/preexisting-HUB.umap'
assert not dst.exists(), 'Preserve immutable pre-existing snapshot'
shutil.copy2(src,dst)
assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,False)
print('Pre-existing HUB saved; original disk snapshot retained.')
