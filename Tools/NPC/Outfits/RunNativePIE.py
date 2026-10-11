"""Native fixture host: fresh receipt, 120 s internal/150 s external, no asset saves."""
import argparse
import hashlib
import json
import time
from pathlib import Path
import unreal

parser=argparse.ArgumentParser()
parser.add_argument('--count',type=int,default=1)
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
parser.add_argument('--gc',action='store_true',help='Force native GC after fixture destruction')
parser.add_argument('--trace',action='store_true',help='Diagnostic CPU/GPU trace after fixture outfits are ready')
parser.add_argument('--profile-gpu',action='store_true',help='One detailed GPU frame after readiness; diagnostic only')
parser.add_argument('--prewarm',action='store_true',help='Prepare the same dressed group in this PIE world before measuring')
parser.add_argument('--selection',action='store_true',help='Run bounded weighted/instance/saved-precedence QA with transient definitions')
parser.add_argument('--profile-install',action='store_true',help='Diagnostic install-stage logging; do not use as a steady-state acceptance capture')
args=parser.parse_args(globals().get('NPC_OUTFIT_ARGS',[]))
assert not args.selection or not (args.prewarm or args.trace), 'Selection QA is a separate functional fixture'
assert not args.profile_gpu or (not args.selection and not args.trace), 'Use one GPU diagnostic at a time'
assert args.name.replace('_','').replace('-','').isalnum()
root=Path(unreal.Paths.project_dir()).resolve()
configuration_paths = [
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
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
buffer_cvar='ef.ClothingMorph.ImmutableBindingBuffers'
previous_buffers=unreal.SystemLibrary.get_console_variable_int_value(buffer_cvar)
prepared_cvar='ef.ClothingMorph.PreparedBinding'
previous_prepared=unreal.SystemLibrary.get_console_variable_int_value(prepared_cvar)
profile_cvar='ef.ClothingMorph.ProfileInstall'
previous_profile=unreal.SystemLibrary.get_console_variable_int_value(profile_cvar)
gpu_settings={'r.ProfileGPU.ShowUI':0,'r.ProfileGPU.ShowLeafEvents':1,'r.ProfileGPU.UnicodeOutput':0,
              'r.ProfileGPU.TableFormatting':0,'r.ProfileGPU.ThresholdPercent':0}
previous_gpu={key:unreal.SystemLibrary.get_console_variable_float_value(key) for key in gpu_settings} if args.profile_gpu else {}
quality_names=['sg.ViewDistanceQuality','sg.AntiAliasingQuality','sg.ShadowQuality','sg.GlobalIlluminationQuality',
               'sg.ReflectionQuality','sg.PostProcessQuality','sg.TextureQuality','sg.EffectsQuality',
               'sg.FoliageQuality','sg.ShadingQuality','r.ScreenPercentage','r.VSync','t.MaxFPS']
quality={name:unreal.SystemLibrary.get_console_variable_float_value(name) for name in quality_names}
native_args={k:v for k,v in vars(args).items() if k not in ['immutable_buffers','prepared_binding','trace','profile_gpu','prewarm','selection','profile_install','gc']}
command=('Project.NPCOutfits.SelectionQA ' if args.selection else 'Project.NPCOutfits.QA ')+' '.join(k+'='+str(v) for k,v in native_args.items())
assert not (args.selection and args.gc), 'GC fixture uses the standard outfit command'
if args.gc: command+=' gc'
warm_args=dict(native_args,name=args.name+'_warmup',dressed=1,capture=1,functional=0,loading='preparation')
warm_command='Project.NPCOutfits.QA '+' '.join(k+'='+str(v) for k,v in warm_args.items())

def done(error=''):
    unreal.unregister_slate_post_tick_callback(handle)
    if state.get('trace_started'):
        unreal.SystemLibrary.execute_console_command(worlds.get_game_world(),'Trace.Stop')
    unreal.EditorLevelLibrary.editor_end_play()
    perf.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
    unreal.SystemLibrary.execute_console_command(worlds.get_editor_world(),buffer_cvar+' '+str(previous_buffers))
    unreal.SystemLibrary.execute_console_command(worlds.get_editor_world(),prepared_cvar+' '+str(previous_prepared))
    unreal.SystemLibrary.execute_console_command(worlds.get_editor_world(),profile_cvar+' '+str(previous_profile))
    for key,value in previous_gpu.items(): unreal.SystemLibrary.execute_console_command(worlds.get_editor_world(),key+' '+str(value))
    (out/'host.json').write_text(json.dumps({'status':'FAIL' if error else 'PASS','reason':error,'seconds':time.monotonic()-started,'command':command,'immutable_buffers':args.immutable_buffers,'prepared_binding':args.prepared_binding,'profile_install':args.profile_install,'profile_gpu':args.profile_gpu,'trace':args.trace,'quality':quality,'configuration_hashes':configuration_hashes,'prewarm_receipt':str(warm_receipt) if args.prewarm else None},indent=2),encoding='utf-8')

def tick(dt):
    try:
        if time.monotonic()-started>150: done('150-second host ceiling'); return
        world=worlds.get_game_world()
        if world and unreal.GameplayStatics.get_player_pawn(world,0) and not state['issued']:
            state['issued']=True
            unreal.SystemLibrary.execute_console_command(world,buffer_cvar+' '+str(args.immutable_buffers))
            unreal.SystemLibrary.execute_console_command(world,prepared_cvar+' '+str(args.prepared_binding))
            unreal.SystemLibrary.execute_console_command(world,profile_cvar+' '+str(int(args.profile_install)))
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
handle=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('Native outfit PIE scheduled: '+str(out))
