import json
import pathlib
import unreal

OUT = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/author_inventory_ui.json'
OUT.write_text(json.dumps({'result': 'PENDING: UI authoring started.'}))
try:
    out = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA'
    assets = pathlib.Path(__file__).parent / 'Assets'
    destination = '/Game/_Game/Widgets/Equipment/Assets/Icons'
    for source in assets.glob('*.png'):
        asset_path = destination + '/' + source.stem
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', str(source))
            task.set_editor_property('destination_path', destination)
            task.set_editor_property('automated', True)
            task.set_editor_property('save', True)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    
    result = unreal.EFEquipmentAuthoringLibrary.build_inventory_ui()
    if not result.startswith('PASS:'):
        raise RuntimeError(result)
    
    (out / 'author_inventory_ui.json').write_text(json.dumps({'result': result}, indent=2))
    print(result)
except Exception as error:
    OUT.write_text(json.dumps({'result': 'FAIL: ' + str(error)}, indent=2))
    raise
