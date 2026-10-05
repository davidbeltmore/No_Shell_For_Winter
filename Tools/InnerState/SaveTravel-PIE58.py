"""Focused ALS disk save/load + actual non-dungeon travel. Requires external SaveGuard."""
import unreal
import time
import json
import traceback
from pathlib import Path

out=Path(unreal.Paths.project_saved_dir()).resolve()/'Migration/InnerState20261005/save_travel.json'
ed=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
st={'started':time.monotonic(),'phase':'ready','since':time.monotonic(),'checks':[]}
delegates=[]
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
def sub(world,name,module='EFProjectSystemsGameplay',game_instance=False):
    prefix=unreal.GameplayStatics.get_game_instance(world).get_path_name() if game_instance else world.get_path_name()
    return next((x for x in unreal.ObjectIterator(unreal.load_class(None,'/Script/'+module+'.'+name)) if x.get_path_name().startswith(prefix)),None)
def check(name,condition):
    st['checks'].append({'name':name,'passed':bool(condition)})
    if not condition: raise RuntimeError(name)
def phase(name): st.update(phase=name,since=time.monotonic())
def finish(error=''):
    st['error']=error; st['wall_seconds']=time.monotonic()-st['started']; st['passed']=not error
    out.write_text(json.dumps(st,indent=2),encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    perf.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
    if ed.is_in_play_in_editor(): unreal.EditorLevelLibrary.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()
def saved(ok): st['saved']=bool(ok)
def loaded(ok): st['loaded']=bool(ok)
def tick(delta):
    try:
        if time.monotonic()-st['started']>110: raise RuntimeError('110 second watchdog: '+st['phase'])
        world=worlds.get_game_world()
        pawn=unreal.GameplayStatics.get_player_pawn(world,0) if world else None
        if not pawn: return
        inner=pawn.get_component_by_class(unreal.ProjectInnerStateComponent)
        if not inner: return
        age=time.monotonic()-st['since']
        clock=sub(world,'ProjectDayCycleSubsystem')
        save=sub(world,'ALSLoadAndSaveSubsystem','AscentSaveSystem',True)
        if st['phase']=='ready' and age>6:
            check('HUB real player ready', 'HUB' in world.get_path_name())
            for key,value in {'Stress':63.,'Stimulation':-24.,'Hygiene':37.,'Bathroom':50.,'Sickness':32.,'Awareness':88.,'Sensation':-27.}.items(): inner.set_debug_value(key,value)
            exposure=unreal.GuidLibrary.new_guid()
            inner.add_poison(9.,unreal.ProjectPoisonSpeed.SLOW,'QA.Travel',exposure)
            clock.set_pause_reason('InnerStateSaveQA',True)
            st['snapshot']={k:inner.get_value(k) for k in ['Stress','Stimulation','Hygiene','Bathroom','Sickness','Awareness','Sensation','Poison']}
            st['saved_calendar']=inner.get_editor_property('State').get_editor_property('CalendarHours')
            callback=unreal.OnSaveFinished(); callback.bind_callable(saved); delegates.append(callback)
            save.save_game_world('InnerState_QA_World',callback,True,False,'Isolated Inner State QA; restored by SaveGuard')
            phase('saving')
        elif st['phase']=='saving' and 'saved' in st:
            check('ALS disk save completed',st['saved'])
            clock.set_pause_reason('InnerStateSaveQA',False)
            unreal.GameplayStatics.open_level(world,'/Game/_Game/Locations/StorySelection')
            phase('travel')
        elif st['phase']=='travel' and age>6 and 'StorySelection' in world.get_path_name():
            st['travel_values']={k:inner.get_value(k) for k in st['snapshot']}
            check('actual map travel preserves slow and signed needs', all(abs(st['travel_values'][k]-st['snapshot'][k])<3 for k in ['Stress','Stimulation','Hygiene','Bathroom','Sickness','Sensation']))
            check('actual map travel preserves poison exposure',0<inner.get_value('Poison')<=9)
            callback=unreal.OnLoadFinished(); callback.bind_callable(loaded); delegates.append(callback)
            save.load_game_world('InnerState_QA_World',callback,True)
            phase('loading')
        elif st['phase']=='loading' and 'loaded' in st and 'HUB' in world.get_path_name():
            check('ALS disk load completed',st['loaded'])
            phase('settle')
        elif st['phase']=='settle' and age>2:
            st['loaded_values']={k:inner.get_value(k) for k in st['snapshot']}
            st['loaded_calendar']=inner.get_editor_property('State').get_editor_property('CalendarHours')
            hours=st['loaded_calendar']-st['saved_calendar']
            # The world resumes after ALS completion; allow only the physically
            # possible evolution during that measured simulation interval.
            bounds={'Stress':35.,'Stimulation':3.,'Hygiene':2.5,'Bathroom':100/48,'Sickness':6.,'Sensation':10.,'Awareness':4*75}
            check('ALS restores saved clocks',0<=hours<.25)
            check('ALS restores physiology with bounded subsequent simulation',all(abs(st['loaded_values'][k]-st['snapshot'][k])<=rate*hours+.1 for k,rate in bounds.items()))
            check('ALS preserves exact pending slow poison debt',inner.get_value('Poison')==st['snapshot']['Poison'])
            finish()
    except Exception:
        finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
if not ed.is_in_play_in_editor(): ed.editor_request_begin_play()
print('Save/travel test scheduled; target-only, 110 second watchdog.')
