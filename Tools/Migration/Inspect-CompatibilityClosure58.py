"""Read-only loaded-asset inventory; run in the target UE editor through its wrapper."""
import json, traceback
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.project_saved_dir()) / 'Analysis/CompatibilityClosure'
OUT.mkdir(parents=True, exist_ok=True)
report = {'resaved': False, 'meshes': {}, 'assets': {}, 'errors': []}

def value(obj, name):
    try:
        v = obj.get_editor_property(name)
        if isinstance(v, unreal.Object): return v.get_path_name()
        if hasattr(v, 'export_text'): return v.export_text()
        return str(v)
    except Exception as e: return {'unavailable': str(e)}

try:
    for name in ['Female', 'Male', 'Multiple']:
        mesh = unreal.load_asset('/Game/DazToUnreal/' + name + '/' + name)
        row = {'path': mesh.get_path_name(), 'morphs': value(mesh, 'morph_targets'),
               'physics_asset': value(mesh, 'physics_asset'), 'skeleton': value(mesh, 'skeleton'),
               'methods': [m for m in dir(mesh) if any(t in m for t in ['morph', 'bone', 'physics'])]}
        physics = mesh.get_editor_property('physics_asset')
        if physics:
            row['physics_methods'] = [m for m in dir(physics) if any(t in m for t in ['constraint', 'body'])]
            row['constraints'] = value(physics, 'constraint_setup')
            row['bodies'] = value(physics, 'skeletal_body_setups')
        skeleton = mesh.get_editor_property('skeleton')
        row['slots'] = value(skeleton, 'slot_groups')
        report['meshes'][name] = row
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    candidates = [a for a in registry.get_assets_by_path('/Game/FullSample', recursive=True)
                  if any(t in str(a.asset_name).lower() for t in ['ladder','climb','bow','arrow','introductiontoacf'])]
    for entry in candidates:
        path = str(entry.package_name)
        row = {'class': str(entry.asset_class_path)}
        asset = entry.get_asset()
        if isinstance(asset, unreal.Blueprint):
            generated = asset.generated_class()
            default = unreal.get_default_object(generated) if generated else None
            row['parent'] = value(asset, 'parent_class')
            row['methods'] = [m for m in dir(default) if not m.startswith('_')] if default else []
        if isinstance(asset, unreal.AnimMontage):
            row['tracks'] = value(asset, 'slot_anim_tracks')
        report['assets'][path] = row
except Exception:
    report['errors'].append(traceback.format_exc())
finally:
    (OUT / 'loaded_asset_inventory.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf8')
    unreal.log('CompatibilityClosure inventory: ' + str(OUT))
