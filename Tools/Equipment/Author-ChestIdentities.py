"""Persist only the new chest identities in HUB; prove all existing placements/stock retained."""
import json
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_dir())/'Saved/EquipmentQA/ProceduralChests'
world=unreal.EditorLoadingAndSavingUtils.load_map('/Game/_Game/Hub/HUB')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def retained():
    result={}
    for actor in actors.get_all_level_actors():
        row={'label':actor.get_actor_label(),'transform':str(actor.get_actor_transform()),'tags':str(actor.tags)}
        storage=actor.get_component_by_class(unreal.ProjectChestInventoryComponent)
        if storage: row['stock']=str(storage.get_editor_property('items'));row['legacy_mode']=storage.get_editor_property('procedural_mode')
        lock=actor.get_component_by_class(unreal.ProjectLockpickableComponent)
        if lock: row['starts_locked']=lock.get_editor_property('starts_locked');row['difficulty']=lock.get_editor_property('difficulty')
        result[actor.get_name()]=row
    return result
before=retained();identities=[]
for actor in actors.get_all_level_actors():
    if not actor.get_component_by_class(unreal.ProjectChestInventoryComponent): continue
    cfg=actor.get_component_by_class(unreal.ProjectProceduralChestComponent)
    assert cfg,actor.get_path_name()
    identity=actor.get_editor_property('actor_guid')
    cfg.set_editor_property('stable_identity',identity)
    # Reacquire after property notifications can reconstruct components.
    cfg=actor.get_component_by_class(unreal.ProjectProceduralChestComponent)
    identities.append({'actor':actor.get_path_name(),'identity':cfg.get_editor_property('stable_identity').to_string(),
                       'mode':cfg.get_editor_property('procedural_mode')})
assert len({row['identity'] for row in identities})==len(identities)
assert before==retained(),'Existing placements, stock or lock settings changed'
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/_Game/Hub/HUB')
(out/'identities.json').write_text(json.dumps({'status':'PASS','before':before,'after':retained(),'identities':identities},indent=2),encoding='utf-8')
