"""Read-only Epic/ACFU inventory; writes only the selected project audit receipt."""
import argparse, datetime, json, os, time
from pathlib import Path

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def main():
    start=time.monotonic()
    ap=argparse.ArgumentParser()
    ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--engine',type=Path,required=True)
    args=ap.parse_args(); project=args.project.resolve(); engine=args.engine.resolve()
    assert (project/'NoShellForWinter.uproject').is_file(), 'Target project identity missing'
    manifests=Path(os.environ.get('PROGRAMDATA','C:/ProgramData'))/'Epic/EpicGamesLauncher/Data/Manifests'
    fields=['DisplayName','AppName','AppVersionString','InstallLocation','MainGameAppName','bIsIncompleteInstall']
    items=[]; errors=[]
    for path in manifests.glob('*.item'):
        if time.monotonic()-start>20: raise TimeoutError('20-second inventory ceiling')
        try:
            item=read(path)
            if Path(item.get('InstallLocation','')).resolve()!=engine: continue
            name=(item.get('DisplayName','')+' '+item.get('AppName','')).lower()
            if 'acf' in name or item.get('AppName','').startswith('UE_'):
                items.append({'manifest':str(path),**{k:item.get(k) for k in fields}})
        except (OSError,ValueError) as exc: errors.append(str(exc))
    candidates=list((project/'Plugins').rglob('AscentCombatFramework.uplugin'))+list((engine/'Engine/Plugins/Marketplace').rglob('AscentCombatFramework.uplugin'))
    plugins=[]
    engine_modules=read(engine/'Engine/Binaries/Win64/UnrealEditor.modules')
    for path in candidates:
        desc=read(path); binary=path.parent/'Binaries/Win64/UnrealEditor.modules'
        modules=read(binary) if binary.exists() else {}
        plugins.append({'descriptor':str(path),'project_override':path.is_relative_to(project),
            **{k:desc.get(k) for k in ['FriendlyName','VersionName','Version','EngineVersion']},
            'editor_build_id':modules.get('BuildId'),'engine_build_id':engine_modules.get('BuildId'),
            'build_id_match':bool(modules.get('BuildId')) and modules.get('BuildId')==engine_modules.get('BuildId'),
            'missing_editor_modules':[v for v in modules.get('Modules',{}).values() if not (binary.parent/v).exists()]})
    result={'timestamp_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'project':str(project),'engine':str(engine),
        'engine_version':read(engine/'Engine/Build/Build.version'),'epic_installations':items,'plugins':plugins,'errors':errors,
        'launcher_gui':'PENDING: separate desktop inspection required','newer_release_available':'PENDING: not established by local inventory',
        'seconds':round(time.monotonic()-start,3)}
    result['status']='PASS' if len(plugins)==1 and plugins[0]['build_id_match'] and not plugins[0]['missing_editor_modules'] and not errors and len(items)>=2 else 'PENDING'
    out=project/'Saved/InstallationAudit/acfu.json';out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
