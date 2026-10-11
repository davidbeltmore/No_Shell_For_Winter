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
args=parser.parse_args(globals().get('NPC_OUTFIT_ARGS',[]))
assert args.name.replace('_','').replace('-','').isalnum()
root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/NPCOutfits/Native'/args.name
assert not (out/'runtime.json').exists(), 'Use a fresh name'
out.mkdir(parents=True,exist_ok=True)
receipt=out/'runtime.json'
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.is_in_play_in_editor()
started=time.monotonic()
state={'issued':False}
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
command='Project.NPCOutfits.QA '+' '.join(k+'='+str(v) for k,v in vars(args).items())

def done(error=''):
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorLevelLibrary.editor_end_play()
    perf.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
    (out/'host.json').write_text(json.dumps({'status':'FAIL' if error else 'PASS','reason':error,'seconds':time.monotonic()-started,'command':command},indent=2),encoding='utf-8')

def tick(dt):
    try:
        if time.monotonic()-started>150: done('150-second host ceiling'); return
        world=worlds.get_game_world()
        if world and unreal.GameplayStatics.get_player_pawn(world,0) and not state['issued']:
            state['issued']=True
            unreal.SystemLibrary.execute_console_command(world,command)
        if state['issued'] and receipt.exists():
            result=json.loads(receipt.read_text(encoding='utf-8-sig'))
            done('' if result['status']=='PASS' else result.get('reason','Native FAIL'))
    except Exception as error: done(str(error)); raise
handle=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('Native outfit PIE scheduled: '+str(out))
