"""Bounded transient-only reduction and witness-work estimates; never authors assets."""
import argparse
import hashlib
import json
import time
import traceback
from pathlib import Path
import unreal

parser = argparse.ArgumentParser()
parser.add_argument('--garments', default='RagShirt,RagPants,RagFootWraps')
parser.add_argument('--ratios', default='0.5,0.25')
args = parser.parse_args(globals().get('NPC_OUTFIT_ARGS', []))
ratios = [float(value) for value in args.ratios.split(',')]
assert 0 < len(ratios) <= 3 and all(0.1 <= r < 1 for r in ratios)
names = args.garments.split(',')
assert 0 < len(names) <= 3 and len(names) == len(set(names))
root = Path(unreal.Paths.project_dir()).resolve()
out = root/'Saved/NPCOutfits/LODProbe'/str(time.time_ns())
out.mkdir(parents=True)
started = time.monotonic()
report = {'status': 'PENDING', 'rows': [], 'saved_assets': False, 'limit_seconds': 120}

def dirty():
    return sorted(p.get_path_name() for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
                  + list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()))

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert not dirty(), 'Preserve pre-existing dirty assets; run the probe with an idle, saved editor'
    director = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
    meshes = {}
    for row in director.garments:
        if str(row.garment_id) in names:
            mesh = row.source_garment
            if not isinstance(mesh, unreal.SkeletalMesh):
                mesh = unreal.load_asset(str(mesh))
            assert isinstance(mesh, unreal.SkeletalMesh), 'Source garment mesh unavailable'
            meshes[str(row.garment_id)] = mesh
    assert set(meshes) == set(names)
    packages = []
    for mesh in meshes.values():
        path = mesh.get_path_name().split('.')[0]
        assert path.startswith('/Game/')
        packages.append(root/'Content'/(path[len('/Game/'):]+'.uasset'))
    before = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    report['source_packages_before'] = before
    helper = root/'Plugins/EFClothingMorph/Source/EFClothingMorphEditor/Private/EFClothingNativeMeshAuthoringLibrary.cpp'
    report['helper_sha256'] = hashlib.sha256(helper.read_bytes()).hexdigest()
    for name in names:
        for ratio in ratios:
            assert time.monotonic()-started < 120, 'Probe deadline before next reduction'
            raw = unreal.EFClothingNativeMeshAuthoringLibrary.inspect_reduced_garment_lod(director, name, ratio)
            value = raw[-1] if isinstance(raw, tuple) else raw
            assert isinstance(value, str) and value.startswith('{'), 'Native probe did not return JSON: '+str(raw)
            row = json.loads(value)
            report['rows'].append(row)
            assert row['status'] == 'PASS', str(row)
            (out/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    report['source_packages_after'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in packages}
    report['dirty_packages_after'] = dirty()
    assert report['source_packages_after'] == before and not report['dirty_packages_after']
    assert time.monotonic()-started <= 120, 'Probe exceeded total deadline'
    report['status'] = 'PASS'
except Exception:
    report.update(status='FAIL', error=traceback.format_exc())
finally:
    report['seconds'] = time.monotonic()-started
    (out/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(str(out/'result.json'))
if report['status'] != 'PASS':
    raise RuntimeError(report['error'])
