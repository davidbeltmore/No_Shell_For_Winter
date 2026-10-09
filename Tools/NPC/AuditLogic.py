"""Read-only effective component parity for all owned NPCs; 120-second ceiling."""
import json,time,unreal
from pathlib import Path
audit_root=Path(unreal.Paths.project_dir());audit_start=time.monotonic()
audit_report={'variants':[],'errors':[]}
for row in json.loads((audit_root/'Tools/NPC/Roster.json').read_text())['archetypes']:
    src=row.get('source') or ('/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/' if row['family']=='Services' else '/Game/FullSample/Blueprints/Characters/'+row['family']+'/')+row['name']
    for gender in ['Male','Female']:
        assert time.monotonic()-audit_start<120,'120-second parity deadline'
        dst='/Game/_Game/Characters/'+gender+'/'+row['name']+gender
        result=json.loads(unreal.ProjectCompatibilityInspectionLibrary.sync_owned_npc_logic_defaults(unreal.load_asset(src),unreal.load_asset(dst),False))
        audit_report['variants'].append({'source':src,'target':dst,**result})
        if result.get('error') or result['errors'] or result['differences']:audit_report['errors'].append(dst)
audit_report.update(status='FAIL' if audit_report['errors'] else 'PASS',count=len(audit_report['variants']),properties_compared=sum(r.get('compared',0) for r in audit_report['variants']),seconds=time.monotonic()-audit_start)
(audit_root/'Saved/NPCLogic/audit.json').write_text(json.dumps(audit_report,indent=2))
print(json.dumps({k:v for k,v in audit_report.items() if k!='variants'}))
assert not audit_report['errors'],audit_report['errors']
