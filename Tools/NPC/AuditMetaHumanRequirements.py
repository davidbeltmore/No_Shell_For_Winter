"""Read-only live check of retained ACF MetaHuman requirements; run after restart.

Invoke via Tools/InnerState/RunEditorPython58.py. Does not save vendor assets or
dismiss/remove AssetGuidelines. Runtime values alone do not satisfy guidelines:
UE also compares the exact strings stored in the Engine config branch.
"""
import configparser
import json
import time
from pathlib import Path
import unreal


def audit_metahuman_requirements():
    started = time.monotonic()
    root = Path(unreal.Paths.project_dir())
    config = configparser.ConfigParser(strict=False, interpolation=None)
    config.optionxform = str
    config.read(root / 'Config/DefaultEngine.ini', encoding='utf-8-sig')
    enabled = set(unreal.PluginBlueprintLibrary.get_enabled_plugin_names())
    report = {'plugins': {}, 'settings': {}, 'assets': {}, 'errors': []}
    for plugin in ['LiveLinkControlRig', 'AppleARKitFaceSupport',
                   'DazToUnreal', 'EFCharacterCreationDazBridge']:
        report['plugins'][plugin] = plugin in enabled
        if plugin not in enabled:
            report['errors'].append('Disabled plugin: ' + plugin)
    for key, expected in {
        'r.GPUSkin.Support16BitBoneIndex': 'True',
        'r.GPUSkin.UnlimitedBoneInfluences': 'True',
        'r.SkinCache.CompileShaders': 'True',
        'SkeletalMesh.UseExperimentalChunking': '1',
    }.items():
        authored = config.get('/Script/Engine.RendererSettings', key, fallback='')
        effective = unreal.SystemLibrary.get_console_variable_int_value(key)
        report['settings'][key] = {'authored': authored, 'effective': effective}
        if authored != expected or effective != 1:
            report['errors'].append('Incorrect setting: ' + key)
    for path in [
        '/AscentCombatFramework/MetaHumans/Common/Common/Mocap/mh_arkit_mapping_anim',
        '/Game/FullSample/MetaHumans/Hadley/Face/Hadley_FaceMesh',
    ]:
        assert time.monotonic() - started < 60, '60-second audit deadline'
        asset = unreal.load_asset(path)
        report['assets'][path] = bool(asset)
        if not asset:
            report['errors'].append('Cannot load: ' + path)
    report['status'] = 'FAIL' if report['errors'] else 'PASS'
    report['seconds'] = round(time.monotonic() - started, 3)
    out = root / 'Saved/MetaHumanGuidelines'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'requirements.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))
    assert not report['errors'], report['errors']


audit_metahuman_requirements()
