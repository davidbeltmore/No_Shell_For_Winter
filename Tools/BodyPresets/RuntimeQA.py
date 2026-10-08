"""Exercise the same development runtime gate used by the packaged executable."""
import json
import runpy
import time
import traceback
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
runpy.run_path(str(root/'Tools/BodyPresets/InspectCandidateDetails.py'))
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
result=root/'Saved/BodyPresetsQA/Runtime/runtime.txt'
previous=result.stat().st_mtime_ns if result.exists() else 0
state={'started':time.monotonic(),'phase':'ready','due':0}
def tick(dt):
    try:
        now=time.monotonic()
        if now-state['started']>125: raise RuntimeError('PIE runtime harness exceeded 125 seconds')
        if state['phase']=='ready':
            world=worlds.get_game_world()
            pawn=unreal.GameplayStatics.get_player_pawn(world,0) if world else None
            if pawn:
                unreal.SystemLibrary.execute_console_command(world,'Project.BodyPresets.QA',unreal.GameplayStatics.get_player_controller(world,0))
                state['phase']='test'
        elif state['phase']=='test' and result.exists() and result.stat().st_mtime_ns!=previous:
            state['receipt']=result.read_text(encoding='utf-8-sig')
            editor.editor_request_end_play()
            state.update(phase='exit',due=now+3)
        elif state['phase']=='exit' and now>=state['due']:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            perf.set_editor_property('bThrottleCPUWhenNotForeground',old_throttle)
            # Release editor-owned wrappers before the engine tears down Python.
            globals()['perf']=None
            globals()['editor']=None
            globals()['worlds']=None
            state.clear()
            import gc
            gc.collect()
            unreal.SystemLibrary.quit_editor()
    except Exception:
        (root/'Saved/BodyPresetsQA/runtime-harness-error.txt').write_text(traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(state['handle'])
        editor.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()
state['handle']=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
