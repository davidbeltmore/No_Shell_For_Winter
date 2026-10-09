"""Read-only source/copy logic comparison; no compiling, spawning or saving assets."""
import json,time,unreal
from pathlib import Path
root=Path(unreal.Paths.project_dir()); out=root/'Saved/NPCLogic'; out.mkdir(parents=True,exist_ok=True)
start=time.monotonic(); manifest=json.loads((root/'Tools/NPC/Roster.json').read_text())
def path(o): return o.get_path_name() if isinstance(o,unreal.Object) else str(o)
def props(o):
    result={}
    for name in dir(o):
        if name.startswith('_'): continue
        try:
            v=o.get_editor_property(name)
            if not callable(v): result[name]=path(v)
        except Exception: pass
    return result
def components(bp):
    sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem); lib=unreal.SubobjectDataBlueprintFunctionLibrary
    result=[]
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        o=lib.get_object_for_blueprint(lib.get_data(h),bp)
        if isinstance(o,unreal.ActorComponent): result.append({'name':o.get_name(),'class':o.get_class().get_path_name(),'properties':props(o)})
    return result
report={'world':unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),'pie':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'dirty_content':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'dirty_maps':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'templates':[]}
for row in manifest['archetypes']:
    if row['id'] not in ['LevelingGuy','Vendor','Guy','GunEnemy','MeleeEnemy']: continue
    src=row.get('source') or ('/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/' if row['family']=='Services' else '/Game/FullSample/Blueprints/Characters/'+row['family']+'/')+row['name']
    for p in [src]+['/Game/_Game/Characters/'+g+'/'+row['name']+g for g in ['Male','Female']]:
        assert time.monotonic()-start<90
        bp=unreal.load_asset(p); actor=unreal.get_default_object(bp.generated_class())
        data=actor.get_component_by_class(unreal.ACFCharacterInitializerComponent).get_editor_property('character_init_data_asset')
        report['templates'].append({'path':p,'parent':str(unreal.EditorAssetLibrary.find_asset_data(p).get_tag_value('ParentClass')),'actor':props(actor),'components':components(bp),'data':{'path':path(data),'properties':props(data),'fragments':[{'class':f.get_class().get_path_name(),'properties':props(f)} for f in data.fragments]} if data else None})
report['seconds']=time.monotonic()-start
(out/'inspect-logic.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='templates'}))
