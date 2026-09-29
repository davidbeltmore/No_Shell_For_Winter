"""Replace unsafe all-actors[0] with the equivalent nullable first-actor query.

Run externally against the native editor MCP. Saves nothing; compile/test/save
the single project Blueprint explicitly after inspecting the resulting graph.
"""
import json,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/Equipment'))
from UnrealMcp import Client
c=Client();B='editor_toolset.toolsets.blueprint.BlueprintTools';c.describe(B)
path='/Game/FullSample/GASP/LevelPrototyping/LevelBlock.LevelBlock'
graph={'refPath':path+':UserConstructionScript'}
nodes=c.call(B,'find_nodes',{'graph':graph,'title':''})['returnValue']
before=c.call(B,'get_node_infos',{'nodes':nodes})['returnValue']
def named(suffix):return next(n for n in before if n['node']['refPath']==graph['refPath']+'.'+suffix)
def pin(node,direction,name):return next(p for p in node[direction+'_pins'] if p['name']==name)
old=named('K2Node_CallFunction_5');item=named('K2Node_GetArrayItem_0')
assert old['type_id']=='Actor|GetAllActorsOfClass'
assert pin(old,'input','ActorClass')['value']=='/Game/FullSample/GASP/LevelPrototyping/LevelVisuals.LevelVisuals_C'
assert pin(item,'input','Dimension 1')['value']=='0'
assert len(pin(old,'output','OutActors')['connected_pins'])==1
backup=ROOT/'Saved/Analysis/CompatibilityClosure/OwnedAbilitySetsBeforeRepair/LevelBlock.uasset'
if not backup.exists():shutil.copy2(ROOT/'Content/FullSample/GASP/LevelPrototyping/LevelBlock.uasset',backup)
new_ref=c.call(B,'create_node',{'graph':graph,'type_id':'Actor|GetActorOfClass','pos':old['position'],'declaring_class':{'refPath':'/Script/Engine.GameplayStatics'}})['returnValue']
new=c.call(B,'get_node_infos',{'nodes':[new_ref]})['returnValue'][0]
c.call(B,'set_pin_value',{'pin':pin(new,'input','ActorClass')['pin_id'],'value':pin(old,'input','ActorClass')['value']})
new=c.call(B,'get_node_infos',{'nodes':[new_ref]})['returnValue'][0]
for upstream in pin(old,'input','execute')['connected_pins']:
    c.call(B,'break_pins',{'output_pin':upstream,'input_pin':pin(old,'input','execute')['pin_id']})
    c.call(B,'connect_pins',{'output_pin':upstream,'input_pin':pin(new,'input','execute')['pin_id']})
for downstream in pin(old,'output','then')['connected_pins']:
    c.call(B,'connect_pins',{'output_pin':pin(new,'output','then')['pin_id'],'input_pin':downstream})
for downstream in pin(item,'output','Output')['connected_pins']:
    c.call(B,'connect_pins',{'output_pin':pin(new,'output','ReturnValue')['pin_id'],'input_pin':downstream})
c.call(B,'delete_node',{'node':item['node']})
c.call(B,'delete_node',{'node':old['node']})
after=c.call(B,'get_node_infos',{'nodes':c.call(B,'find_nodes',{'graph':graph,'title':''})['returnValue']})
(ROOT/'Saved/Analysis/CompatibilityClosure/Graphs/LevelBlock_after.json').write_text(json.dumps(after,indent=2,ensure_ascii=False),encoding='utf8')
print('Replaced query; existing valid/invalid branches and first-actor selection preserved. Compile and validate before saving.')
