"""Disposable G9 conversion fixture. Never touches production bodies or DAZ exports."""
import json
import time
import traceback
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
assert root==Path('D:/Projects UE5/NoShellForWinter')
out=root/'Saved/BodyPresetsQA/fresh-conversion.json'
report={'status':'PENDING','started':time.monotonic()}
folder='/Game/DazToUnreal/__BodyPresetQA'
try:
    path=folder+'/G9ConversionFixture'
    assert not unreal.EditorAssetLibrary.does_asset_exist(path),'Fixture exists; inspect previous result before another run'
    task=unreal.AssetImportTask()
    task.filename='D:/DAZ/Male2/UpdatedFBX/Male2.fbx'
    task.destination_path=folder
    task.destination_name='G9ConversionFixture'
    task.automated=True
    task.save=False
    task.replace_existing=False
    # Use the same legacy FBX factory as the installed DAZ bridge. Interchange's
    # FbxImportUI conversion silently omitted the requested physics asset in 5.8.
    task.factory=unreal.FbxFactory()
    options=unreal.FbxImportUI()
    options.import_as_skeletal=True
    options.import_mesh=True
    options.import_animations=False
    options.import_materials=False
    options.import_textures=False
    options.create_physics_asset=True
    options.automated_import_should_detect_type=False
    options.mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH
    options.skeletal_mesh_import_data.set_editor_property('import_morph_targets',True)
    task.options=options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    report['imported_paths']=list(task.imported_object_paths)
    mesh=next(unreal.load_asset(p) for p in task.imported_object_paths if isinstance(unreal.load_asset(p),unreal.SkeletalMesh))
    local=mesh.get_editor_property('skeleton')
    lib=unreal.EFBodyOnboardingLibrary
    report['before']=json.loads(lib.inspect_body(mesh,local))
    report['conversion']=lib.convert_genesis9_body_report(mesh,local,unreal.load_asset('/Game/DazToUnreal/Male/Male'))
    assert report['conversion'].startswith('PASS:'),report['conversion']
    report['after']=json.loads(lib.inspect_body(mesh,local))
    assert report['after']['assignedSkeleton']==local.get_path_name()
    assert report['after']['assignedCompatible']
    assert 'spine_01' in {b['name'] for b in report['after']['bones']}
    assert 'hip' not in {b['name'] for b in report['after']['bones']}
    assert report['before']['morphCount']==report['after']['morphCount'] and report['after']['morphCount']>0
    # A second attempt must refuse before mutation.
    repeat=lib.convert_genesis9_body_report(mesh,local,unreal.load_asset('/Game/DazToUnreal/Male/Male'))
    report['repeat']=str(repeat)
    assert 'Already converted' in repeat,repeat
    assert report['after']==json.loads(lib.inspect_body(mesh,local))
    assert unreal.EditorAssetLibrary.save_loaded_asset(local)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh.get_editor_property('physics_asset'))
    report['status']='PASS'
except Exception:
    report['status']='FAIL'
    report['error']=traceback.format_exc()
finally:
    report['elapsed_seconds']=time.monotonic()-report.pop('started')
    out.write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k not in ('before','after')}))
if report['status']!='PASS':
    raise RuntimeError(report['error'])
