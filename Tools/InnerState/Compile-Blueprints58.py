"""Compile the affected asset closure without resaving protected Player/UI assets."""
import unreal
import json
from pathlib import Path

paths=[]
for folder in ['/Game/_Game/InnerState','/Game/_Game/Widgets/InnerState','/Game/_Game/Widgets/InnerDoctrine']:
    paths += unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False)
paths += ['/Game/FullSample/Player', '/Game/FullSample/Blueprints/StatusEffects/ACF_PoisonStatus_BP']
rows=[]
for path in paths:
    asset=unreal.load_asset(path)
    if isinstance(asset, unreal.Blueprint):
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        status=str(asset.get_editor_property('Status'))
        rows.append({'asset':asset.get_path_name(), 'status':status, 'passed':'BS_UP_TO_DATE' in status})
report={'passed':all(x['passed'] for x in rows), 'count':len(rows), 'assets':rows}
(Path(unreal.Paths.project_saved_dir())/'Migration/InnerState20261005/blueprint_compile.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({'passed':report['passed'],'count':len(rows),'failed':[x for x in rows if not x['passed']]}))
