"""Read-only editor preflight: actual mesh hierarchy and dirty package inventory."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
assert root == Path('D:/Projects UE5/NoShellForWinter')
out = root / 'Saved/BodyPresetsQA'
out.mkdir(parents=True, exist_ok=True)
result = {'dirty_content': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
          'dirty_maps': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()], 'meshes': {}}
result['skeleton_modifier_methods'] = [x for x in dir(unreal.SkeletonModifier) if not x.startswith('_')]
for name in ['Male2', 'Male', 'Female']:
    mesh = unreal.load_asset(f'/Game/DazToUnreal/{name}/{name}')
    modifier = unreal.SkeletonModifier()
    assert modifier.set_skeletal_mesh(mesh)
    bones = list(modifier.get_all_bone_names())
    result['meshes'][name] = {'skeleton': mesh.get_editor_property('skeleton').get_path_name(),
                             'bones': [str(x) for x in bones],
                             'parents': {str(x): str(modifier.get_parent_name(x)) for x in bones}}
(out / 'inspection.json').write_text(json.dumps(result, indent=2))
print(json.dumps({'dirty_content': result['dirty_content'], 'dirty_maps': result['dirty_maps'],
                  'bone_counts': {k: len(v['bones']) for k, v in result['meshes'].items()}}))
