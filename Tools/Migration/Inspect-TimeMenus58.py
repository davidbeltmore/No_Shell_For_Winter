import json
from pathlib import Path
import unreal
out = Path(unreal.Paths.project_saved_dir()) / 'Analysis/TimeKeys20261007'
out.mkdir(parents=True, exist_ok=True)
data = {'dirty_maps':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()], 'dirty_content':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
for path in ['/Game/FullSample/Integrations/SharedAssets/ACF_PauseMenu_WB', '/Game/FullSample/Integrations/UIIntegrations/Widgets/Time/ACFTimeTravel_WB', '/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFUltimatePlayerControllerBP', '/Game/FullSample/Blueprints/Game/ACFFullPlayerControllerBP']:
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    cdo = unreal.get_default_object(cls)
    data[path] = {'type': str(type(cdo)), 'methods': [n for n in dir(cdo) if any(s in n.lower() for s in ['pause','resume','time','menu','widget'])]}
(out/'menus_before.json').write_text(json.dumps(data, indent=2))
print(json.dumps(data))
