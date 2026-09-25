"""Create project-owned, numerically equivalent copies; never modify vendor assets."""
import hashlib
import json
import re
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_saved_dir()) / 'Analysis/ProgressionPilot'
out.mkdir(parents=True, exist_ok=True)
report = []
pattern = re.compile(r'(Value=[^,]+,Curve=\(CurveTable=None,RowName=)"([^"]+)"(\),RegistryType="None")')
for name in ['ACF_AttDependencies_GE', 'ACF_StatInit_GE']:
    source = '/AscentCombatFramework/GASRuntime/GameplayEffects/' + name
    destination = '/Game/_Game/Data/Progression/GE_Project' + name.removeprefix('ACF_')
    source_class = unreal.load_class(None, source + '.' + name + '_C')
    if not source_class:
        raise RuntimeError(source)
    source_mods = [x.export_text() for x in unreal.get_default_object(source_class).get_editor_property('modifiers')]
    if unreal.load_asset(destination):
        asset = unreal.load_asset(destination)
    else:
        source_asset = unreal.load_asset(source)
        asset = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(
            destination.rsplit('/', 1)[1], destination.rsplit('/', 1)[0], source_asset)
    if not asset:
        raise RuntimeError('Duplicate failed: ' + destination)
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    cdo = unreal.get_default_object(unreal.load_class(None, destination + '.' + destination.rsplit('/', 1)[1] + '_C'))
    mods = list(cdo.get_editor_property('modifiers'))
    if len(mods) != len(source_mods):
        raise RuntimeError('Unexpected modifier count')
    removed = []
    for index, original in enumerate(source_mods):
        cleaned, count = pattern.subn(r'\1""\3', original)
        mods[index].import_text(cleaned)
        if mods[index].export_text() != cleaned:
            raise RuntimeError('Modifier round-trip mismatch')
        removed.append(count)
    cdo.set_editor_property('modifiers', mods)
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    report.append({'source': source, 'destination': destination, 'removed_orphan_rows_per_modifier': removed,
                   'source_modifiers_sha256': hashlib.sha256('\n'.join(source_mods).encode()).hexdigest(),
                   'native_source_unchanged': source_mods == [x.export_text() for x in unreal.get_default_object(source_class).get_editor_property('modifiers')]})
(out / 'curve_adapters.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('PROGRESSION_CURVE_ADAPTERS_COMPLETE')
