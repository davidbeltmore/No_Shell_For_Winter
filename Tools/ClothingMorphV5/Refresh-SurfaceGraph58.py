"""Refresh only the owned surface graph after a kernel-source change; use the live Python bridge."""
import json
import shutil
import time
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/ClothingMorphV4QA/SurfaceGraph'/str(time.time_ns())
out.mkdir(parents=True)
started=time.monotonic()
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
source=root/'Plugins/EFClothingMorph/Content/Deformers/DG_EFGarmentSurfaceConstraint.uasset'
assert source.is_file()
shutil.copy2(source,out/source.name)
result=unreal.EFClothingSurfaceDeformerBuilderLibrary.build_or_update_surface_constraint_deformer(False)
report={'status':'PASS' if result.success else 'FAIL','rebuilt':result.rebuilt,
        'asset':'/EFClothingMorph/Deformers/DG_EFGarmentSurfaceConstraint.DG_EFGarmentSurfaceConstraint',
        'report':result.report,'seconds':time.monotonic()-started}
(out/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
assert result.success, result.report
