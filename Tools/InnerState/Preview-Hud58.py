import unreal
import time
import json
from pathlib import Path

out = Path(unreal.Paths.project_saved_dir()).resolve() / 'Migration/InnerState20261005'
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert 'HUB' in unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
started = time.monotonic()
captured = False
def preview_tick(delta):
    global captured
    age = time.monotonic() - started
    if age > 25:
        unreal.unregister_slate_post_tick_callback(preview_handle)
        unreal.EditorLevelLibrary.editor_end_play()
        return
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world,0) if world else None
    if not pawn or age < 12 or captured: return
    inner = pawn.get_component_by_class(unreal.ProjectInnerStateComponent)
    cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectSurvivalNeedsSubsystem')
    hud = next((s for s in unreal.ObjectIterator(cls) if s.get_path_name().startswith(world.get_path_name())), None)
    if not inner or not hud: return
    hud.set_needs_hud_visible(True)
    inner.set_debug_value('Sensation', -35.0)
    inner.set_debug_value('Stimulation', 25.0)
    unreal.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename="' + str(out / 'hud_preview.png').replace('\\','/') + '"')
    (out/'preview_runtime.json').write_text(json.dumps({'pawn':pawn.get_path_name(), 'inner':inner.get_path_name(), 'values':{k:inner.get_value(k) for k in ['Stress','Stimulation','Hygiene','Bathroom','Air','Composure','Awareness','Sensation']}}), encoding='utf-8')
    captured = True
preview_handle = unreal.register_slate_post_tick_callback(preview_tick)
editor.editor_request_begin_play()
print('HUB preview scheduled; auto-stop 25s.')
