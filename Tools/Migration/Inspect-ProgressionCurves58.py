"""Read-only audit: do not save or alter native effects."""
import json
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Analysis/ProgressionPilot'
out.mkdir(parents=True, exist_ok=True)
report = {}
for name in ['ACF_AttDependencies_GE', 'ACF_StatInit_GE']:
    path = '/AscentCombatFramework/GASRuntime/GameplayEffects/' + name
    cls = unreal.load_class(None, path + '.' + name + '_C')
    if not cls:
        raise RuntimeError('Cannot load ' + path)
    obj = unreal.get_default_object(cls)
    report[path] = {
        'duration_policy': str(obj.get_editor_property('duration_policy')),
        'modifiers': [str(x.export_text()) for x in obj.get_editor_property('modifiers')],
    }
(out / 'native_effects.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('PROGRESSION_CURVE_INSPECTION_COMPLETE')
