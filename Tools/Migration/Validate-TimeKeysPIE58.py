"""Bounded real Slate key routing, native ACF pause page and wait UI regression."""
import json
import time
import traceback
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Analysis/TimeKeys20261007'
out.mkdir(parents=True, exist_ok=True)
state = {'started': time.monotonic(), 'phase':'boot', 'checks':[], 'blueprints':[]}
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
handle = None

def check(label, passed):
    state['checks'].append({'label':label, 'passed':bool(passed)})
    if not passed: raise RuntimeError(label)

def key(world, name):
    unreal.SystemLibrary.execute_console_command(world, 'Project.Time.Key '+name)

def active(world, path):
    cls = unreal.load_class(None, path)
    return [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(world, cls, False) if w.is_activated()]

def shot(world, name):
    unreal.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename="'+str(out/(name+'.png')).replace('\\','/')+'"')

PAUSE = '/Game/FullSample/Integrations/SharedAssets/ACF_PauseMenu_WB.ACF_PauseMenu_WB_C'
WAIT = '/Script/EFProjectSystemsGameplay.ProjectWaitWidget'
# Do not dispatch F8 in the editor: Unreal reserves it for ejecting from PIE.
steps = [('Equals',2,False),('Equals',4,False),('Equals',4,False),('Hyphen',2,False),('Hyphen',1,False),('Hyphen',1,True),('Hyphen',1,True),('Equals',1,False)]

def finish(error=''):
    state['error']=error
    (out/'pie.json').write_text(json.dumps(state, indent=2))
    if editor.is_in_play_in_editor(): unreal.EditorLevelLibrary.editor_end_play()
    state.update(phase='shutdown', since=time.monotonic())

def phase(name): state.update(phase=name, since=time.monotonic())

def tick(delta):
    try:
        if state['phase']=='shutdown':
            if time.monotonic()-state['since']>3:
                unreal.unregister_slate_post_tick_callback(handle)
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
            return
        if time.monotonic()-state['started']>240: raise RuntimeError('240 second limit')
        if state['phase']=='boot':
            for path in ['/Game/FullSample/Player','/Game/FullSample/Integrations/SharedAssets/ACF_PauseMenu_WB','/Game/FullSample/Integrations/UIIntegrations/Widgets/Time/ACFTimeTravel_WB']:
                bp=unreal.load_asset(path)
                unreal.BlueprintEditorLibrary.compile_blueprint(bp)
                status=str(bp.get_editor_property('status'))
                state['blueprints'].append({'path':path,'status':status})
                check('Compile '+path,'UP_TO_DATE' in status)
            editor.load_level('/Game/_Game/Hub/HUB')
            editor.editor_request_begin_play()
            phase('ready'); return
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world or not unreal.GameplayStatics.get_player_pawn(world,0): return
        cls=unreal.load_class(None,'/Script/EFProjectSystemsGameplay.ProjectDayCycleSubsystem')
        clock=next(x for x in unreal.ObjectIterator(cls) if x.get_path_name().startswith(world.get_path_name()))
        age=time.monotonic()-state['since']
        if state['phase']=='ready' and age>10:
            unreal.WidgetLibrary.set_input_mode_game_only(unreal.GameplayStatics.get_player_controller(world,0))
            unreal.SystemLibrary.execute_console_command(world,'Project.Time.Hud')
            state['step']=0
            key(world,steps[0][0]); phase('steps')
        elif state['phase']=='steps' and age>.35:
            name,speed,paused=steps[state['step']]
            state.setdefault('step_observations', []).append({'key':name, 'speed':unreal.GameplayStatics.get_global_time_dilation(world), 'paused':unreal.GameplayStatics.is_game_paused(world), 'wait_open':clock.is_wait_menu_open(), 'native_open':clock.is_native_pause_menu_open()})
            check('Key step '+str(state['step'])+' '+name,unreal.GameplayStatics.get_global_time_dilation(world)==speed and unreal.GameplayStatics.is_game_paused(world)==paused)
            state['step']+=1
            if state['step']<len(steps): key(world,steps[state['step']][0]); phase('steps')
            else: key(world,'Hyphen'); key(world,'P'); phase('paused_menu')
        elif state['phase']=='paused_menu' and age>.5:
            check('P opens native ACF menu from timeline pause',len(active(world,PAUSE))==1)
            shot(world,'native_pause')
            key(world,'Equals'); phase('modal_block')
        elif state['phase']=='modal_block' and age>.3:
            check('Equals cannot release native menu pause',unreal.GameplayStatics.is_game_paused(world) and len(active(world,PAUSE))==1)
            key(world,'P'); phase('native_closed')
        elif state['phase']=='native_closed' and age>.3:
            check('P closes native menu and preserves timeline pause',not active(world,PAUSE) and unreal.GameplayStatics.is_game_paused(world))
            key(world,'Equals'); key(world,'Slash'); phase('wait_open')
        elif state['phase']=='wait_open' and age>.5:
            check('Slash opens paused wait selector',len(active(world,WAIT))==1 and unreal.GameplayStatics.is_game_paused(world))
            check('Wait selector shows mouse cursor',unreal.GameplayStatics.get_player_controller(world,0).get_editor_property('show_mouse_cursor'))
            shot(world,'wait_selector')
            phase('wait_confirm')
        elif state['phase']=='wait_confirm' and age>.5:
            state['wait_start']=clock.get_calendar_hours()
            check('Confirm short bounded wait',clock.confirm_wait(.08))
            check('Wait starts at x4',clock.is_waiting() and unreal.GameplayStatics.get_global_time_dilation(world)==4)
            phase('wait_finish')
        elif state['phase']=='wait_finish' and age>3:
            check('Wait completes automatically at x1',not clock.is_waiting() and unreal.GameplayStatics.get_global_time_dilation(world)==1)
            check('Wait simulated elapsed hours',clock.get_calendar_hours()-state['wait_start']>=.08)
            check('Wait retains native calendar agreement',abs(clock.get_day_cycle_state_actor().get_time_of_day()-clock.get_calendar_hours()%24)<1/3600)
            key(world,'Slash'); phase('cancel_open')
        elif state['phase']=='cancel_open' and age>.3:
            check('Confirm long cancel fixture',clock.confirm_wait(24))
            key(world,'Hyphen'); phase('cancel_wait')
        elif state['phase']=='cancel_wait' and age>.3:
            check('Minus cancels wait and steps x4 to x2',not clock.is_waiting() and unreal.GameplayStatics.get_global_time_dilation(world)==2)
            key(world,'P'); phase('normal_pause')
        elif state['phase']=='normal_pause' and age>.3:
            check('P opens exactly one native pause menu from running',len(active(world,PAUSE))==1 and unreal.GameplayStatics.is_game_paused(world))
            key(world,'P'); phase('normal_resume')
        elif state['phase']=='normal_resume' and age>.3:
            check('P resumes native menu at x1',not active(world,PAUSE) and not unreal.GameplayStatics.is_game_paused(world) and unreal.GameplayStatics.get_global_time_dilation(world)==1)
            key(world,'Slash'); phase('close_selector')
        elif state['phase']=='close_selector' and age>.3:
            key(world,'Slash'); phase('closed_selector')
        elif state['phase']=='closed_selector' and age>.3:
            check('Slash closes selector without waiting',not active(world,WAIT) and not clock.is_waiting() and not unreal.GameplayStatics.is_game_paused(world))
            finish()
    except Exception: finish(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle=unreal.register_slate_post_tick_callback(tick)

