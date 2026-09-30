"""Bounded, SaveGuard-owned editor session for task-specific diagnostic requests.
Only request files created after this process starts are consumed. No assets saved.
"""
import contextlib, gc, io, json, time, traceback
from pathlib import Path
import unreal
OUT=Path(unreal.Paths.project_saved_dir()).resolve()/'Analysis/CompatibilityClosure'
OUT.mkdir(parents=True,exist_ok=True)
started=time.time()
state={'busy':False,'quit_at':None}
scope={'unreal':unreal,'OUT':OUT,'__name__':'compatibility_inspection'}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

def tick(delta):
    if state['busy']: return
    state['busy']=True
    try:
        if state['quit_at']:
            if time.time()-state['quit_at']>8:
                unreal.unregister_slate_post_tick_callback(handle)
                # Release diagnostic UObject wrappers while Python and UE are still live.
                # Requests may retain objects from a package retired through the editor.
                scope.clear()
                gc.collect()
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
            return
        if time.time()-started>1800 or (OUT/'close.request').exists() and (OUT/'close.request').stat().st_mtime>started:
            if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(): unreal.EditorLevelLibrary.editor_end_play()
            state['quit_at']=time.time()
            return
        for request in sorted(OUT.glob('request_*.py')):
            receipt=request.with_suffix('.json')
            if request.stat().st_mtime<started or receipt.exists(): continue
            buffer=io.StringIO()
            result={'request':request.name,'started':time.time()}
            try:
                with contextlib.redirect_stdout(buffer): exec(compile(request.read_text(encoding='utf8'),str(request),'exec'),scope)
                result['status']='PASS'
            except Exception:
                result['status']='FAIL';result['error']=traceback.format_exc()
            result['output']=buffer.getvalue();result['finished']=time.time()
            receipt.write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf8')
            break
    finally: state['busy']=False

handle=unreal.register_slate_post_tick_callback(tick)
(OUT/'host.json').write_text(json.dumps({'started':started,'project':unreal.Paths.project_dir()}),encoding='utf8')
