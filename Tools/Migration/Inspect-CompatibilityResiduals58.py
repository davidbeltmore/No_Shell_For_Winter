"""Read-only snapshots for remaining animation and PCG serialization warnings."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir()).resolve()/'Analysis/CompatibilityClosure'
report={}
snapshot=unreal.ProjectAnimationDiagnosticsLibrary.snapshot_object_properties
bp=unreal.load_asset('/Game/FullSample/Animations/ACF_Humanoid_ABP')
report['humanoid_blueprint']=json.loads(snapshot(bp,'Parent,Graph'))
report['humanoid_nodes']=[]
for obj in unreal.ObjectIterator(unreal.Object):
    path=unreal.Object.get_path_name(obj)
    if path.startswith('/Game/FullSample/Animations/ACF_Humanoid_ABP.') and 'Slot' in obj.get_class().get_name():
        report['humanoid_nodes'].append(json.loads(snapshot(obj,'')))
graph=unreal.load_asset('/PCGExtendedToolkit/Subgraphs/SpawnPCGGraph/PCGEx_SpawnPCGGraphByAttribute')
report['pcg_graph']=json.loads(snapshot(graph,'Node'))
report['pcg_wait_settings']=[]
for obj in unreal.ObjectIterator(unreal.Object):
    if unreal.Object.get_path_name(obj).startswith('/PCGExtendedToolkit/Subgraphs/SpawnPCGGraph/PCGEx_SpawnPCGGraphByAttribute.') and 'WaitForPCGDataSettings' in obj.get_class().get_name():
        report['pcg_wait_settings'].append(json.loads(snapshot(obj,'Cached,Output,Version')))
(out/'residual_loaded_snapshots.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
print('Residual snapshots',len(report['humanoid_nodes']),len(report['pcg_wait_settings']))
