import unreal
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not editor.is_in_play_in_editor(), 'Finish PIE first.'
unreal.SystemLibrary.quit_editor()
