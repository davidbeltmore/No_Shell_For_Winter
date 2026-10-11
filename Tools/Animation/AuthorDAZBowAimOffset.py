"""Retarget the legacy UE4 bow aim asset and its references to owned UEFN assets."""
import unreal,json,time
from pathlib import Path
start=time.monotonic();out=Path(unreal.Paths.project_dir())/'Saved/BowPose';e=unreal.EditorAssetLibrary
dest='/Game/_Game/Characters/Animation/Weapons/BowAim'
original='/Game/FullSample/GASP/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin'
retpath=dest+'/RTG_BowUE4ToUEFN'
ret=unreal.load_asset(retpath) if e.does_asset_exist(retpath) else e.duplicate_asset(original,retpath)
ctl=unreal.IKRetargeterController.get_controller(ret)
source_rig=unreal.load_asset('/Game/FullSample/GASP/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget')
target_rig=unreal.load_asset('/AscentCombatFramework/Animation/SkeletalMeshes/Rig/IK_UEFN_Mannequin')
ctl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE,source_rig)
ctl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET,target_rig)
for side in [unreal.RetargetSourceOrTarget.SOURCE,unreal.RetargetSourceOrTarget.TARGET]:
 ctl.set_current_retarget_pose('Default Pose',side);ctl.reset_retarget_pose('Default Pose',[],side)
ctl.auto_map_chains(unreal.AutoMapChainType.FUZZY,True)
srcmesh=unreal.load_asset('/Game/FullSample/GASP/UE4_Mannequin/Meshes/SKM_UE4_Mannequin')
dstmesh=unreal.load_asset('/Game/FullSample/GASP/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin')
ret.set_editor_property('SourcePreviewMesh',srcmesh);ret.set_editor_property('TargetPreviewMesh',dstmesh)
assert e.save_loaded_asset(ret,only_if_is_dirty=False)
source=e.find_asset_data('/Game/FullSample/Animations/OriginalAnimations/Bow/BowAim_AO')
suffixes=['C','D','U','L','LD','LU','R','RD','RU']
required=[dest+'/DAZ_BowAim_AO']+[dest+'/DAZ_BowAim_LookAt_'+s for s in suffixes]
assets=[]
existing=[e.does_asset_exist(p) for p in required]
assert not any(existing) or all(existing),'Partial owned aim set: restore missing assets from Git or author an inspected new versioned set; do not overwrite a live reference set.'
if not any(existing):
 assets=unreal.IKRetargetBatchOperation.duplicate_and_retarget([source],srcmesh,dstmesh,ret,prefix='DAZ_',target_path=dest,include_referenced_assets=True,overwrite_existing_files=True)
assert time.monotonic()-start<90,'90-second retarget ceiling'
ao=unreal.load_asset(dest+'/DAZ_BowAim_AO');assert ao
basepath=dest+'/DAZ_BowAim_CenterReference'
# Never edit a reference already consumed by loaded additive sequences: that can
# stall UE 5.8 recompression. New policy versions require a new reference path.
if e.does_asset_exist(basepath):
 base=unreal.load_asset(basepath)
else:
 base=e.duplicate_asset(dest+'/DAZ_BowAim_LookAt_C',basepath)
 kind=type(base.get_editor_property('AdditiveAnimType'))
 base.set_editor_property('AdditiveAnimType',next(getattr(kind,n) for n in ['AAT_NONE','NONE'] if hasattr(kind,n)))
 base.set_editor_property('RefPoseSeq',None)
 assert e.save_loaded_asset(base,only_if_is_dirty=False)
corrections=[]
for suffix in suffixes:
 assert time.monotonic()-start<90,'90-second authoring ceiling'
 seq=unreal.load_asset(dest+'/DAZ_BowAim_LookAt_'+suffix);seq.set_editor_property('RefPoseSeq',base)
 correction=json.loads(unreal.ProjectAnimationDiagnosticsLibrary.stabilize_owned_bow_aim_component_rotations(seq,base))
 assert correction['success'],correction
 assert correction['maximum_component_rotation_error_degrees']<.01,correction
 corrections.append({'asset':seq.get_path_name(),**correction})
 assert e.save_loaded_asset(seq,only_if_is_dirty=False)
samples=[]
for sample,suffix in zip(ao.get_editor_property('SampleData'),['C','D','L','LD','LU','R','RD','RU','U']):
 sample.set_editor_property('Animation',unreal.load_asset(dest+'/DAZ_BowAim_LookAt_'+suffix));samples.append(sample)
assert len(samples)==9
ao.set_editor_property('SampleData',samples)
assert all(s.get_editor_property('Animation') and s.get_editor_property('Animation').get_editor_property('Skeleton')==ao.get_editor_property('Skeleton') for s in ao.get_editor_property('SampleData'))
assert e.save_loaded_asset(ao,only_if_is_dirty=False)
cdo=unreal.get_default_object(unreal.load_asset('/Game/_Game/Characters/Animation/Weapons/ABP_DAZBowOverlay').generated_class());cdo.set_editor_property('AimOffset',ao)
cdo.set_editor_property('AimOffsetAlpha',1.0)
for field,suffix in [('Aim_Center','C'),('Aim_Down','D'),('Aim_Up','U'),('Aim_Left','L'),('Aim_Right','R')]:
 seq=unreal.load_asset(dest+'/DAZ_BowAim_LookAt_'+suffix);assert seq
 cdo.set_editor_property(field,seq)
bp=unreal.load_asset('/Game/_Game/Characters/Animation/Weapons/ABP_DAZBowOverlay');unreal.BlueprintEditorLibrary.compile_blueprint(bp)
compile_status=str(bp.get_editor_property('Status'));assert 'UP_TO_DATE' in compile_status,compile_status
assert e.save_loaded_asset(bp,only_if_is_dirty=False)
report={'status':'PASS','seconds':time.monotonic()-start,'retargeter':ret.get_path_name(),'aim_offset':ao.get_path_name(),'assets':[str(a.package_name) for a in assets],'skeleton':str(ao.get_editor_property('Skeleton').get_path_name()),'corrections':corrections,'compile_status':compile_status,'sample_count':len(samples),'reference':base.get_path_name()}
(out/'aim-author.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
