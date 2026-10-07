"""Compile dependent Blueprints in memory and load the time fixture in a fresh process."""
import json
import time
import traceback
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Analysis/TimeControl'
legacy = '-TimeLegacySave' in unreal.SystemLibrary.get_command_line()
expected = json.loads((out / ('Visual_20260928_022101/receipt.json' if legacy else 'Visual/receipt.json')).read_text())['saved_hours']
state = {'started': time.monotonic(), 'phase': 'boot', 'expected_hours': expected, 'legacy_save': legacy, 'blueprints': []}
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
handle = None

def finish(error=''):
    state['error'] = error
    (out / 'reload.json').write_text(json.dumps(state, indent=2))
    state.update(phase='shutdown', since=time.monotonic())
    if editor.is_in_play_in_editor():
        unreal.EditorLevelLibrary.editor_end_play()

def save_system(world):
    cls = unreal.load_class(None, '/Script/AscentSaveSystem.ALSLoadAndSaveSubsystem')
    gi = unreal.GameplayStatics.get_game_instance(world).get_path_name()
    return next(x for x in unreal.ObjectIterator(cls) if x.get_path_name().startswith(gi))

def component(world, name):
    cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.' + name)
    return unreal.GameplayStatics.get_player_pawn(world, 0).get_component_by_class(cls)

def status_remaining(status):
    return next((s.remaining_duration_seconds for s in status.build_active_status_snapshots() if str(s.status_name) == 'GraceStep'), 0.0)

def tick(delta):
    try:
        if state['phase'] == 'shutdown':
            if time.monotonic() - state['since'] > 3:
                unreal.unregister_slate_post_tick_callback(handle)
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
                unreal.SystemLibrary.quit_editor()
            return
        if time.monotonic() - state['started'] > 300:
            raise RuntimeError('Reload timeout')
        if state['phase'] == 'boot':
            paths = ['/Game/FullSample/Player', '/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar',
                     '/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget'] if '-TimeCompileBlueprints' in unreal.SystemLibrary.get_command_line() else []
            for path in paths:
                bp = unreal.load_asset(path)
                unreal.BlueprintEditorLibrary.compile_blueprint(bp)
                status = str(bp.get_editor_property('status'))
                state['blueprints'].append({'path': path, 'status': status})
                if 'UP_TO_DATE' not in status:
                    raise RuntimeError('Blueprint compile failed: ' + path + ' ' + status)
            editor.load_level('/Game/_Game/Hub/HUB')
            editor.editor_request_begin_play()
            state.update(phase='ready', since=time.monotonic())
            return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world or not unreal.GameplayStatics.get_player_pawn(world, 0):
            return
        cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectDayCycleSubsystem')
        clock = next(x for x in unreal.ObjectIterator(cls) if x.get_path_name().startswith(world.get_path_name()))
        if state['phase'] == 'ready' and time.monotonic() - state['since'] > 10:
            clock.toggle_player_pause()
            unreal.GameplayStatics.set_game_paused(world, False)
            state.update(phase='pause_ownership', since=time.monotonic())
        elif state['phase'] == 'pause_ownership' and time.monotonic() - state['since'] > .3:
            state['retains_pause_after_external_release'] = unreal.GameplayStatics.is_game_paused(world)
            if not state['retains_pause_after_external_release']:
                raise RuntimeError('Legacy UI released player-owned pause')
            clock.toggle_player_pause()
            # Poll the native state; no Python delegate proxy needs to survive
            # travel, GC or engine shutdown in this fresh-process scenario.
            save_system(world).load_game_world('TimeControl_QA_World', unreal.OnLoadFinished(), True)
            state['phase'] = 'loading'
        elif state['phase'] == 'loading' and 'IDLE' in str(save_system(world).get_system_state()).upper():
            state['restored_hours'] = clock.get_calendar_hours()
            native = clock.get_day_cycle_state_actor()
            state['native_hours'] = native.get_time_of_day()
            if abs(state['native_hours'] - state['restored_hours'] % 24) > 1/3600:
                raise RuntimeError('Native ACF clock differs from restored calendar')
            state['speed'] = unreal.GameplayStatics.get_global_time_dilation(world)
            if abs(state['restored_hours'] - expected) > .15 or state['speed'] != 1:
                raise RuntimeError('Fresh-process calendar restoration failed')
            state['loaded'] = True
            state.update(phase='activity_start', since=time.monotonic())
        elif state['phase'] == 'activity_start' and time.monotonic() - state['since'] > 2:
            needs = component(world, 'ProjectSurvivalNeedsComponent')
            for name in ['Hunger', 'Thirst', 'Sleep']:
                needs.set_need_current_value(name, 90, True)
            training = component(world, 'ProjectPracticeComponent')
            if not training.configure_book('ACFTime_QA_Book', 0.4):
                raise RuntimeError('Cannot configure isolated timed book')
            emote_class = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectEmoteSubsystem')
            emotes = next(x for x in unreal.ObjectIterator(emote_class) if x.get_path_name().startswith(world.get_path_name()))
            if not emotes.request_start_interaction_by_id('Actions.Training.ReadBook'):
                raise RuntimeError('Real reading interaction rejected')
            state.update(phase='activity_ready', since=time.monotonic())
        elif state['phase'] == 'activity_ready' and time.monotonic() - state['since'] > 1:
            training = component(world, 'ProjectPracticeComponent')
            status = component(world, 'ProjectSurvivalStatusComponent')
            if not training.is_training_active() or not status.apply_status('GraceStep', 30, None):
                raise RuntimeError('Training/status fixture not active')
            if not clock.request_time_speed(4):
                raise RuntimeError('Training fast-forward rejected')
            state.update(phase='activity_fast', since=time.monotonic(), activity_clock=clock.get_calendar_hours(),
                         activity_hours=training.get_session_hours(), status_remaining=status_remaining(status))
        elif state['phase'] == 'activity_fast' and time.monotonic() - state['since'] > 2:
            training = component(world, 'ProjectPracticeComponent')
            status = component(world, 'ProjectSurvivalStatusComponent')
            elapsed = clock.get_calendar_hours() - state['activity_clock']
            state['training_clock_error_hours'] = abs(training.get_session_hours() - state['activity_hours'] - elapsed)
            state['status_clock_error_seconds'] = abs(state['status_remaining'] - status_remaining(status) - elapsed*75)
            if state['training_clock_error_hours'] > .004 or state['status_clock_error_seconds'] > .3:
                raise RuntimeError('Activity/status duration differs from native calendar')
            clock.toggle_player_pause()
            state.update(phase='activity_paused', since=time.monotonic(), frozen_training=training.get_session_hours(),
                         frozen_status=status_remaining(status), frozen_calendar=clock.get_calendar_hours())
        elif state['phase'] == 'activity_paused' and time.monotonic() - state['since'] > 2:
            training = component(world, 'ProjectPracticeComponent')
            status = component(world, 'ProjectSurvivalStatusComponent')
            if (training.get_session_hours(), status_remaining(status), clock.get_calendar_hours()) != (state['frozen_training'], state['frozen_status'], state['frozen_calendar']):
                raise RuntimeError('Paused activity, status or native clock advanced')
            state['activity_status_pause'] = True
            clock.toggle_player_pause()
            if not clock.request_time_speed(4):
                raise RuntimeError('Activity resume speed rejected')
            state.update(phase='activity_finish', since=time.monotonic())
        elif state['phase'] == 'activity_finish' and (clock.get_calendar_hours() - state['activity_clock']) * 75 > 32:
            training = component(world, 'ProjectPracticeComponent')
            status = component(world, 'ProjectSurvivalStatusComponent')
            state['book_completed_hours'] = training.get_book_hours('ACFTime_QA_Book')
            state['status_expired'] = not status.is_status_active('GraceStep')
            if training.is_training_active() or state['book_completed_hours'] < .4 - 1e-6 or not state['status_expired']:
                raise RuntimeError('Timed activity/status did not finish on shared simulation')
            clock.request_time_speed(1)
            finish()
    except Exception:
        finish(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
