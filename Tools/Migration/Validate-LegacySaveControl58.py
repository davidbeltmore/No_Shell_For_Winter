"""Control: unchanged legacy UI plus native ALS save, with practice disabled."""
from pathlib import Path
import unreal

enabled='-PracticeControlEnabled' in unreal.SystemLibrary.get_command_line()
unreal.get_default_object(unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectPracticeSettings')).set_editor_property('bEnabled', enabled)
source = (Path(unreal.Paths.project_dir()) / 'Tools/Migration/Capture-ProgressionBaselinePIE58.py').read_text(encoding='utf8')
source = source.replace("'Analysis/ProgressionPilot/Visual'", "'Analysis/PracticeProgression/"+('EnabledSaveControl' if enabled else 'LegacySaveControl')+"'")
old = """            subsystem(world, 'ProjectInnerDoctrineMenuSubsystem').close_menu()
            finish()"""
new = """            subsystem(world, 'ProjectInnerDoctrineMenuSubsystem').close_menu()
            save_class=unreal.load_class(None,'/Script/AscentSaveSystem.ALSLoadAndSaveSubsystem')
            gi=unreal.GameplayStatics.get_game_instance(world)
            save=next(x for x in unreal.ObjectIterator(save_class) if x.get_path_name().startswith(gi.get_path_name()))
            callback=unreal.OnSaveFinished()
            callback.bind_callable(lambda ok:state.update(saved=bool(ok)))
            state['callback']=callback
            save.save_game_world('PracticeLegacyControl',callback,True)
            phase('saving')
        elif state['phase']=='saving' and state.get('saved') and age>5:
            state.pop('callback',None)
            finish()"""
assert old in source
exec(compile(source.replace(old, new), 'LegacySaveControl', 'exec'))
