"""Read-only native MCP inventory of remaining quest/cue contracts."""
import sys,json,urllib.request
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Equipment'))
from UnrealMcp import Client
c=Client()
B='editor_toolset.toolsets.blueprint.BlueprintTools'
O='editor_toolset.toolsets.object.ObjectTools'
c.describe('EditorToolset.EditorAppToolset')
assert not c.call('EditorToolset.EditorAppToolset','IsPIERunning',{})['returnValue']
c.describe(B);c.describe(O)
out=Path(__file__).resolve().parents[2]/'Saved/Analysis/ProgressionRemaining'
out.mkdir(parents=True,exist_ok=True)
paths=['/Game/FullSample/Blueprints/GAS/Cues/ACF_DefaultHit_GC',
       '/Game/FullSample/Blueprints/GAS/Cues/ACF_DefaultReaction_GC',
       '/Game/FullSample/Blueprints/Game/ACFFullPlayerControllerBP',
       '/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFUltimatePlayerControllerBP']
for path in paths:
    name=path.rsplit('/',1)[1];ref={'refPath':path+'.'+name}
    result={'path':path}
    try:
        result['properties']=c.call(O,'list_properties',{'instance':ref})
        if 'Cues/' in path:
            result['cue']=c.call(O,'get_properties',{'instance':ref,'properties':['GameplayCueTag']})
        graphs=c.call(B,'list_graphs',{'blueprint':ref})['returnValue']
        result['graphs']={}
        for graph in graphs:
            nodes=c.call(B,'find_nodes',{'graph':graph,'title':''})['returnValue']
            result['graphs'][graph['refPath']]=c.call(B,'get_node_infos',{'nodes':nodes}) if nodes else {}
    except Exception as e:result['error']=str(e)
    (out/(name+'.json')).write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf8')
    print(name,list(result),flush=True)
for name,path in [('quests','/Game/FullSample/Integrations/ATSIntegrations/AQSQuests.AQSQuests'),('hub','/Game/_Game/Hub/HUB.HUB:PersistentLevel')]:
    try:r=c.call(O,'list_properties',{'instance':{'refPath':path}})
    except Exception as e:r={'error':str(e)}
    (out/(name+'.json')).write_text(json.dumps(r,indent=2,ensure_ascii=False),encoding='utf8')
try:
    with urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:8000/mcp',method='DELETE',headers={'Mcp-Session-Id':c.session}),timeout=10):pass
except Exception:pass
