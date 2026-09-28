"""Compile dependent Blueprints in memory and load the time fixture in a fresh process."""
import json
import time
import traceback
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Analysis/TimeControl'
expected = json.loads((out / 'Visual/receipt.json').read_text())['saved_hours']
state = {'started': time.monotonic(), 'phase': 'boot', 'expected_hours': expected, 'blueprints': []}
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
            state['speed'] = unreal.GameplayStatics.get_global_time_dilation(world)
            if abs(state['restored_hours'] - expected) > .15 or state['speed'] != 1:
                raise RuntimeError('Fresh-process calendar restoration failed')
            state['loaded'] = True
            finish()
    except Exception:
        finish(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
