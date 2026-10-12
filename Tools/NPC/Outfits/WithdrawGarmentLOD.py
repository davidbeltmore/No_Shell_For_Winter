"""Retire only the generated FullRag LOD experiment through Unreal Editor.

Inspect first; --apply backs up the current packages and removes LOD1 using the
native skeletal-mesh editor. Never modifies character bodies or their LOD policy.
"""
import argparse
import hashlib
import json
import shutil
import time
import traceback
from pathlib import Path
import unreal

parser = argparse.ArgumentParser()
parser.add_argument('--apply', action='store_true')
args = parser.parse_args(globals().get('NPC_OUTFIT_ARGS', []))
root = Path(unreal.Paths.project_dir()).resolve()
out = root/'Saved/NPCOutfits/GarmentLOD'/('withdraw_'+str(time.time_ns()))
out.mkdir(parents=True)
started = time.monotonic()
report = {'status': 'PENDING', 'apply': args.apply, 'rows': [], 'backups': []}
expected = {
    'RagShirt': '1f7dd61ee2470e709649529285ea800d1ab985bc6e0579d58c010869a84ba055',
    'RagPants': '172c487cb6906a2133381b90788623617696655c802d9af8265f1cdce02f437a',
    'RagFootWraps': '6ec991df2205bfef2403d7f169398265d5874cd1ab5bcf3a5e8f580b07274e0b',
}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def snapshot(mesh):
    return {'vertices': [sub.get_num_verts(mesh, i) for i in range(sub.get_lod_count(mesh))],
            'skeleton': mesh.get_editor_property('skeleton').get_path_name(),
            'morphs': sorted(m.get_name() for m in mesh.get_editor_property('morph_targets'))}

def write():
    report['seconds'] = time.monotonic()-started
    (out/'withdraw.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert not list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Preserve dirty packages'
    assert unreal.SystemLibrary.get_console_variable_int_value('ef.ClothingMorph.IndependentNPCLod') == 0
    sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    director = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
    meshes = {}
    for name, sha in expected.items():
        mesh = unreal.load_asset(f'/Game/DazToUnreal/{name}/{name}')
        path = root/f'Content/DazToUnreal/{name}/{name}.uasset'
        assert digest(path) == sha, 'Unexpected garment change; preserve it: '+name
        before = snapshot(mesh)
        assert len(before['vertices']) == 2, 'Expected only the original LOD0 and generated LOD1'
        meshes[name] = mesh
        report['rows'].append({'garment': name, 'mesh': mesh.get_path_name(), 'before': before, 'before_sha256': sha})
    write()
    if args.apply:
        paths = [root/f'Content/DazToUnreal/{n}/{n}.uasset' for n in expected]
        paths += [root/'Content/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.uasset']
        paths += [p for p in (root/'Plugins/EFClothingMorph/Content/_Internal/Compiled').rglob('*.uasset')
                  if any(n.lower() in p.name.lower() for n in expected) or 'registry' in p.name.lower() or 'manifest' in p.name.lower()]
        for path in paths:
            relative = path.relative_to(root)
            backup = out/'Backups'/relative
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, backup)
            report['backups'].append({'path': str(relative), 'sha256': digest(path)})
        write()
        for row in report['rows']:
            assert time.monotonic()-started < 120
            mesh = meshes[row['garment']]
            assert sub.remove_lods(mesh, [1]), 'Native LOD removal failed'
            after = snapshot(mesh)
            before = row['before']
            assert after['vertices'] == before['vertices'][:1]
            assert after['skeleton'] == before['skeleton'] and after['morphs'] == before['morphs']
            row['after'] = after
            assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
            row['saved'] = True
            write()
        rows = list(director.garments)
        for row in rows:
            if str(row.garment_id) in expected:
                row.set_editor_property('reduced_lod_maximum_witness_spacing_cm', .5)
        director.set_editor_property('garments', rows)
        assert unreal.EditorAssetLibrary.save_loaded_asset(director, only_if_is_dirty=False)
        report['director_saved'] = True
    report['status'] = 'PASS'
except Exception:
    report.update(status='FAIL', error=traceback.format_exc())
finally:
    write()
    print(str(out/'withdraw.json'))
if report['status'] != 'PASS':
    raise RuntimeError(report['error'])
