"""Read-only inspection of the existing HUB water, never saves or starts PIE."""
import unreal,json
from pathlib import Path
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world and 'HUB' in world.get_path_name()
rows=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if any(t in (actor.get_name()+' '+actor.get_class().get_path_name()).lower() for t in ['water','mud','sand','snow']):
        p=actor.get_actor_location()
        row={'path':actor.get_path_name(),'class':actor.get_class().get_path_name(),'location':[p.x,p.y,p.z]}
        for prop in ['volume_kind','strength','runtime_update_interval']:
            try: row[prop]=str(actor.get_editor_property(prop))
            except Exception: pass
        rows.append(row)
out=Path(unreal.Paths.project_saved_dir()).resolve()/'Migration/Hygiene20261005'
out.mkdir(parents=True,exist_ok=True)
(out/'water_inspection.json').write_text(json.dumps({'world':world.get_path_name(),'actors':rows},indent=2),encoding='utf-8')
print(json.dumps(rows))
