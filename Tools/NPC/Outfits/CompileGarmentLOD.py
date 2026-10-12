"""Publish only stale native garment bindings, then the derived V5 catalog."""
import json,time,traceback
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir());out=root/'Saved/NPCOutfits/GarmentLOD'/('compile_'+str(time.time_ns()));out.mkdir(parents=True)
start=time.monotonic();report={'status':'PENDING'}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    director=unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
    options=unreal.EFClothingNativeSourceCompileOptions()
    options.set_editor_property('output_root','/EFClothingMorph/_Internal/Compiled/V4')
    options.set_editor_property('maximum_push_cm',2.5)
    options.set_editor_property('only_stale',True)
    options.set_editor_property('strict_catalog_certification',True)
    result=unreal.EFClothingFitCompilerLibrary.compile_native_source_catalog_v4(director,unreal.load_asset('/Game/DazToUnreal/Multiple/Multiple'),options)
    report['compile']=str(result.report);report['rows']=[str(row.report) for row in result.rows]
    assert result.success,report['compile']
    report['sync']=unreal.EFClothingFitCompilerLibrary.sync_unisex_runtime_catalog(director)
    assert report['sync'].startswith('PASS:'),report['sync']
    report['status']='PASS'
except Exception: report.update(status='FAIL',error=traceback.format_exc())
finally:
    report['seconds']=time.monotonic()-start
    (out/'compile.json').write_text(json.dumps(report,indent=2))
    print(str(out/'compile.json'))
if report['status']!='PASS': raise RuntimeError(report['error'])
