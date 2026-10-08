"""Reusable imported-body onboarding, run inside the target UE editor.

Read JSON from EF_BODY_ONBOARD_REQUEST or Saved/BodyPresetsQA/onboard-request.json.
Default mode is inspect. prepare converts/binds and registers a candidate; full
runtime, clothing, cook and visual gates are still required before certification.
"""
import hashlib
import json
import math
import os
import shutil
import time
import traceback
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
assert root==Path('D:/Projects UE5/NoShellForWinter')
request=Path(os.environ.get('EF_BODY_ONBOARD_REQUEST',str(root/'Saved/BodyPresetsQA/onboard-request.json')))
config=json.loads(request.read_text(encoding='utf-8-sig'))
mode=config.get('mode','inspect')
assert mode in ('inspect','prepare')
mesh_path=config['mesh'].split('.')[0]
folder=mesh_path.rsplit('/',1)[0]
assert folder.startswith('/Game/DazToUnreal/')
assert folder not in ['/Game/DazToUnreal/'+n for n in ('Male','Female','Multiple')]
out=root/'Saved/BodyPresetsQA'/('Onboard_'+time.strftime('%Y%m%d_%H%M%S')+'_'+str(time.time_ns()%1000000000))
out.mkdir(parents=True)
report={'status':'PENDING','request':config,'started':time.monotonic(),'backups':[]}
def backup(asset):
    package=asset.get_path_name().split('.')[0]
    assert package.startswith('/Game/'),package
    filename=root/'Content'/(package[6:]+'.uasset')
    if filename.exists():
        destination=out/'Backup'/(package[6:]+'.uasset')
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(filename,destination)
        report['backups'].append({'package':package,'sha256':hashlib.sha256(filename.read_bytes()).hexdigest(),'file':str(destination)})
try:
    mesh=unreal.load_asset(mesh_path)
    assert isinstance(mesh,unreal.SkeletalMesh)
    skeleton_path=config.get('skeleton',mesh_path+'_Skeleton')
    local=unreal.load_asset(skeleton_path)
    assert isinstance(local,unreal.Skeleton) and local.get_path_name().rsplit('/',1)[0]==folder
    reference=unreal.load_asset('/Game/DazToUnreal/Male/Male')
    lib=unreal.EFBodyOnboardingLibrary
    report['before']=json.loads(lib.inspect_body(mesh,local))
    names={b['name'] for b in report['before']['bones']}
    report['route']='bind_converted' if 'spine_01' in names else 'convert_genesis9'
    report['morphs']=sorted(str(n) for n in mesh.get_all_morph_target_names())
    report['materials']=[{'slot':str(m.material_slot_name),'material':m.material_interface.get_path_name() if m.material_interface else None} for m in mesh.materials]
    report['physics']=str(mesh.get_editor_property('physics_asset'))
    report['deformer']=str(mesh.get_default_mesh_deformer())
    report['skeleton_referencers']=list(unreal.EditorAssetLibrary.find_package_referencers_for_asset(skeleton_path,True))
    if mode=='prepare':
        assert config['gender'] in ('Male','Female')
        preset_id=config['preset_id']
        assert preset_id and config['label']
        catalog=unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets')
        director=unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
        assert catalog and director
        rows=list(catalog.bodies)
        row=next((r for r in rows if str(r.preset_id)==preset_id),None)
        assert not row or row.mesh==mesh,'Stable preset ID already belongs to another mesh'
        assert not row or row.gender==getattr(unreal.CharacterCreationGender,config['gender'].upper()),'Stable preset ID already belongs to another gender'
        assert not any(r.mesh==mesh and str(r.preset_id)!=preset_id for r in rows),'Mesh already registered under a different ID'
        materials=list(mesh.materials)
        for slot,path in config.get('material_overrides',{}).items():
            material=unreal.load_asset(path)
            assert isinstance(material,unreal.MaterialInterface),path
            target_slot=next((m for m in materials if str(m.material_slot_name)==slot),None)
            assert target_slot is not None,slot
            target_slot.material_interface=material
        assert all(m.material_interface for m in materials),'Resolve missing material slots with explicit material_overrides before preparation'
        original_physics=mesh.get_editor_property('physics_asset')
        assert original_physics
        for asset in (mesh,local,original_physics,catalog,director): backup(asset)
        if report['route']=='convert_genesis9':
            result=lib.convert_genesis9_body_report(mesh,local,reference)
            assert result.startswith('PASS:'),result
        else:
            result=lib.bind_local_skeleton(mesh,local,reference)
            assert result is not None and not str(result),str(result)
        after=json.loads(lib.inspect_body(mesh,local))
        assert after['assignedCompatible'] and after['assignedSkeleton']==local.get_path_name()
        assert report['morphs']==sorted(str(n) for n in mesh.get_all_morph_target_names())
        report['after']=after
        if row is None:
            row=unreal.EFBodyPresetDefinition()
            row.preset_id=preset_id
            row.display_name=config['label']
            row.gender=getattr(unreal.CharacterCreationGender,config['gender'].upper())
            row.mesh=mesh
            rows.append(row)
        row.display_name=config['label']
        row.allow_npc=bool(config.get('allow_npc',True))
        row.npc_weight=float(config.get('npc_weight',1.0))
        assert math.isfinite(row.npc_weight) and row.npc_weight>=0
        profiles=[]
        for p in config.get('morph_profile',[]):
            assert p['name'] in report['morphs'],'Unknown morph: '+p['name']
            entry=unreal.EFBodyMorphProfileEntry()
            entry.morph_name=p['name']
            entry.minimum=float(p.get('minimum',-1))
            entry.maximum=float(p.get('maximum',1))
            entry.default_value=float(p.get('default',0))
            entry.randomize_npc=bool(p.get('randomize_npc',True))
            entry.npc_minimum=float(p.get('npc_minimum',0))
            entry.npc_maximum=float(p.get('npc_maximum',0.3))
            assert entry.minimum<=entry.default_value<=entry.maximum
            assert entry.minimum<=entry.npc_minimum<=entry.npc_maximum<=entry.maximum
            profiles.append(entry)
        if 'morph_profile' in config: row.morph_profile=profiles
        if config.get('author_secondary_physics',False):
            safe_id=''.join(c if c.isalnum() else '_' for c in preset_id)
            physics_path='/Game/_Game/Characters/Physics/PA_'+safe_id+'_Morph'
            physics=row.morph_physics_asset
            if not physics: physics=unreal.load_asset(physics_path) if unreal.EditorAssetLibrary.does_asset_exist(physics_path) else None
            assert not physics or not any(r.morph_physics_asset==physics and str(r.preset_id)!=preset_id for r in rows),'Physics profile belongs to another preset'
            if physics: backup(physics)
            else: physics=unreal.EditorAssetLibrary.duplicate_asset(original_physics.get_path_name(),physics_path)
            assert physics
            result=unreal.ProjectCompatibilityInspectionLibrary.author_morph_physics_candidate(mesh,physics)
            assert not result.startswith('ERROR'),result
            assert unreal.EditorAssetLibrary.save_loaded_asset(physics)
            row.morph_physics_asset=physics
            report['physics']=result
        catalog.bodies=rows
        mesh.set_editor_property('materials',materials)
        source=mesh.get_default_mesh_deformer()
        if not source:
            source=unreal.load_asset('/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth')
            mesh.set_editor_property('default_mesh_deformer',source)
        bodies=list(director.bodies)
        target=next((b for b in bodies if b.body_surface==mesh),None)
        if target is None:
            target=unreal.EFClothingBodyTarget()
            target.set_editor_property('body_surface',mesh)
            target.set_editor_property('material_slot_aliases',config.get('material_slot_aliases',{}))
            branches=config.get('genital_bone_branches',[])
            assert all(b in {v['name'] for v in after['bones']} for b in branches)
            assert not branches or source.get_path_name()=='/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth.DG_DualQuatSkin_Morph_Cloth','Custom deformer requires a validated coverage adapter'
            target.set_editor_property('excluded_fit_bone_branches',branches)
            target.set_editor_property('genital_bone_branches',branches)
            target.set_editor_property('bone_coverage_deformer',unreal.load_asset('/EFClothingMorph/Deformers/DG_EFBodyCoverageDQS'))
            target.set_editor_property('bone_coverage_deformer_source',source)
            bodies.append(target)
        director.set_editor_property('bodies',bodies)
        for asset in (local,mesh,original_physics,catalog,director):
            assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
        if config.get('compile_clothing',True):
            options=unreal.EFClothingNativeSourceCompileOptions()
            options.set_editor_property('output_root','/EFClothingMorph/_Internal/Compiled/V4')
            options.set_editor_property('maximum_push_cm',2.5)
            options.set_editor_property('only_stale',True)
            options.set_editor_property('strict_catalog_certification',True)
            result=unreal.EFClothingFitCompilerLibrary.compile_native_source_catalog_v4(director,unreal.load_asset('/Game/DazToUnreal/Multiple/Multiple'),options)
            report['clothing']=str(result.report)
            assert result.success,report['clothing']
            report['runtime_sync']=unreal.EFClothingFitCompilerLibrary.sync_unisex_runtime_catalog(director)
            assert report['runtime_sync'].startswith('PASS:'),report['runtime_sync']
        report['status']='PREPARED; runtime, visual, cook and packaged certification gates PENDING'
    else:
        report['status']='INSPECTED; no assets modified'
except Exception:
    report['status']='FAIL; inspect backups and dirty packages before retrying'
    report['error']=traceback.format_exc()
finally:
    report['elapsed_seconds']=time.monotonic()-report.pop('started')
    (out/'report.json').write_text(json.dumps(report,indent=2))
    print(str(out/'report.json'))
if report.get('error'): raise RuntimeError(report['error'])
