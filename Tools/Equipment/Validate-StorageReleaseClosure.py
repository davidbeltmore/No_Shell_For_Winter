"""Validate storage dependencies; compile Blueprints in a separate UE invocation."""
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
assert root == Path('D:/Projects UE5/NoShellForWinter').resolve()
for relative in (
    'Tools/Equipment/Finalize-StorageAssets.py',
    'Tools/Migration/Create-CalystoDungeonDirectorRuntimeAssetsV6.py',
    'Tools/Migration/Validate-CalystoDungeonDirectorV6CookClosure.py',
):
    unreal.log('Storage release closure: ' + relative)
    runpy.run_path(str(root / relative), run_name='__main__')
print('PASS: Native storage asset retirement and cook closure. Compile Blueprints in a fresh editor process.')
