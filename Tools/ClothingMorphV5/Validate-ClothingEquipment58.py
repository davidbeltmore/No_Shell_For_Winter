"""Real ACF replacement/cancellation tests plus an explicit stale-callback fixture."""
import importlib.util
import pathlib
import re
import time
import traceback
import unreal

spec = importlib.util.spec_from_file_location('clothing_latency_support', pathlib.Path(__file__).with_name('Measure-ClothingLatency58.py'))
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
S,R = h.S,h.R
CASES = [(g,c) for g in ('Female','Male') for c in ('replace_chest','replace_lower','cancel','bulk_unequip','stale_callback')]
h.TRIALS = CASES
R.update(scenario='native equipment bridge correctness', checks=[])


def meshes():
    return [c for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
            if c.get_skeletal_mesh_asset() and c.get_skeletal_mesh_asset().get_path_name() in {i[2] for i in h.ITEMS}]


def equip(index):
    entries = S['equipment'].get_all_items_of_class_in_inventory(S['classes'][index])
    if not entries:
        S['equipment'].add_item_to_inventory_by_class(S['classes'][index],1,False)
        entries = S['equipment'].get_all_items_of_class_in_inventory(S['classes'][index])
    assert entries, 'Inventory fixture could not add '+h.ITEMS[index][0]
    S['equipment'].equip_item_from_inventory(entries[0])


def coverage(expected):
    path = f'/Game/DazToUnreal/{S["gender"]}/{S["gender"]}.{S["gender"]}'
    body = next(c for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
                if c.is_visible() and c.get_skeletal_mesh_asset() and c.get_skeletal_mesh_asset().get_path_name() == path)
    if S['gender'] == 'Male':
        while body.get_editor_property('leader_pose_component'):
            body = body.get_editor_property('leader_pose_component')
        assert all(body.is_bone_hidden_by_name(b) == expected for b in ('shaft_01','scrotum'))
    else:
        assert body.is_material_section_shown(body.get_material_index('Genesis9_GP_Torso'),0) != expected


def tick(dt):
    if S['busy']:
        return
    S['busy'] = True
    now = time.perf_counter()
    try:
        if now-S['start'] > 300:
            raise RuntimeError('Equipment QA timeout: '+S['phase'])
        if now < S['due']:
            return
        phase = S['phase']
        if phase in ('load','play','player'):
            S['busy'] = False
            h.tick(dt)
            return
        if phase == 'prepare':
            assert S.get('bridge'), 'Native bridge missing'
            assert 'authoritative=1' in S['bridge'].get_debug_summary(), S['bridge'].get_debug_summary()
            if S['index'] >= len(CASES):
                S['idle_start'] = S['bridge'].get_debug_summary()
                h.transition('idle',7.)
                return
            gender,case = CASES[S['index']]
            if S.get('gender') != gender:
                assert S['custom'].select_gender(getattr(unreal.CharacterCreationGender,gender.upper()))
                S['gender'] = gender
                h.transition('prepare',2.)
                return
            if 'managed=0 ' not in S['runtime'].get_debug_summary():
                if now-S['due'] > 8:
                    raise RuntimeError('Cleanup failed: '+S['runtime'].get_debug_summary())
                return
            if 'classes' not in S:
                S['classes'] = [unreal.load_class(None,i[1]+'.'+i[1].rsplit('/',1)[1]+'_C') for i in h.ITEMS]
            S['case'] = case
            if case == 'replace_chest':
                equip(0)
            elif case in ('replace_lower','cancel','stale_callback'):
                equip(1)
            else:
                for i in (2,0,1): equip(i)
            h.transition('seed_ready',.1)
        elif phase == 'seed_ready':
            visible = [c for c in meshes() if c.is_visible()]
            expected_count = 3 if S['case'] == 'bulk_unequip' else 1
            if len(visible) != expected_count or not all('READY' in str(S['runtime'].get_garment_runtime_state(c)).upper() for c in visible):
                if now-S['due'] > 8:
                    raise RuntimeError('Seed did not become Ready')
                return
            S['t0'] = time.perf_counter()
            S['expected'] = set()
            if S['case'] == 'replace_chest':
                equip(3)
                equip(0)
                equip(3)
                S['expected'] = {h.ITEMS[3][2]}
            elif S['case'] == 'replace_lower':
                equip(4)
                equip(5)
                equip(1)
                S['expected'] = {h.ITEMS[1][2]}
            elif S['case'] == 'cancel':
                equip(4)
                h.unequip() # Same-frame cancellation with source loads pending.
            else:
                h.unequip() # Deliberately bulk-remove: native bridge must guard late reloads.
            h.transition('verify',.5)
        elif phase == 'verify':
            R['current_equipment'] = [i.get_editor_property('item').get_class().get_path_name()
                for i in S['equipment'].get_current_equipment().get_editor_property('equipped_items')]
            R['current_bridge'] = S['bridge'].get_debug_summary()
            visible = [c for c in meshes() if c.is_visible()]
            paths = {c.get_skeletal_mesh_asset().get_path_name() for c in visible}
            assert paths == S['expected'], f'{S["case"]} expected={S["expected"]} visible={paths}'
            assert all('READY' in str(S['runtime'].get_garment_runtime_state(c)).upper() for c in visible)
            coverage(S['case'] == 'replace_lower')
            R['checks'].append(dict(gender=S['gender'],case=S['case'],status='PASS', elapsed_s=now-S['t0'],
                                    bridge=S['bridge'].get_debug_summary(),visible=sorted(paths)))
            if S['case'] == 'stale_callback' and not S.get('injected'):
                old = next(c for c in meshes() if c.get_skeletal_mesh_asset().get_path_name() == h.ITEMS[1][2])
                old.set_visibility(True) # Explicit simulation, NOT claimed as a disk-load measurement.
                S['injected'] = True
                S['t0'] = time.perf_counter()
                h.transition('verify',.25)
                return
            S['injected'] = False
            h.unequip()
            S['index'] += 1
            h.write()
            h.transition('prepare',.6)
        elif phase == 'idle':
            final = S['bridge'].get_debug_summary()
            before = int(re.search(r'notifications=(\d+)',S['idle_start']).group(1))
            after = int(re.search(r'notifications=(\d+)',final).group(1))
            assert after == before, 'Idle equipment caused repeated fitter notifications'
            R['checks'].append(dict(case='idle_7s',status='PASS',before=S['idle_start'],after=final))
            h.finish()
    except Exception:
        h.finish(traceback.format_exc())
    finally:
        S['busy'] = False


h.write()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['callback'] = unreal.register_slate_post_tick_callback(tick)
