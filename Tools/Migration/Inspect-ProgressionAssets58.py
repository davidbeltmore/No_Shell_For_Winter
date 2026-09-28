"""Read-only asset/default audit of unresolved progression-pilot warnings."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'Analysis/ProgressionRemaining'
out.mkdir(parents=True,exist_ok=True)
report={}
def values(obj, names):
    result={'path':obj.get_path_name(),'class':obj.get_class().get_path_name()}
    for name in names:
        try:
            value=obj.get_editor_property(name)
            result[name]=value.export_text() if hasattr(value,'export_text') else str(value)
        except Exception as e: result[name]={'error':str(e)}
    return result
for name in ['GameplayAbilitiesDeveloperSettings','AbilitySystemGlobals']:
    c=unreal.load_class(None,'/Script/GameplayAbilities.'+name)
    report[name]=values(unreal.get_default_object(c),['GlobalGameplayCueManagerClass','GlobalGameplayCueManagerName'])
(out/'gas_defaults.json').write_text(json.dumps(report,indent=2),encoding='utf8')
report['cue_managers']=[x.get_path_name()+' : '+x.get_class().get_path_name() for x in unreal.ObjectIterator(unreal.load_class(None,'/Script/GameplayAbilities.GameplayCueManager'))]
for name in ['ACF_DefaultHit_GC','ACF_DefaultReaction_GC']:
    c=unreal.load_class(None,'/Game/FullSample/Blueprints/GAS/Cues/'+name+'.'+name+'_C')
    report[name]=values(unreal.get_default_object(c),['gameplay_cue_tag','is_override'])
for path in ['/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFUltimatePlayerControllerBP',
             '/Game/FullSample/Integrations/ATSIntegrations/AQSQuests',
             '/Game/FullSample/DemoRoom/Blueprints/DA_Quests']:
    obj=unreal.load_asset(path)
    if isinstance(obj,unreal.DataTable):
        report[path]=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(obj))
report['quest_objects']=[]
for obj in unreal.ObjectIterator(unreal.AQSQuestManagerComponent):
        report['quest_objects'].append(values(obj,['default_quest_tag','quests_db']))
for obj in unreal.ObjectIterator(unreal.AQSQuestObjective):
        report['quest_objects'].append(values(obj,['target_ref_type','referenced_actors','objective_tag']))
(out/'asset_defaults.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.log('PROGRESSION_ASSET_INSPECTION_COMPLETE')
