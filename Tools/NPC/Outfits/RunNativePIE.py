"""Native fixture host: fresh receipt, 120 s internal/150 s external, no asset saves."""
import argparse
import hashlib
import json
import runpy
import time
from pathlib import Path
import unreal

parser=argparse.ArgumentParser()
parser.add_argument('--count',type=int,default=1)
parser.add_argument('--max-active',type=int,choices=[4,8,16],default=4)
parser.add_argument('--body',default='Male.01')
parser.add_argument('--archetype',default='SkillGuy')
parser.add_argument('--dressed',type=int,choices=[0,1],default=1)
parser.add_argument('--seed',type=int,default=731)
parser.add_argument('--capture',type=float,default=30)
parser.add_argument('--functional',type=int,choices=[0,1],default=0)
parser.add_argument('--loading',choices=['cold','warm'],default='warm')
parser.add_argument('--timeout',type=float,default=120)
parser.add_argument('--name',default='native_'+str(time.time_ns()))
parser.add_argument('--immutable-buffers',type=int,choices=[0,1],default=1)
parser.add_argument('--prepared-binding',type=int,choices=[0,1],default=1)
parser.add_argument('--inspect-lod',action='store_true',help='One runtime LOD snapshot; diagnostic only')
parser.add_argument('--lod-transitions',action='store_true',help='Bounded forced LOD 0/1/0/automatic fit checks and images; diagnostic only')
parser.add_argument('--lod-quality',action='store_true',help='One NPC: extreme morphs, LOD comparison, camera distance and movement; diagnostic only')
parser.add_argument('--lod-quality-case',choices=['all','distance','movement'],default='all',help='Bounded directed retry of a quality subcase')
parser.add_argument('--independent-npc-lod',type=int,choices=[0,1],default=0)
parser.add_argument('--parameter-cache',type=int,choices=[0,1],default=1)
parser.add_argument('--gc',action='store_true',help='Force native GC after fixture destruction')
parser.add_argument('--trace',action='store_true',help='Diagnostic CPU/GPU trace after fixture outfits are ready')
parser.add_argument('--profile-gpu',action='store_true',help='One detailed GPU frame after readiness; diagnostic only')
parser.add_argument('--prewarm',action='store_true',help='Prepare the same dressed group in this PIE world before measuring')
parser.add_argument('--selection',action='store_true',help='Run bounded weighted/instance/saved-precedence QA with transient definitions')
parser.add_argument('--profile-install',action='store_true',help='Diagnostic install-stage logging; do not use as a steady-state acceptance capture')
args=parser.parse_args(globals().get('NPC_OUTFIT_ARGS',[]))
assert not args.selection or not (args.prewarm or args.trace), 'Selection QA is a separate functional fixture'
assert not args.profile_gpu or (not args.selection and not args.trace), 'Use one GPU diagnostic at a time'
assert not args.lod_transitions or (args.dressed == 1 and args.independent_npc_lod == 1 and args.capture >= 20 and not args.selection), 'LOD transitions require dressed independent LOD and >=20 seconds capture'
assert not args.lod_quality or (args.count == 1 and args.dressed == 1 and args.independent_npc_lod == 1 and args.capture >= (45 if args.lod_quality_case=='all' else 15) and not any([args.lod_transitions,args.selection,args.trace,args.profile_gpu,args.functional,args.prewarm])), 'LOD quality requires one dressed NPC, independent LOD, sufficient capture and no other diagnostic/functional fixture'
assert args.lod_quality or args.lod_quality_case=='all', 'A quality subcase requires --lod-quality'
assert args.name.replace('_','').replace('-','').isalnum()
root=Path(unreal.Paths.project_dir()).resolve()
configuration_paths = [
    'Tools/NPC/Outfits/RunNativePIE.py',
    'Tools/NPC/Outfits/LodQuality.py',
    'Content/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.uasset',
    'Content/DazToUnreal/RagShirt/RagShirt.uasset',
    'Content/DazToUnreal/RagPants/RagPants.uasset',
    'Content/DazToUnreal/RagFootWraps/RagFootWraps.uasset',
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Private/EFClothingMorphV3RuntimeComponent.cpp',
    'Content/_Game/Characters/Data/DA_NPCRoster.uasset',
    'Plugins/EFClothingMorph/Content/Deformers/DG_EFGarmentSurfaceConstraint.uasset',
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Private/EFClothingSurfaceDeformerProducer.cpp',
    'Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Private/EFClothingImmutableGraphProvider.cpp',
    'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Characters/ProjectNPCOutfitSubsystem.cpp',
    'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Characters/ProjectNPCOutfitComponent.cpp',
    'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Characters/ProjectNPCOutfitQA.cpp',
]
# Capture before PIE/preparation, never hash files during the measurement window.
configuration_hashes = {name: hashlib.sha256((root/name).read_bytes()).hexdigest()
                        for name in configuration_paths}
out=root/'Saved/NPCOutfits/Native'/args.name
assert not (out/'runtime.json').exists(), 'Use a fresh name'
warm_receipt=root/'Saved/NPCOutfits/Native'/(args.name+'_warmup')/'runtime.json'
if args.prewarm: assert not warm_receipt.exists(), 'Use a fresh warmup name'
out.mkdir(parents=True,exist_ok=True)
receipt=out/'runtime.json'
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.is_in_play_in_editor()
started=time.monotonic()
state={'issued':False,'warm_ready':not args.prewarm}
lod_quality=runpy.run_path(str(root/'Tools/NPC/Outfits/LodQuality.py'))['LodQuality'](args.body,out,args.lod_quality_case) if args.lod_quality else None
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
buffer_cvar='ef.ClothingMorph.ImmutableBindingBuffers'
previous_buffers=unreal.SystemLibrary.get_console_variable_int_value(buffer_cvar)
prepared_cvar='ef.ClothingMorph.PreparedBinding'
previous_prepared=unreal.SystemLibrary.get_console_variable_int_value(prepared_cvar)
lod_cvar='ef.ClothingMorph.IndependentNPCLod'
previous_lod=unreal.SystemLibrary.get_console_variable_int_value(lod_cvar)
lod_bias_cvar='r.SkeletalMeshLODBias'
previous_lod_bias=unreal.SystemLibrary.get_console_variable_int_value(lod_bias_cvar)
parameter_cvar='ef.ClothingMorph.SkipUnchangedParameters'
previous_parameters=unreal.SystemLibrary.get_console_variable_int_value(parameter_cvar)
profile_cvar='ef.ClothingMorph.ProfileInstall'
previous_profile=unreal.SystemLibrary.get_console_variable_int_value(profile_cvar)
admission_cvar='Project.NPCOutfits.MaxActiveInitializations'
previous_admission=unreal.SystemLibrary.get_console_variable_int_value(admission_cvar)
gpu_settings={'r.ProfileGPU.ShowUI':0,'r.ProfileGPU.ShowLeafEvents':1,'r.ProfileGPU.UnicodeOutput':0,
              'r.ProfileGPU.TableFormatting':0,'r.ProfileGPU.ThresholdPercent':0}
previous_gpu={key:unreal.SystemLibrary.get_console_variable_float_value(key) for key in gpu_settings} if args.profile_gpu else {}
quality_names=['r.SkeletalMeshLODBias','r.SkeletalMeshLODRadiusScale','Project.NPCOutfits.UseResidentPrerequisites','sg.ViewDistanceQuality','sg.AntiAliasingQuality','sg.ShadowQuality','sg.GlobalIlluminationQuality',
               'sg.ReflectionQuality','sg.PostProcessQuality','sg.TextureQuality','sg.EffectsQuality',
               'sg.FoliageQuality','sg.ShadingQuality','r.ScreenPercentage','r.VSync','t.MaxFPS']
quality={name:unreal.SystemLibrary.get_console_variable_float_value(name) for name in quality_names}
native_args={k:v for k,v in vars(args).items() if k not in ['max_active','immutable_buffers','prepared_binding','parameter_cache','independent_npc_lod','inspect_lod','lod_transitions','lod_quality','lod_quality_case','trace','profile_gpu','prewarm','selection','profile_install','gc']}
command=('Project.NPCOutfits.SelectionQA ' if args.selection else 'Project.NPCOutfits.QA ')+' '.join(k+'='+str(v) for k,v in native_args.items())
assert not (args.selection and args.gc), 'GC fixture uses the standard outfit command'
if args.gc: command+=' gc'
warm_args=dict(native_args,name=args.name+'_warmup',dressed=1,capture=1,functional=0,loading='preparation')
warm_command='Project.NPCOutfits.QA '+' '.join(k+'='+str(v) for k,v in warm_args.items())

def done(error=''):
    if lod_quality and not error and not lod_quality.complete:
        error='LOD quality sequence did not complete before native fixture teardown'
    if lod_quality:
        (out/'lod-quality.json').write_text(json.dumps({'status':'FAIL' if error else 'PASS','reason':error,'visual_status':'PENDING','records':lod_quality.records,'last_observation':lod_quality.last_observation,'diagnostic_lod_bias':0,'previous_lod_bias':previous_lod_bias,'case':args.lod_quality_case,'screenshot_mode':'viewport'},indent=2),encoding='utf-8')
    if args.lod_transitions and not error and len(state.get('lod_transitions', [])) != 4:
        error='LOD transition sequence did not complete before native fixture teardown'
    unreal.unregister_slate_post_tick_callback(handle)
    restore_world=worlds.get_game_world()
    if state.get('trace_started'):
        unreal.SystemLibrary.execute_console_command(restore_world,'Trace.Stop')
    perf.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
    unreal.SystemLibrary.execute_console_command(restore_world,buffer_cvar+' '+str(previous_buffers))
    unreal.SystemLibrary.execute_console_command(restore_world,prepared_cvar+' '+str(previous_prepared))
    unreal.SystemLibrary.execute_console_command(restore_world,lod_cvar+' '+str(previous_lod))
    if lod_quality: unreal.SystemLibrary.execute_console_command(restore_world,lod_bias_cvar+' '+str(previous_lod_bias))
    unreal.SystemLibrary.execute_console_command(restore_world,parameter_cvar+' '+str(previous_parameters))
    unreal.SystemLibrary.execute_console_command(restore_world,profile_cvar+' '+str(previous_profile))
    unreal.SystemLibrary.execute_console_command(restore_world,admission_cvar+' '+str(previous_admission))
    for key,value in previous_gpu.items(): unreal.SystemLibrary.execute_console_command(restore_world,key+' '+str(value))
    unreal.EditorLevelLibrary.editor_end_play()
    (out/'host.json').write_text(json.dumps({'status':'FAIL' if error else 'PASS','reason':error,'seconds':time.monotonic()-started,'command':command,'immutable_buffers':args.immutable_buffers,'prepared_binding':args.prepared_binding,'parameter_cache':args.parameter_cache,'independent_npc_lod':args.independent_npc_lod,'inspect_lod':args.inspect_lod,'lod_snapshot':state.get('lod_snapshot'),'lod_quality_requested':args.lod_quality,'lod_transitions_requested':args.lod_transitions,'lod_transitions':state.get('lod_transitions',[]),'profile_install':args.profile_install,'profile_gpu':args.profile_gpu,'trace':args.trace,'quality':quality,'configuration_hashes':configuration_hashes,'prewarm_receipt':str(warm_receipt) if args.prewarm else None},indent=2),encoding='utf-8')

def check_lod_transitions(world):
    records=state.setdefault('lod_transitions', [])
    if len(records)==4: return
    pawns=[p for p in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Pawn)
           if any(str(t).startswith('EF.AppearanceId.OutfitQA.') for t in p.tags)]
    if len(pawns)!=args.count or not all(p.get_component_by_class(unreal.ProjectNPCOutfitComponent).state==unreal.ProjectNPCOutfitState.READY for p in pawns): return
    meshes=[m for p in pawns for m in p.get_components_by_class(unreal.SkeletalMeshComponent)
            if m.get_editor_property('skeletal_mesh_asset') and m.get_editor_property('skeletal_mesh_asset').get_name() in ['RagShirt','RagPants','RagFootWraps']]
    assert len(meshes)==args.count*3, 'Expected three live garments per fixture pawn'
    now=time.monotonic()
    forced=[1,2,1,0][len(records)]
    if 'lod_change_at' not in state:
        for mesh in meshes: mesh.set_forced_lod(forced)
        state['lod_change_at']=now
        return
    assert now-state['lod_change_at']<8, 'LOD transition did not settle within eight seconds'
    summaries=[p.get_component_by_class(unreal.EFClothingMorphV3RuntimeComponent).get_debug_summary() for p in pawns]
    lods=[m.get_predicted_lod_level() for m in meshes]
    ready=all('managed=3 ready=3 warming=0 passthrough=0 issues=0' in s for s in summaries)
    ready=ready and all(m.is_visible() for m in meshes) and (forced==0 or all(l==forced-1 for l in lods))
    ready=ready and all(f'{m.get_name()}:Ready[gLOD={m.get_predicted_lod_level()},' in summaries[i//3]
                        for i,m in enumerate(meshes))
    if now-state['lod_change_at']<.5 or not ready: return
    path=out/f'lod_transition_{len(records)}.png'
    if not state.get('lod_image_requested'):
        state['lod_image_requested']=now
        unreal.AutomationLibrary.take_high_res_screenshot(1400,1000,str(path))
        return
    if not path.exists() or path.stat().st_size==0 or now-state['lod_image_requested']<.25: return
    records.append({'forced_lod':forced,'actual_lods':lods,'seconds':now-state['lod_change_at'],
                    'summaries':summaries,'image':str(path)})
    state.pop('lod_change_at')
    state.pop('lod_image_requested')

def tick(dt):
    if state.get('in_tick'): return
    state['in_tick']=True
    try:
        if time.monotonic()-started>150: done('150-second host ceiling'); return
        world=worlds.get_game_world()
        if world and unreal.GameplayStatics.get_player_pawn(world,0) and not state['issued']:
            state['issued']=True
            unreal.SystemLibrary.execute_console_command(world,buffer_cvar+' '+str(args.immutable_buffers))
            unreal.SystemLibrary.execute_console_command(world,prepared_cvar+' '+str(args.prepared_binding))
            unreal.SystemLibrary.execute_console_command(world,lod_cvar+' '+str(args.independent_npc_lod))
            # A positive global bias intentionally keeps a two-LOD garment at
            # LOD1 even near the camera. Isolate distance selection in this
            # visual-only fixture, then restore the exact user's setting.
            if lod_quality: unreal.SystemLibrary.execute_console_command(world,lod_bias_cvar+' 0')
            unreal.SystemLibrary.execute_console_command(world,parameter_cvar+' '+str(args.parameter_cache))
            unreal.SystemLibrary.execute_console_command(world,profile_cvar+' '+str(int(args.profile_install)))
            unreal.SystemLibrary.execute_console_command(world,admission_cvar+' '+str(args.max_active))
            unreal.SystemLibrary.execute_console_command(world,warm_command if args.prewarm else command)
        if args.prewarm and not state['warm_ready']:
            if warm_receipt.exists():
                warm=json.loads(warm_receipt.read_text(encoding='utf-8-sig'))
                if warm['status']!='PASS': done('Prewarm failed: '+warm.get('reason','')); return
                if 'warm_done_at' not in state: state['warm_done_at']=time.monotonic(); return
                if time.monotonic()-state['warm_done_at']<.1: return
                state['warm_ready']=True
                unreal.SystemLibrary.execute_console_command(world,command)
            return
        if state['issued'] and receipt.exists():
            result=json.loads(receipt.read_text(encoding='utf-8-sig'))
            done('' if result['status']=='PASS' else result.get('reason','Native FAIL'))
            return
        if args.lod_transitions and world and state['issued']:
            check_lod_transitions(world)
        if lod_quality and world and state['issued']:
            lod_quality.tick(world)
        if args.inspect_lod and world and state['issued'] and not state.get('lod_snapshot'):
            pawns=[p for p in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Pawn)
                   if any(str(t).startswith('EF.AppearanceId.OutfitQA.') for t in p.tags)]
            if len(pawns)==args.count and all(p.get_component_by_class(unreal.ProjectNPCOutfitComponent).state==unreal.ProjectNPCOutfitState.READY for p in pawns):
                if 'lod_ready_at' not in state: state['lod_ready_at']=time.monotonic()
                if time.monotonic()-state['lod_ready_at']>=1:
                    state['lod_snapshot']=[]
                    for pawn in pawns:
                        for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
                            asset=mesh.get_editor_property('skeletal_mesh_asset')
                            if asset and asset.get_name() in ['RagShirt','RagPants','RagFootWraps']:
                                state['lod_snapshot'].append({'actor':pawn.get_name(),'mesh':asset.get_path_name(),
                                    'lod':mesh.get_predicted_lod_level(),'visible':mesh.is_visible(),
                                    'ignore_leader_lod':mesh.get_editor_property('bIgnoreLeaderPoseComponentLOD'),
                                    'sync_parent_lod':mesh.get_editor_property('bSyncAttachParentLOD')})
        if (args.trace or args.profile_gpu) and world and state['issued'] and not state.get('diagnostic_started'):
            pawns=[p for p in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Pawn)
                   if any(str(t).startswith('EF.AppearanceId.OutfitQA.') for t in p.tags)]
            if len(pawns)==args.count and all(p.get_component_by_class(unreal.ProjectNPCOutfitComponent).state==unreal.ProjectNPCOutfitState.READY for p in pawns):
                state['diagnostic_started']=True
                if args.trace:
                    state['trace_started']=True
                    unreal.SystemLibrary.execute_console_command(world,'Trace.File "'+str(out/'capture.utrace').replace('\\','/')+'" cpu,gpu,frame,bookmark')
                else:
                    for key,value in gpu_settings.items(): unreal.SystemLibrary.execute_console_command(world,key+' '+str(value))
                    unreal.SystemLibrary.execute_console_command(world,'ProfileGPU')
    except Exception as error: done(str(error)); raise
    finally: state['in_tick']=False
handle=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('Native outfit PIE scheduled: '+str(out))
