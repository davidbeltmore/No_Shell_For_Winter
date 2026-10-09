"""Restore source effective component defaults on owned ancestors and 40 variants."""
import json,re,shutil,time,unreal
from pathlib import Path
root=Path(unreal.Paths.project_dir());out=root/'Saved/NPCLogic';out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((root/'Tools/NPC/Roster.json').read_text()); start=time.monotonic();report={'pairs':[]}
e=unreal.EditorAssetLibrary; pairs={}
def parent(p):
    value=str(e.find_asset_data(p).get_tag_value('ParentClass'))
    match=re.search("'([^']+)'",value); return match.group(1).split('.')[0] if match else value
def source(row):
    return row.get('source') or ('/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/' if row['family']=='Services' else '/Game/FullSample/Blueprints/Characters/'+row['family']+'/')+row['name']
def ancestors(p):
    par=parent(p)
    if par.startswith('/Script/'): return
    ancestors(par)
    dst='/Game/_Game/Characters/Framework/'+par.rsplit('/',1)[1]
    assert e.does_asset_exist(dst),dst
    pairs[dst]=par
for row in manifest['archetypes']: ancestors(source(row))
for row in manifest['archetypes']:
    for gender in ['Male','Female']: pairs['/Game/_Game/Characters/'+gender+'/'+row['name']+gender]=source(row)
for dst,src in pairs.items():
    assert time.monotonic()-start<180,'Repair deadline'
    path=root/'Content'/(dst.removeprefix('/Game/')+'.uasset'); backup=out/'Originals'/path.relative_to(root)
    if not backup.exists(): backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,backup)
    original=unreal.load_asset(src);owned=unreal.load_asset(dst)
    result=json.loads(unreal.ProjectCompatibilityInspectionLibrary.sync_owned_npc_logic_defaults(original,owned,True))
    report['pairs'].append({'source':src,'target':dst,**result})
    (out/'repair.json').write_text(json.dumps(report,indent=2))
    assert not result.get('error') and not result['errors'],result
    assert unreal.BlueprintEditorLibrary.compile_blueprint(owned),dst
    assert e.save_loaded_asset(owned,only_if_is_dirty=False),dst
report.update(status='PASS',count=len(pairs),seconds=time.monotonic()-start)
(out/'repair.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='pairs'}))
