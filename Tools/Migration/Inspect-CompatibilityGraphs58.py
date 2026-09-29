"""Read-only native MCP Blueprint graph snapshots of the reproduced failures."""
import json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Equipment'))
from UnrealMcp import Client
c=Client();B='editor_toolset.toolsets.blueprint.BlueprintTools'
c.describe(B)
out=Path(__file__).resolve().parents[2]/'Saved/Analysis/CompatibilityClosure/Graphs'
out.mkdir(parents=True,exist_ok=True)
for path in ['/AscentCombatFramework/Blueprints/Actions/ACF_ReleaseShootAction_BP',
             '/AscentCombatFramework/Blueprints/Actions/ACF_ChargedShootAction_BP',
             '/AscentCombatFramework/Blueprints/Components/ACFLadderComponentBP',
             '/AscentCombatFramework/Blueprints/Abilities/ACF_ClimbLadderStart_BP',
             '/AscentCombatFramework/UITools/Compass/Widget/ANS_MapMarker_WBP']:
    name=path.rsplit('/',1)[1];result={'path':path,'graphs':{}}
    try:
        for graph in c.call(B,'list_graphs',{'blueprint':{'refPath':path+'.'+name}})['returnValue']:
            nodes=c.call(B,'find_nodes',{'graph':graph,'title':''})['returnValue']
            result['graphs'][graph['refPath']]=c.call(B,'get_node_infos',{'nodes':nodes}) if nodes else {}
    except Exception as e: result['error']=str(e)
    (out/(name+'.json')).write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf8')
    print(name,'error' if 'error' in result else len(result['graphs']),flush=True)
