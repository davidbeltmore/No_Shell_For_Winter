"""Native fixture host: fresh receipt, 120 s internal/150 s external, no asset saves."""
import argparse
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
parser.add_argument('--trace',action='store_true',help='Diagnostic CPU/GPU trace after fixture outfits are ready')
parser.add_argument('--prewarm',action='store_true',help='Prepare the same dressed group in this PIE world before measuring')
args=parser.parse_args(globals().get('NPC_OUTFIT_ARGS',[]))
assert args.name.replace('_','').replace('-','').isalnum()
root=Path(unreal.Paths.project_dir()).resolve()
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
quality_names=['sg.ViewDistanceQuality','sg.AntiAliasingQuality','sg.ShadowQuality','sg.GlobalIlluminationQuality',
               'sg.ReflectionQuality','sg.PostProcessQuality','sg.TextureQuality','sg.EffectsQuality',
               'sg.FoliageQuality','sg.ShadingQuality','r.ScreenPercentage','r.VSync','t.MaxFPS']
quality={name:unreal.SystemLibrary.get_console_variable_float_value(name) for name in quality_names}
native_args={k:v for k,v in vars(args).items() if k not in ['immutable_buffers','trace','prewarm']}
command='Project.NPCOutfits.QA '+' '.join(k+'='+str(v) for k,v in native_args.items())
warm_args=dict(native_args,name=args.name+'_warmup',dressed=1,capture=1,functional=0,loading='preparation')
warm_command='Project.NPCOutfits.QA '+' '.join(k+'='+str(v) for k,v in warm_args.items())

def done(error=''):
    unreal.unregister_slate_post_tick_callback(handle)
    if state.get('trace_started'):
        unreal.SystemLibrary.execute_console_command(worlds.get_game_world(),'Trace.Stop')
    unreal.EditorLevelLibrary.editor_end_play()
    perf.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
    unreal.SystemLibrary.execute_console_command(worlds.get_editor_world(),buffer_cvar+' '+str(previous_buffers))
    (out/'host.json').write_text(json.dumps({'status':'FAIL' if error else 'PASS','reason':error,'seconds':time.monotonic()-started,'command':command,'immutable_buffers':args.immutable_buffers,'quality':quality,'prewarm_receipt':str(warm_receipt) if args.prewarm else None},indent=2),encoding='utf-8')

def tick(dt):
    try:
        if time.monotonic()-started>150: done('150-second host ceiling'); return
        world=worlds.get_game_world()
        if world and unreal.GameplayStatics.get_player_pawn(world,0) and not state['issued']:
            state['issued']=True
            unreal.SystemLibrary.execute_console_command(world,buffer_cvar+' '+str(args.immutable_buffers))
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
        if args.trace and world and state['issued'] and not state.get('trace_started'):
            pawns=[p for p in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Pawn)
                   if any(str(t).startswith('EF.AppearanceId.OutfitQA.') for t in p.tags)]
            if len(pawns)==args.count and all(p.get_component_by_class(unreal.ProjectNPCOutfitComponent).state==unreal.ProjectNPCOutfitState.READY for p in pawns):
                state['trace_started']=True
                unreal.SystemLibrary.execute_console_command(world,'Trace.File "'+str(out/'capture.utrace').replace('\\','/')+'" cpu,gpu,frame,bookmark')
    except Exception as error: done(str(error)); raise
handle=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('Native outfit PIE scheduled: '+str(out))
