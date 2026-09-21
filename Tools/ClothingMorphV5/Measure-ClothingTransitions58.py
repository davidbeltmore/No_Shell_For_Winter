"""Outfit and body-swap latency; shares only setup/report helpers with single-item test."""
import importlib.util
import os
import pathlib
import time
import traceback
import unreal

spec = importlib.util.spec_from_file_location('clothing_latency_support', pathlib.Path(__file__).with_name('Measure-ClothingLatency58.py'))
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
S, R = h.S, h.R
# FootWrap currently uses the generic parent ItemSlot.Armor. ACF equipment
# lookups match parent tags, so place that existing item before child slots.
GROUPS = ((2,0,1), (2,3,4), (2,3,5))
TASKS = []
for gender in ('Female','Male'):
    for group in range(3):
        TASKS += [('outfit',gender,group), ('swap','Male' if gender == 'Female' else 'Female',group), ('swap',gender,group)]
h.TRIALS = TASKS
R.update(scenario='three-garment outfits and in-place body swaps', expected_trials=len(TASKS))


def garments():
    expected = {h.ITEMS[i][2] for i in GROUPS[TASKS[S['index']][2]]}
    return {c.get_skeletal_mesh_asset().get_path_name(): c
            for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
            if c.is_visible() and c.get_skeletal_mesh_asset()
            and c.get_skeletal_mesh_asset().get_path_name() in expected}


def next_trial():
    S['index'] += 1
    if S['index'] < len(TASKS) and TASKS[S['index']][0] == 'outfit':
        h.transition('cleanup',.2)
    else:
        h.transition('prepare',.2)


def tick(dt):
    if S['busy']:
        return
    S['busy'] = True
    entered = time.perf_counter()
    try:
        S['frame'] += 1
        if entered-S['start'] > 400:
            raise RuntimeError('Transition suite timeout: '+S['phase'])
        if entered < S['due']:
            return
        phase = S['phase']
        if phase in ('load','play','player'):
            # Reuse unmodified real PIE/ACF initialization.
            S['busy'] = False
            h.tick(dt)
            return
        if phase == 'cleanup':
            # Each ACF removal refreshes/reloads the remaining slots. Allow its
            # async completion before removing the next child slot; parent last.
            items = list(S['equipment'].get_current_equipment().get_editor_property('equipped_items'))
            if items:
                S['equipment'].unequip_item_by_guid(items[-1].get_editor_property('item_guid'))
                h.transition('cleanup',.3)
            else:
                h.transition('prepare',.3)
            return
        if phase == 'prepare':
            if S['index'] >= len(TASKS):
                h.finish()
                return
            kind, gender, group = TASKS[S['index']]
            if kind == 'outfit':
                if 'managed=0 ' not in S['runtime'].get_debug_summary():
                    if entered-S['due'] > 12:
                        raise RuntimeError('Outfit cleanup timed out')
                    return
                if S.get('gender') != gender:
                    assert S['custom'].select_gender(getattr(unreal.CharacterCreationGender, gender.upper()))
                    S['gender'] = gender
                    h.transition('prepare', 3.)
                    return
                S['classes'] = [unreal.load_class(None, h.ITEMS[i][1]+'.'+h.ITEMS[i][1].rsplit('/',1)[1]+'_C') for i in GROUPS[group]]
                h.transition('action', .37 + (S['index']%4)*.31)
            else:
                h.transition('action', .73)
        elif phase == 'action':
            kind, gender, group = TASKS[S['index']]
            S['trial'] = dict(kind=kind, gender=gender, repeat=0, garment='+'.join(h.ITEMS[i][0] for i in GROUPS[group]),
                              events=[], callback_gaps_s=[], observer_cost_s=[])
            S['t0'] = time.perf_counter()
            S['game0'] = unreal.GameplayStatics.get_time_seconds(S['world'])
            S['frame0'] = S['frame']
            S['last_sample'] = S['t0']
            S['notified'] = set()
            S['last_key'] = None
            if kind == 'outfit':
                for klass in S['classes']:
                    assert klass
                    S['equipment'].add_item_to_inventory_by_class(klass, 1, True)
            else:
                assert S['custom'].select_gender(getattr(unreal.CharacterCreationGender, gender.upper()))
                S['gender'] = gender
            S['trial']['action_call_s'] = time.perf_counter()-S['t0']
            S['trial']['equipment_after_action'] = [dict(
                slot=str(i.get_editor_property('item_slot')),
                item=i.get_editor_property('item').get_class().get_path_name())
                for i in S['equipment'].get_current_equipment().get_editor_property('equipped_items')]
            h.transition('observe')
        elif phase == 'observe':
            tr = S['trial']
            t = time.perf_counter()-S['t0']
            tr['callback_gaps_s'].append(entered-S['last_sample'])
            S['last_sample'] = entered
            meshes = garments()
            summary = S['runtime'].get_debug_summary()
            states = {p:str(S['runtime'].get_garment_runtime_state(c)) for p,c in meshes.items()}
            key = (str(states),summary)
            if key != S['last_key']:
                tr['events'].append(dict(wall_s=t, frames=S['frame']-S['frame0'], states=states, summary=summary))
                S['last_key'] = key
            if len(meshes) == 3:
                tr.setdefault('component_s',t)
            if h.MODE == 'notify':
                fresh = set(meshes)-S['notified']
                if fresh:
                    S['runtime'].notify_equipment_changed()
                    S['notified'].update(fresh)
                    tr.setdefault('notification_s',time.perf_counter()-S['t0'])
            if 'managed=3 ' in summary:
                tr.setdefault('managed_s',t)
            if len(states) == 3 and all('READY' in state.upper() for state in states.values()):
                tr.update(ready_s=t, frames=S['frame']-S['frame0'])
                body_path = f'/Game/DazToUnreal/{tr["gender"]}/{tr["gender"]}.{tr["gender"]}'
                body = next(c for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
                            if c.is_visible() and c.get_skeletal_mesh_asset()
                            and c.get_skeletal_mesh_asset().get_path_name() == body_path)
                if tr['gender'] == 'Male':
                    while body.get_editor_property('leader_pose_component'):
                        body = body.get_editor_property('leader_pose_component')
                    assert all(body.is_bone_hidden_by_name(b) for b in ('shaft_01','scrotum'))
                else:
                    assert not body.is_material_section_shown(body.get_material_index('Genesis9_GP_Torso'),0)
                tr['coverage'] = 'PASS'
                R['trials'].append(tr)
                h.write()
                if S['index'] in (0,1,6,7):
                    h.transition('capture',.4)
                else:
                    next_trial()
            elif t > 12:
                R['trials'].append(tr)
                tr['mesh_diagnostics'] = [dict(name=c.get_name(), visible=c.is_visible(),
                    mesh=c.get_skeletal_mesh_asset().get_path_name() if c.get_skeletal_mesh_asset() else None)
                    for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)]
                h.write()
                raise RuntimeError('Transition not Ready; see raw trial states and equipment: '+tr['garment'])
            tr['observer_cost_s'].append(time.perf_counter()-entered)
        elif phase == 'capture':
            path = h.OUT/f'{S["index"]}_{S["trial"]["gender"]}.png'
            unreal.AutomationLibrary.take_high_res_screenshot(1400,1000,str(path))
            R.setdefault('post_ready_screenshots',[]).append(str(path))
            S['capture_path'] = path
            h.write()
            h.transition('capture_wait',.5)
        elif phase == 'capture_wait':
            if not S['capture_path'].is_file():
                if entered-S['due'] > 10:
                    raise RuntimeError('Post-ready screenshot missing')
                return
            next_trial()
    except Exception:
        h.finish(traceback.format_exc())
    finally:
        S['busy'] = False


h.write()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['callback'] = unreal.register_slate_post_tick_callback(tick)
