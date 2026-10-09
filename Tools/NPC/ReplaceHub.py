"""Replace only the roster's source NPC instances in HUB, preserving actor identity.
Uses native editor replacement, common-property transfer, reference remapping and
then restores the owned DAZ presentation. No global asset consolidation.
"""
import json,shutil,time,unreal
from pathlib import Path
root=Path(unreal.Paths.project_dir()); out=root/'Saved/NPCDAZ'
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not editor.is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/_Game/Hub/HUB.HUB'
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
manifest=json.loads((root/'Tools/NPC/Roster.json').read_text())
source_to_owned={}
for row in manifest['archetypes']:
    if 'source' in row: src=row['source']
    elif row['family']=='Services': src='/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/'+row['name']
    else: src='/Game/FullSample/Blueprints/Characters/'+row['family']+'/'+row['name']
    source_to_owned[src]='/Game/_Game/Characters/Male/'+row['name']+'Male'
backup=out/'Originals/Content/_Game/Hub/HUB.umap'
if not backup.exists(): backup.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(root/'Content/_Game/Hub/HUB.umap',backup)
report={'replaced':[]}; started=time.monotonic()
def transform_values(a):
    p=a.get_actor_location(); r=a.get_actor_rotation(); s=a.get_actor_scale3d()
    return [round(v,5) for v in (p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z)]
def guid_values(a):
    g=a.get_editor_property('actor_guid')
    return g.to_string()
for actor in actors.get_all_level_actors():
    src=actor.get_class().get_path_name().split('.')[0]
    if src not in source_to_owned: continue
    assert time.monotonic()-started<60
    before={'actor':actor.get_path_name(),'label':actor.get_actor_label(),'class':src,'transform':transform_values(actor),'tags':[str(t) for t in actor.tags],'guid':guid_values(actor)}
    replacement=unreal.load_asset(source_to_owned[src]); assert replacement
    new=unreal.ProjectCompatibilityInspectionLibrary.replace_owned_npc_in_level(actor,replacement); assert new
    assert new.get_path_name()==before['actor'],'Placed save identity changed'
    assert guid_values(new)==before['guid'],'Actor GUID changed'
    assert transform_values(new)==before['transform'],'Transform changed'
    assert set(before['tags'])<={str(t) for t in new.tags}
    new.set_actor_label(before['label'])
    before['replacement']=new.get_class().get_path_name(); report['replaced'].append(before)
remaining=[a.get_path_name() for a in actors.get_all_level_actors() if a.get_class().get_path_name().split('.')[0] in source_to_owned]
assert not remaining,remaining
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/_Game/Hub/HUB')
report.update(status='PASS',remaining_source_npcs=0,seconds=time.monotonic()-started)
(out/'hub-replacement.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
