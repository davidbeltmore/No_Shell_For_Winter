"""Cold conversion reload and idempotent production registration; retire only owned QA assets."""
import hashlib
import json
import os
import runpy
import shutil
import time
import traceback
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/BodyPresetsQA'
report={'status':'PENDING','started':time.monotonic()}
try:
    conversion=json.loads((out/'fresh-conversion.json').read_text())
    assert conversion['status']=='PASS'
    mesh=unreal.load_asset(conversion['after']['mesh'])
    local=mesh.get_editor_property('skeleton')
    current=json.loads(unreal.EFBodyOnboardingLibrary.inspect_body(mesh,local))
    assert current==conversion['after'],'Cold reload changed converted geometry/skeleton evidence'
    report['cold_reload']='PASS'
    before_catalog=unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets')
    ids=[str(r.preset_id) for r in before_catalog.bodies]
    request={'mode':'prepare','mesh':'/Game/DazToUnreal/Male2/Male2','gender':'Male','preset_id':'Male.02','label':'Preset 2','npc_weight':1,'compile_clothing':True,'author_secondary_physics':True}
    request_path=out/'onboard-validation-request.json'
    request_path.write_text(json.dumps(request,indent=2))
    previous=os.environ.get('EF_BODY_ONBOARD_REQUEST')
    os.environ['EF_BODY_ONBOARD_REQUEST']=str(request_path)
    report['onboard_reports']=[]
    try:
        for iteration in range(2):
            context=runpy.run_path(str(root/'Tools/BodyPresets/OnboardBody.py'))
            result=context['report']
            assert result['status'].startswith('PREPARED'),result
            report['onboard_reports'].append(str(context['out']/'report.json'))
            assert ids==[str(r.preset_id) for r in before_catalog.bodies]
            del context
    finally:
        if previous is None: os.environ.pop('EF_BODY_ONBOARD_REQUEST',None)
        else: os.environ['EF_BODY_ONBOARD_REQUEST']=previous
    report['idempotent_registration']='PASS; catalog IDs/order unchanged after two runs'
    # Only assets created by FreshImportConversionQA, backed up before editor deletion.
    folder='/Game/DazToUnreal/__BodyPresetQA'
    paths=list(unreal.EditorAssetLibrary.list_assets(folder,True,False))
    allowed={p.split('.')[0] for p in paths}
    assert len(paths)==3 and all(p.startswith(folder+'/G9ConversionFixture') for p in paths),paths
    report['retired_fixture']=[]
    for path in paths:
        refs=list(unreal.EditorAssetLibrary.find_package_referencers_for_asset(path,True))
        assert all(r.split('.')[0] in allowed for r in refs),(path,refs)
        package=path.split('.')[0]
        file=root/'Content'/(package[6:]+'.uasset')
        backup=out/'FreshFixtureBackup'/file.name
        backup.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(file,backup)
        report['retired_fixture'].append({'path':path,'referencers':refs,'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'backup':str(backup)})
    assets=[unreal.load_asset(p) for p in paths]
    assert unreal.EditorAssetLibrary.delete_loaded_assets(assets)
    assert not unreal.EditorAssetLibrary.list_assets(folder,True,False)
    report['status']='PASS'
except Exception:
    report['status']='FAIL'
    report['error']=traceback.format_exc()
finally:
    report['elapsed_seconds']=time.monotonic()-report.pop('started')
    (out/'onboard-workflow.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))
if report['status']!='PASS': raise RuntimeError(report['error'])
