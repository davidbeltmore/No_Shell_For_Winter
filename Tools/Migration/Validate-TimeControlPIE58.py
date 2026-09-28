"""Real HUB time-control validation; run through Run-TimeControl58.ps1 (SaveGuard)."""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = (Path(unreal.Paths.project_saved_dir()) / 'Analysis/TimeControl/Visual').resolve()
OUT.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state = {'phase': 'load', 'started': time.monotonic(), 'since': time.monotonic(), 'checks': []}
handle = None
callbacks = []  # Keep Python delegate proxies alive across map-travel garbage collection.

def subsystem(world, name, module='EFProjectSystemsGameplay'):
    cls = unreal.load_class(None, '/Script/' + module + '.' + name)
    return next((x for x in unreal.ObjectIterator(cls) if x.get_path_name().startswith(world.get_path_name())), None)

def phase(name):
    state.update(phase=name, since=time.monotonic())

def check(name, condition):
    state['checks'].append({'name': name, 'passed': bool(condition)})
    if not condition:
        raise RuntimeError(name)

def finish(error=''):
    state['error'] = error
    (OUT / 'receipt.json').write_text(json.dumps(state, indent=2), encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    if editor.is_in_play_in_editor():
        unreal.EditorLevelLibrary.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()

def shot(world, name):
    unreal.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename="' + str(OUT / (name + '.png')).replace('\\', '/') + '"')

def save_system(world):
    cls = unreal.load_class(None, '/Script/AscentSaveSystem.ALSLoadAndSaveSubsystem')
    prefix = unreal.GameplayStatics.get_game_instance(world).get_path_name()
    return next(x for x in unreal.ObjectIterator(cls) if x.get_path_name().startswith(prefix))

def saved(success):
    state['save_success'] = bool(success)

def loaded(success):
    state['load_success'] = bool(success)

def tick(delta):
    try:
        if time.monotonic() - state['started'] > 420:
            raise RuntimeError('Timeout in ' + state['phase'])
        age = time.monotonic() - state['since']
        if state['phase'] == 'load':
            editor.load_level('/Game/_Game/Hub/HUB')
            editor.editor_request_begin_play()
            phase('ready')
            return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            if state['phase'] not in ('ready', 'travel', 'loading'):
                raise RuntimeError('PIE was interrupted before validation finished')
            return
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        clock = subsystem(world, 'ProjectDayCycleSubsystem')
        if not pawn or not clock:
            return
        needs_cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectSurvivalNeedsComponent')
        needs = pawn.get_component_by_class(needs_cls)
        if state['phase'] == 'ready' and age > 10:
            check('30 minute day', clock.get_current_snapshot().day_length_seconds == 1800)
            subsystem(world, 'ProjectSurvivalNeedsSubsystem').set_needs_hud_visible(True)
            check('unsupported speed rejected', not clock.request_time_speed(3))
            state['pawn'] = pawn.get_path_name()
            state['speed_index'] = 0
            phase('start_speed')
        elif state['phase'] == 'start_speed':
            speed = [1, 2, 4][state['speed_index']]
            state['eligibility'] = str(clock.can_fast_forward())
            check('request x' + str(speed), clock.request_time_speed(speed))
            state['sample_start'] = [clock.get_calendar_hours(), needs.get_need_current_value('Hunger'), time.monotonic()]
            phase('sample')
        elif state['phase'] == 'sample' and age > 4:
            speed = [1, 2, 4][state['speed_index']]
            h0, n0, t0 = state['sample_start']
            hours = clock.get_calendar_hours() - h0
            consumed = n0 - needs.get_need_current_value('Hunger')
            state.setdefault('samples', []).append({'speed': speed, 'hours': hours, 'hunger_used': consumed, 'real_seconds': time.monotonic()-t0})
            check('calendar advanced x' + str(speed), hours > 0)
            check('native dilation x' + str(speed), unreal.GameplayStatics.get_global_time_dilation(world) == speed)
            check('hunger follows calendar x' + str(speed), abs(consumed - hours * 4) < 0.25)
            shot(world, 'time_x' + str(speed))
            state['speed_index'] += 1
            phase('start_speed' if state['speed_index'] < 3 else 'pause_start')
        elif state['phase'] == 'pause_start':
            check('player pause accepted', clock.toggle_player_pause())
            state['frozen'] = [clock.get_calendar_hours(), needs.get_need_current_value('Hunger')]
            phase('paused')
        elif state['phase'] == 'paused' and age > 2:
            check('calendar frozen', clock.get_calendar_hours() == state['frozen'][0])
            check('hunger frozen', needs.get_need_current_value('Hunger') == state['frozen'][1])
            shot(world, 'time_paused')
            phase('pause_capture')
        elif state['phase'] == 'pause_capture' and age > 1:
            clock.set_pause_reason('TimeQA_Modal', True)
            clock.toggle_player_pause()
            check('modal retains pause', unreal.GameplayStatics.is_game_paused(world))
            clock.set_pause_reason('TimeQA_Modal', False)
            check('last owner resumes', not unreal.GameplayStatics.is_game_paused(world))
            check('resume at x1', unreal.GameplayStatics.get_global_time_dilation(world) == 1)
            state['resume_hours'] = clock.get_calendar_hours()
            phase('resumed')
        elif state['phase'] == 'resumed' and age > 2:
            check('calendar resumes', clock.get_calendar_hours() > state['resume_hours'])
            teams = subsystem(world, 'ACFTeamManagerSubsystem', 'AscentTeams')
            candidates = [x for x in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
                          if x != pawn and 'Dummy' in x.get_name() and teams.are_actors_hostile(x, pawn)]
            check('real hostile training dummy available', bool(candidates))
            state['threat_actor'] = candidates[0].get_path_name()
            pos = candidates[0].get_actor_location()
            state['threat_location'] = [pos.x, pos.y, pos.z]
            check('accelerate before threat enters radius', clock.request_time_speed(4))
            candidates[0].set_actor_location(pawn.get_actor_location() + unreal.Vector(1500, 0, 0), False, True)
            phase('threat')
        elif state['phase'] == 'threat' and age > 1:
            check('nearby hostile cancels acceleration', unreal.GameplayStatics.get_global_time_dilation(world) == 1)
            check('nearby hostile rejects acceleration', not clock.request_time_speed(4))
            enemy = next(x for x in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn) if x.get_path_name() == state['threat_actor'])
            enemy.set_actor_location(unreal.Vector(*state['threat_location']), False, True)
            phase('critical')
        elif state['phase'] == 'critical' and age > 1:
            thirst = needs.get_need_current_value('Thirst')
            needs.set_need_current_value('Thirst', 1.0, True)
            check('critical need rejects acceleration', not clock.request_time_speed(4))
            needs.set_need_current_value('Thirst', thirst, True)
            clock.set_pause_reason('TimeQA_Save', True)
            state['saved_hours'] = clock.get_calendar_hours()
            callback = unreal.OnSaveFinished()
            callback.bind_callable(saved)
            callbacks.append(callback)
            save_system(world).save_game_world('TimeControl_QA_World', callback, True, False, 'Isolated time control validation')
            phase('saving')
        elif state['phase'] == 'saving' and 'save_success' in state:
            check('ALS world saved to disk', state['save_success'])
            clock.set_pause_reason('TimeQA_Save', False)
            state['travel_hours'] = clock.get_calendar_hours()
            unreal.GameplayStatics.open_level(world, '/Game/_Game/Locations/StorySelection')
            phase('travel')
        elif state['phase'] == 'travel' and age > 8 and unreal.GameplayStatics.get_current_level_name(world, True) == 'StorySelection':
            check('calendar survives map travel', clock.get_calendar_hours() >= state['travel_hours'])
            check('travel restores normal speed', unreal.GameplayStatics.get_global_time_dilation(world) == 1)
            state['story_eligibility'] = str(clock.can_fast_forward())
            accepted = clock.request_time_speed(2)
            state['story_reason'] = str(clock.get_current_snapshot().time_control_message)
            unreal.SystemLibrary.execute_console_command(world, 'Project.Time.Report')
            check('StorySelection modal retains pause', unreal.GameplayStatics.is_game_paused(world))
            check('StorySelection rejects acceleration while modal paused', not accepted and 'Resume time' in state['story_reason'])
            shot(world, 'story_modal_paused')
            phase('story_capture')
        elif state['phase'] == 'story_capture' and age > 1:
            clock.request_time_speed(1)
            callback = unreal.OnLoadFinished()
            callback.bind_callable(loaded)
            callbacks.append(callback)
            save_system(world).load_game_world('TimeControl_QA_World', callback, True)
            phase('loading')
        elif state['phase'] == 'loading' and 'load_success' in state:
            check('ALS world loaded', state['load_success'])
            check('load restores saved calendar', abs(clock.get_calendar_hours() - state['saved_hours']) < 0.15)
            check('load restores x1', unreal.GameplayStatics.get_global_time_dilation(world) == 1)
            finish()
    except Exception:
        finish(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
