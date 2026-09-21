"""Read-only scalar settings/edge inventory of native Calysto topology graphs."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
if root != Path('D:/Projects UE5/NoShellForWinter').resolve():
    raise RuntimeError('Wrong project')
rows = []
seen = set()

def props(obj):
    out = {}
    for name in dir(obj):
        if name.startswith('_'):
            continue
        try:
            value = obj.get_editor_property(name)
            out[name] = props(value) if isinstance(value, unreal.PCGAttributePropertySelector) else str(value)
        except Exception:
            pass
    return out

def inspect(graph):
    if not graph or graph.get_path_name() in seen or len(seen) > 40:
        return
    seen.add(graph.get_path_name())
    try:
        nodes = graph.get_editor_property('nodes')
    except Exception:
        return
    for node in nodes:
        settings = node.get_settings()
        row = {'graph': graph.get_path_name(), 'node': node.get_name(), 'title': str(node.node_title),
               'class': settings.get_class().get_path_name(), 'settings': props(settings), 'inputs': []}
        for pin in node.input_pins:
            p = pin.get_editor_property('properties')
            row['inputs'].append({'pin': str(p.get_editor_property('label')),
                'edges': [props(edge) for edge in pin.get_editor_property('edges')]})
        rows.append(row)
        if isinstance(settings, unreal.PCGSubgraphSettings):
            try:
                sub = settings.get_editor_property('subgraph_instance').get_editor_property('graph')
                inspect(sub)
            except Exception as e:
                row['subgraph_error'] = str(e)

inspect(unreal.load_asset('/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape'))
for name in ('PCG_GetRandomStartEnd', 'PCG_RoomPath', 'PCG_GenerateSidePath'):
    inspect(unreal.load_asset('/Game/Calysto/Dungeon/PCG/Function/' + name))
defaults = unreal.get_default_object(unreal.load_class(None, '/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C'))
rows.append({'graph': 'BlueprintDefaults', 'properties': props(defaults)})
out = root / 'Saved/Migration/CalystoDungeonDirectorV7/EnemyTravel_20260913/ShapeSettings.json'
out.write_text(json.dumps(rows, indent=2), encoding='utf-8')
unreal.log('COMPACT_SHAPE_PROBE_DONE=' + str(len(rows)))
