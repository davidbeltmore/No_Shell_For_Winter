"""Real interaction/pursuit gate: native 150 seconds, host 180 seconds."""
import json,time,unreal
from pathlib import Path
logic_out=Path(unreal.Paths.project_dir())/'Saved/NPCLogic'
logic_receipt=logic_out/'Runtime/runtime.txt'
logic_start=time.monotonic();logic_mtime=logic_receipt.stat().st_mtime_ns if logic_receipt.exists() else 0
logic_editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
logic_worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not logic_editor.is_in_play_in_editor()
logic_perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
logic_throttle=logic_perf.get_editor_property('bThrottleCPUWhenNotForeground');logic_perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
logic_state={'issued':False}
logic_command=globals().get('NPC_LOGIC_COMMAND','Project.NPCLogic.QA')
def logic_done(error=''):
    unreal.unregister_slate_post_tick_callback(logic_handle)
    unreal.EditorLevelLibrary.editor_end_play()
    logic_perf.set_editor_property('bThrottleCPUWhenNotForeground',logic_throttle)
    (logic_out/'pie-host.json').write_text(json.dumps({'status':'FAIL' if error else 'PASS','error':error,'seconds':time.monotonic()-logic_start},indent=2))
def logic_tick(dt):
    try:
        if time.monotonic()-logic_start>180: logic_done('180-second host deadline');return
        world=logic_worlds.get_game_world()
        if world and unreal.GameplayStatics.get_player_pawn(world,0) and not logic_state['issued']:
            unreal.SystemLibrary.execute_console_command(world,logic_command);logic_state['issued']=True
        if logic_state['issued'] and logic_receipt.exists() and logic_receipt.stat().st_mtime_ns!=logic_mtime:
            result=logic_receipt.read_text(encoding='utf-8-sig');logic_done('' if 'status=PASS' in result else result)
    except Exception as exc: logic_done(str(exc));raise
logic_handle=unreal.register_slate_post_tick_callback(logic_tick)
logic_editor.editor_request_begin_play()
print('Real NPC logic PIE scheduled: native 150s / host 180s ceilings.')
