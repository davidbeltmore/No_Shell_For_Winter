"""Read-only inventory of effective NPC templates, initialization and authoring APIs."""
import json, time
from pathlib import Path
import unreal

started=time.monotonic()
out=Path(unreal.Paths.project_dir())/'Saved/NPCDAZ'
out.mkdir(parents=True,exist_ok=True)
eal=unreal.EditorAssetLibrary
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib=unreal.SubobjectDataBlueprintFunctionLibrary
def path(o): return o.get_path_name() if o else None
def properties(o,names):
    r={}
    for n in names:
        try: r[n]=str(o.get_editor_property(n))
        except Exception: pass
    return r
paths=[
 '/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale',
 '/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale',
 '/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/ACF_SkillGuy_BP',
 '/Game/FullSample/MetaHumans/ACF_NPCAI_BP',
 '/Game/FullSample/Blueprints/Characters/Enemies/Zombie/ACFZombieBP']
report={'templates':[]}
for p in paths:
    assert time.monotonic()-started<90
    bp=unreal.load_asset(p); cdo=unreal.get_default_object(bp.generated_class())
    item={'path':p,'parent':str(eal.find_asset_data(p).get_tag_value('ParentClass')),'components':[],'tags':[str(t) for t in cdo.tags]}
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        d=lib.get_data(h); o=lib.get_object_for_blueprint(d,bp)
        if not o: continue
        names=['skeletal_mesh_asset','anim_class','animation_mode','component_tags','relative_location','relative_rotation','relative_scale3d','character_init_data_asset','visibility_based_anim_tick_option','hidden_in_game']
        item['components'].append({'path':path(o),'class':o.get_class().get_name(),'props':properties(o,names)})
    report['templates'].append(item)
report['apis']={n:getattr(unreal.BlueprintEditorLibrary,n).__doc__ for n in ['reparent_blueprint','compile_blueprint']}
report['elapsed_seconds']=time.monotonic()-started
(out/'inspect.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
