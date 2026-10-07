"""Read-only loaded editor evidence for the ACFU time integration."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Analysis/ACFUTime20261007'
out.mkdir(parents=True, exist_ok=True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
data = {'project': unreal.Paths.project_dir(), 'world': world.get_path_name(),
        'dirty_content': [x.get_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
        'dirty_maps': [x.get_name() for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
        'day_actors': [x.get_path_name() for x in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.DaySequenceActor)]}
bp = unreal.load_object(None, '/AscentCombatFramework/Blueprints/Actors/ACF_TimeManager_BP.ACF_TimeManager_BP')
data['acf_time_is_day_sequence'] = isinstance(unreal.get_default_object(bp.generated_class()), unreal.DaySequenceActor)
defaults = unreal.get_default_object(bp.generated_class())
data.update(day_length_hours=defaults.get_day_length(), time_per_cycle_hours=defaults.get_time_per_cycle(),
            initial_time_hours=defaults.get_initial_time_of_day(),
            day_sequence_collections=str(defaults.get_editor_property('day_sequence_collections')))
(out / 'live_before.json').write_text(json.dumps(data, indent=2), encoding='utf8')
print(json.dumps(data))
