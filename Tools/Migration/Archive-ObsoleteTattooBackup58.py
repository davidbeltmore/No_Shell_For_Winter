"""Archive an unreferenced historical widget byte-for-byte, then retire via Editor.

The working TattooShop widget stays at its original public path. Run in target UE.
"""
import hashlib, json, zipfile
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
relative = 'Content/_MigrationBackup/TattooShop/20260811-231427/WBP_TattooCustomization.uasset'
package = '/Game/_MigrationBackup/TattooShop/20260811-231427/WBP_TattooCustomization'
archive = root/'Tools/Migration/Archive/TattooShop_20260811-231427.zip'
manifest = archive.with_suffix('.json')
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.wait_for_completion()
options = unreal.AssetRegistryDependencyOptions(True, True, True, True, True)
references = list(registry.get_referencers(package, options))
assert not references, 'Historical widget acquired references; do not retire it.'
active = unreal.load_asset('/Game/TattooShop/Blueprints/Widget/WBP_TattooCustomization')
assert active, 'Working widget is missing.'
unreal.BlueprintEditorLibrary.compile_blueprint(active)
assert 'UP_TO_DATE' in str(active.get_editor_property('status')), 'Working widget does not compile.'
source = root/relative
if source.exists():
    original = source.read_bytes()
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr(relative, original)
    with zipfile.ZipFile(archive) as z:
        assert z.namelist() == [relative] and z.read(relative) == original
    record = {'original_path': relative, 'original_package': package,
              'sha256': hashlib.sha256(original).hexdigest(), 'bytes': len(original),
              'archive': archive.relative_to(root).as_posix(), 'referencers': [],
              'active_widget': unreal.Object.get_path_name(active),
              'restore': 'Extract the single original entry into a detached project copy first; validate before restoring to the live project.',
              'retired_through_editor': False}
    manifest.write_text(json.dumps(record, indent=2)+'\n', encoding='utf8')
    assert unreal.EditorAssetLibrary.delete_asset(package), 'Editor retirement failed; archive retained.'
    assert not source.exists()
    record['retired_through_editor'] = True
    manifest.write_text(json.dumps(record, indent=2)+'\n', encoding='utf8')
    print(json.dumps(record))
else:
    assert archive.exists() and manifest.exists(), 'No live backup and no archive.'
    print('Already archived; existing manifest retained.')
