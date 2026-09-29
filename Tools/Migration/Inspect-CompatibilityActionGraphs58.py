import sys,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Equipment'))
from UnrealMcp import Client
c=Client();B='editor_toolset.toolsets.blueprint.BlueprintTools';c.describe(B)
result={}
for p in ['/AscentCombatFramework/Blueprints/Abilities/ACF_ClimbLadderLoop_BP',
          '/AscentCombatFramework/Integrations/Objectives/ACF_FollowNPCObjective_BP']:
    name=p.rsplit('/',1)[1]
    gs=c.call(B,'list_graphs',{'blueprint':{'refPath':p+'.'+name}})['returnValue']
    result[p]={g['refPath']:c.call(B,'get_node_infos',{'nodes':c.call(B,'find_nodes',{'graph':g,'title':''})['returnValue']}) for g in gs}
out=Path(__file__).resolve().parents[2]/'Saved/Analysis/CompatibilityClosure/Graphs/ladder_loop_quest_follow.json'
out.write_text(json.dumps(result,indent=2),encoding='utf8')
print(list(result))
