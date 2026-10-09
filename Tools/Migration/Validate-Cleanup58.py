"""Compile restored chest contracts, then run the existing bounded PIE fixture."""
import json
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved/CleanupEvidence/20261008'
compiled = []
for path in ('/Game/_Game/Items/Chests/BP_EquipmentDeposit',
             '/Game/_Game/Items/Chests/BP_EquipmentLockedChest'):
    asset = unreal.load_asset(path)
    assert isinstance(asset, unreal.Blueprint), path
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    status = str(asset.get_editor_property('status'))
    assert 'UPTODATE' in ''.join(c for c in status.upper() if c.isalnum()), (path, status)
    compiled.append({'asset': path, 'status': status})
(out / 'chest-blueprints.json').write_text(json.dumps(compiled, indent=2))
runpy.run_path(str(root / 'Tools/BodyPresets/RuntimeQA.py'))
