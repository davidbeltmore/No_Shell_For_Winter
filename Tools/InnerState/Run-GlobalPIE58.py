"""Schedule bounded real gameplay QA in the existing HUB editor. No assets/saves written."""
import unreal
import time
from pathlib import Path

qa_root = Path(unreal.Paths.project_saved_dir()).resolve() / 'InnerStateQA'
qa_started = time.monotonic()
qa_started_test = False
qa_start_files = set(qa_root.glob('*/report.json'))
qa_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
qa_performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
qa_throttle_before = qa_performance.get_editor_property('bThrottleCPUWhenNotForeground')
qa_performance.set_editor_property('bThrottleCPUWhenNotForeground', False)
qa_worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
qa_world = qa_worlds.get_editor_world() or qa_worlds.get_game_world()
assert qa_world and 'HUB' in qa_world.get_path_name()

def qa_tick(delta):
    global qa_started_test
    age = time.monotonic() - qa_started
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    pawn = unreal.GameplayStatics.get_player_pawn(world,0) if world else None
    if not qa_started_test and pawn and age >= 8:
        unreal.SystemLibrary.execute_console_command(world, 'Project.InnerState.QA')
        qa_started_test = True
    reports = set(qa_root.glob('*/report.json')) - qa_start_files
    if reports or age >= 120:
        unreal.unregister_slate_post_tick_callback(qa_handle)
        unreal.EditorLevelLibrary.editor_end_play()
        qa_performance.set_editor_property('bThrottleCPUWhenNotForeground', qa_throttle_before)
        print('Global Inner State QA: ' + (str(next(iter(reports))) if reports else 'TIMEOUT at 120s'))

qa_handle = unreal.register_slate_post_tick_callback(qa_tick)
if not qa_editor.is_in_play_in_editor(): qa_editor.editor_request_begin_play()
print('Global HUB gameplay QA scheduled. Hard wall limit: 120 seconds.')
