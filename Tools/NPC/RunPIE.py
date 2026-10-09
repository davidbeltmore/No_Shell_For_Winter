"""Run the native 90-second smoke test in PIE, keep editor open, 120-second host ceiling."""
import json,time,unreal
from pathlib import Path
out=Path(unreal.Paths.project_dir())/'Saved/NPCDAZ'; receipt=out/'Runtime/runtime.txt'
started=time.monotonic(); old_mtime=receipt.stat().st_mtime_ns if receipt.exists() else 0
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.is_in_play_in_editor()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground'); perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
state={'issued':False}
def done(error=''):
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.EditorLevelLibrary.editor_end_play()
    perf.set_editor_property('bThrottleCPUWhenNotForeground',old_throttle)
    (out/'pie-host.json').write_text(json.dumps({'status':'FAIL' if error else 'PASS','error':error,'seconds':time.monotonic()-started},indent=2))
def tick(dt):
    if time.monotonic()-started>120: done('120-second host watchdog'); return
    world=worlds.get_game_world()
    if world and unreal.GameplayStatics.get_player_pawn(world,0) and not state['issued']:
        unreal.SystemLibrary.execute_console_command(world,'Project.NPCDAZ.QA'); state['issued']=True
    if state['issued'] and receipt.exists() and receipt.stat().st_mtime_ns!=old_mtime:
        text=receipt.read_text(encoding='utf-8-sig'); done('' if 'status=PASS' in text else text)
handle=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('NPC PIE smoke scheduled: 12 representatives, native 90s/host 120s ceilings.')
