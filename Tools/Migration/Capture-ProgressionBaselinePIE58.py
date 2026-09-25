"""Visible baseline only: no purchases, save calls, or asset writes."""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = (Path(unreal.Paths.project_saved_dir()) / 'Analysis/ProgressionPilot/Visual').resolve()
OUT.mkdir(parents=True, exist_ok=True)
initial_screenshots = set(OUT.glob('*.png'))
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
state = {'phase': 'load', 'started': time.monotonic(), 'since': time.monotonic()}
handle = None

def subsystem(world, name):
    cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.' + name)
    for obj in unreal.ObjectIterator(cls):
        if obj.get_path_name().startswith(world.get_path_name()):
            return obj
    return None

def phase(name):
    state.update(phase=name, since=time.monotonic())

def finish(error=''):
    state['error'] = error
    state['screenshots'] = [str(x) for x in OUT.glob('*.png') if x not in initial_screenshots]
    (OUT / 'receipt.json').write_text(json.dumps(state, indent=2), encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(handle)
    if editor.is_in_play_in_editor():
        unreal.EditorLevelLibrary.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()

def shot(world, name):
    filename = str(OUT / (name + '.png')).replace('\\', '/')
    unreal.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename="' + filename + '"')

def tick(delta):
    try:
        if time.monotonic() - state['started'] > 180:
            finish('Timeout in ' + state['phase'])
            return
        age = time.monotonic() - state['since']
        if state['phase'] == 'load':
            editor.load_level('/Game/_Game/Hub/HUB')
            editor.editor_request_begin_play()
            phase('pie')
            return
        world = unreal.EditorLevelLibrary.get_game_world()
        if not world:
            return
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        if state['phase'] == 'pie' and pawn and age > 8:
            hud = subsystem(world, 'ProjectSurvivalNeedsSubsystem')
            if not hud:
                raise RuntimeError('Needs subsystem unavailable')
            state['pawn'] = pawn.get_path_name()
            state['hud_visible'] = hud.set_needs_hud_visible(True)
            phase('hud')
        elif state['phase'] == 'hud' and age > 3:
            shot(world, 'needs_doctrine_hud')
            phase('hud_capture')
        elif state['phase'] == 'hud_capture' and age > 3:
            menu = subsystem(world, 'ProjectInnerDoctrineMenuSubsystem')
            state['menu_open'] = menu.open_menu_for_actor(pawn)
            if not state['menu_open']:
                raise RuntimeError('Doctrine menu did not open')
            phase('menu')
        elif state['phase'] == 'menu' and age > 3:
            shot(world, 'doctrine_menu')
            phase('menu_capture')
        elif state['phase'] == 'menu_capture' and age > 3:
            subsystem(world, 'ProjectInnerDoctrineMenuSubsystem').close_menu()
            finish()
    except Exception:
        finish(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
