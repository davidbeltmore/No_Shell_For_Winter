"""Editor-only CC inspection driver. Commands/evidence stay under Saved/."""
import builtins
import json
import time
import traceback
from pathlib import Path
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

OUT = Path(unreal.Paths.project_dir()) / 'Saved/Migration/CharacterCreationRework'
OUT.mkdir(parents=True, exist_ok=True)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

def world():
    return unreal.EditorLevelLibrary.get_game_world()

def subsystem(class_name):
    cls = unreal.load_class(None, class_name)
    w = world()
    gi = unreal.GameplayStatics.get_game_instance(w) if w else None
    for obj in unreal.ObjectIterator(cls):
        if gi and obj.get_outer() == gi:
            return obj
    raise RuntimeError('PIE subsystem missing: ' + class_name)

def creator():
    return subsystem('/Script/EFCharacterCreationRuntime.EFCharacterCreationSubsystem')

def root():
    return creator().get_active_root_widget_for_automation()

def controls():
    prefix = root().get_path_name()
    return [o for o in unreal.ObjectIterator(unreal.Widget) if o.get_path_name().startswith(prefix + ':') or o.get_path_name().startswith(prefix + '.')]

def inspect():
    result = {'root': root().get_path_name(), 'controls': [], 'morphs': []}
    for o in controls():
        r = {'name': o.get_name(), 'class': o.get_class().get_name(), 'visibility': str(o.get_visibility())}
        try:
            g = o.get_cached_geometry()
            r['size'] = str(unreal.SlateBlueprintLibrary.get_local_size(g))
            r['position'] = str(unreal.SlateBlueprintLibrary.local_to_absolute(g, unreal.Vector2D()))
        except Exception:
            pass
        if isinstance(o, unreal.TextBlock):
            r['text'] = str(o.get_text())
        if isinstance(o, unreal.Border):
            r['color'] = str(o.get_editor_property('BrushColor'))
        result['controls'].append(r)
    for entry in root().get_displayed_morph_entries():
        result['morphs'].append({'name': str(entry.morph_name), 'label': entry.display_name, 'section': entry.section, 'category': str(entry.category)})
    return result

def run(command):
    action = command['action']
    if action == 'start':
        settings = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
        settings.set_editor_property('NewWindowWidth', command.get('width', 1920))
        settings.set_editor_property('NewWindowHeight', command.get('height', 1080))
        settings.set_editor_property('LastExecutedPlayModeType', unreal.PlayModeType.PLAY_MODE_IN_EDITOR_FLOATING)
        LEVEL.editor_play_simulate()
        return {'requested': True}
    if action == 'open':
        return {'opened': creator().open_character_creation_for_automation()}
    if action == 'category':
        root().select_presentation_category(command['value'])
    elif action == 'section':
        root().select_presentation_section(command['value'])
    elif action == 'theme':
        theme = subsystem('/Script/EFProjectSystemsUI.EFProjectDynamicThemeSubsystem')
        theme.set_theme_preset(getattr(unreal.EFProjectHUDThemePreset, command['value'].upper()))
    elif action == 'inspect':
        return inspect()
    elif action == 'exec':
        # Reviewed project-local snippets, retained verbatim alongside their output.
        scope = {'unreal': unreal, 'root': root, 'creator': creator, 'controls': controls, 'world': world, 'subsystem': subsystem, 'OUT': OUT, 'result': None}
        exec(command['code'], scope)
        return scope['result']
    elif action == 'console':
        unreal.SystemLibrary.execute_console_command(world(), command['value'])
    elif action == 'stop':
        LEVEL.editor_request_end_play()
    elif action == 'finish':
        unreal.unregister_slate_post_tick_callback(builtins._efcc_rework_tick)
    else:
        if action not in ('category', 'section', 'theme'):
            raise RuntimeError('Unknown action ' + action)
    return {'done': action}

def tick(delta):
    request = OUT / 'request.json'
    if not request.exists():
        return
    try:
        command = json.loads(request.read_text(encoding='utf-8-sig'))
        request.unlink()
        response = {'id': command['id'], 'result': run(command)}
    except Exception:
        response = {'id': command.get('id', 'unknown'), 'error': traceback.format_exc()}
    (OUT / ('response_' + str(response['id']) + '.json')).write_text(json.dumps(response, indent=2, default=str), encoding='utf-8')

if hasattr(builtins, '_efcc_rework_tick'):
    unreal.unregister_slate_post_tick_callback(builtins._efcc_rework_tick)
builtins._efcc_rework_tick = unreal.register_slate_post_tick_callback(tick)
(OUT / 'driver_ready.json').write_text(json.dumps({'project': unreal.Paths.project_dir(), 'time': time.time()}))
unreal.log('EFCC rework validation driver ready')
