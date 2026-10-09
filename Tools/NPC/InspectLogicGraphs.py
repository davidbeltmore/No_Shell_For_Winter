"""Native MCP graph snapshots of shared interaction and AI boundaries, read-only."""
import json,sys,time
from pathlib import Path
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'Tools/Equipment'))
from UnrealMcp import Client
c=Client(); ts='editor_toolset.toolsets.blueprint.BlueprintTools'; c.describe(ts)
out=root/'Saved/NPCLogic/Graphs';out.mkdir(parents=True,exist_ok=True); start=time.monotonic()
for p in ['/AscentCombatFramework/Integrations/Components/ACFDialogueComponentBP','/AscentCombatFramework/Integrations/Actors/ACF_NPC_BP','/AscentCombatFramework/Blueprints/AI/Controllers/ACF_NPCController_BP','/Game/_Game/Characters/Framework/ACF_NPC_BP']:
    name=p.rsplit('/',1)[1]; report={'path':p,'graphs':{}}
    for graph in c.call(ts,'list_graphs',{'blueprint':{'refPath':p+'.'+name}})['returnValue']:
        assert time.monotonic()-start<100
        nodes=c.call(ts,'find_nodes',{'graph':graph,'title':''})['returnValue']
        report['graphs'][graph['refPath']]=c.call(ts,'get_node_infos',{'nodes':nodes}) if nodes else {}
    (out/(('owned-' if '/_Game/' in p else '')+name+'.json')).write_text(json.dumps(report,indent=2))
    print(name,len(report['graphs']),flush=True)
