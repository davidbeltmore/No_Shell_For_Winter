"""Read-only authored data and in-memory Blueprint compile gate; never save assets."""
import hashlib
import json
import pathlib
import re
import subprocess
import traceback
import unreal

ROOT = pathlib.Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/EquipmentQA/cook_preflight.json'
REPORT = {'status': 'PENDING', 'blueprints': [], 'data': [], 'assets_saved': []}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def validate_native(asset):
    result = asset.validate()
    if isinstance(result, str):
        return not result, result
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1])
    return result is True, '' if result is True else 'Native validation failed'

def validate():
    hub_pawn = unreal.load_asset('/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar')
    REPORT['hub_pawn_parent'] = unreal.BlueprintEditorLibrary.get_blueprint_parent_class(hub_pawn).get_path_name()
    REPORT['commit'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    REPORT['working_tree'] = subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True).splitlines()
    REPORT['map_sha256'] = {path: digest(ROOT / path) for path in (
        'Content/FullSample/Integrations/UIIntegrations/Level/MenuMap.umap',
        'Content/_Game/Hub/HUB.umap', 'Content/Procedural/Maps/DungeonGeneration.umap')}
    registry = unreal.load_asset('/Game/_Game/Data/Equipment/DA_EquipmentSlots')
    assert registry and validate_native(registry)[0], 'Equipment slot registry is invalid'
    for path in unreal.EditorAssetLibrary.list_assets('/Game/_Game/Data/Equipment', recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        if isinstance(asset, (unreal.EFEquipmentRuleProfile, unreal.EFEquipmentSlotRegistry)):
            valid, error = validate_native(asset)
            REPORT['data'].append({'path': path, 'valid': valid, 'error': error})
            assert valid, path + ': ' + error
    clothing = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
    assert clothing.is_policy_valid(), clothing.get_policy_validation_error()
    manifest = unreal.load_asset('/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingSystemManifest')
    assert manifest.is_valid_manifest(), manifest.get_validation_error()
    assert len(manifest.get_editor_property('body_profiles')) >= 2
    assert len(manifest.get_editor_property('garments')) >= 12
    REPORT['clothing_authority'] = clothing.get_path_name()
    REPORT['generated_manifest'] = manifest.get_path_name()
    director = unreal.load_asset('/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector')
    assert director, 'Configured travel Director is missing'
    errors = director.get_authoring_errors()
    assert not errors, str(errors)
    REPORT['travel_authority'] = {'version': 7, 'asset': director.get_path_name()}
    # Compile every Blueprint changed by this equipment implementation. The
    # pre-existing user HUB map is not a Blueprint and is never loaded/saved here.
    changed = subprocess.check_output(['git', 'diff', '--name-only', '--diff-filter=ACMRT', '6a72c7fbdd1e00f2f51beb450d7456e2fd10b480', '--', 'Content'], cwd=ROOT, text=True).splitlines()
    packages = sorted('/Game/' + p[len('Content/'):-len('.uasset')] for p in changed if p.endswith('.uasset'))
    hashes = {p: digest(ROOT / ('Content/' + p[len('/Game/'):] + '.uasset')) for p in packages}
    for package in packages:
        asset = unreal.load_asset(package)
        assert asset, 'Missing modified asset ' + package
        if isinstance(asset, unreal.Blueprint):
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            status = str(asset.get_editor_property('status'))
            normalized = re.sub('[^A-Z0-9]', '', status.upper())
            REPORT['blueprints'].append({'path': package, 'status': status})
            assert 'UPTODATE' in normalized and 'WARNING' not in normalized, package + ': ' + status
    assert len(REPORT['blueprints']) >= 20, 'Equipment Blueprint gate did not discover its consumers'
    for package, before in hashes.items():
        assert before == digest(ROOT / ('Content/' + package[len('/Game/'):] + '.uasset')), 'Read-only validation changed ' + package
    REPORT['asset_sha256'] = hashes
    REPORT['cook_support_packages'] = [director.get_path_name().split('.')[0]]
    REPORT['status'] = 'PASS'

try:
    validate()
except Exception:
    REPORT['status'] = 'FAIL'
    REPORT['error'] = traceback.format_exc()
finally:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(REPORT, indent=2), encoding='utf-8')
if REPORT['status'] != 'PASS':
    raise RuntimeError(REPORT['error'])
unreal.log('Equipment cook preflight: PASS; no assets saved')
