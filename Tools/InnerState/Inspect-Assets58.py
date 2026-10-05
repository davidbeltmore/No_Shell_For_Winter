import unreal
import json
from pathlib import Path

out = Path(unreal.Paths.project_saved_dir()) / 'Migration/InnerState20261005'
report = {}
for path in ['/Game/FullSample/Blueprints/StatusEffects/ACF_PoisonStatus_BP',
             '/AscentCombatFramework/StatusEffects/ACFDamageOverTimeBP']:
    bp = unreal.load_asset(path)
    data = {'class': str(bp.get_class().get_path_name()), 'graphs': []}
    for field in ['FunctionGraphs', 'UbergraphPages']:
        try:
            for graph in bp.get_editor_property(field):
                data['graphs'].append({'name': graph.get_name(), 'nodes':
                    [n.get_class().get_name() for n in graph.get_editor_property('Nodes')]})
        except Exception as error:
            data[field] = str(error)
    report[path] = data
report['world'] = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectInnerStateSettings')
report['settings_class'] = str(cls)
report['inner_state_settings'] = str(unreal.get_default_object(cls).get_editor_property('BathroomDeadlineHours')) if cls else 'CLASS MISSING'
report['inner_python_types'] = [x for x in dir(unreal) if 'InnerState' in x]
(out / 'asset_inspection.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
