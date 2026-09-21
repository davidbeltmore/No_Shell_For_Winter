"""Bounded PIE latency experiment. Does not save assets or change runtime code.

Readiness is render-dispatch validation, not a timestamp of displayed pixels.
Baseline NEVER forces reconciliation. Candidate modes change transient PIE state.
"""
import json
import os
import pathlib
import random
import re
import time
import traceback
import unreal

MODE = os.environ.get('EF_CLOTHING_LATENCY_MODE', 'baseline')
RUN = os.environ.get('EF_CLOTHING_LATENCY_RUN', MODE)
REPEATS = int(os.environ.get('EF_CLOTHING_LATENCY_REPEATS', '3'))
assert MODE in ('baseline', 'notify', 'watchdog', 'armor_event')
ROOT = pathlib.Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Saved/ClothingLatencyQA' / RUN
OUT.mkdir(parents=True, exist_ok=True)
ITEMS = [
    ('RagShirt', '/Game/_Game/Clothes/Rags01/RagShirt', '/Game/DazToUnreal/RagShirt/RagShirt.RagShirt'),
    ('RagPants', '/Game/_Game/Clothes/Rags01/RagPants', '/Game/DazToUnreal/RagPants/RagPants.RagPants'),
    ('RagFootWraps', '/Game/_Game/Clothes/Rags01/RagFootWrap', '/Game/DazToUnreal/RagFootWraps/RagFootWraps.RagFootWraps'),
    ('Bra', '/Game/_Game/Clothes/UnderWear01/Bra', '/Game/DazToUnreal/UnderWearBra/UnderWearBra.UnderWearBra'),
    ('Panty', '/Game/_Game/Clothes/UnderWear01/Panty', '/Game/DazToUnreal/UnderWearPanty/UnderWearPanty.UnderWearPanty'),
    ('Bikini', '/Game/_Game/Clothes/Bikini01/Bikini', '/Game/DazToUnreal/Bikini/Bikini.Bikini'),
]
LE = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
S = dict(phase='load', due=0., start=time.perf_counter(), index=0, frame=0, busy=False)
R = dict(status='PENDING', mode=MODE, run=RUN, trials=[], scope='PIE; fresh editor process; existing disk/DDC cache; no runtime code edits',
         ready_definition='First Python observation of Ready, based on native render-validated submission; NOT GPU completion/present time',
         saved_assets=False, repeats=REPEATS)
rng = random.Random(580921)
TRIALS = []
for gender in ('Female', 'Male'):
    for repeat in range(REPEATS):
        order = list(range(len(ITEMS)))
        rng.shuffle(order)
        for item in order:
            TRIALS.append((gender, repeat, item, rng.uniform(.05, 1.95)))


def write():
    (OUT/'result.json').write_text(json.dumps(R, indent=2), encoding='utf-8')


def transition(phase, delay=0.):
    S.update(phase=phase, due=time.perf_counter()+delay)
    (OUT/'progress.txt').write_text(f'{phase} {S["index"]}/{len(TRIALS)}')


def component(fragment):
    return next((c for c in S['player'].get_components_by_class(unreal.ActorComponent)
                 if fragment in c.get_class().get_name()), None)


def unequip():
    for item in list(S['equipment'].get_current_equipment().get_editor_property('equipped_items')):
        S['equipment'].unequip_item_by_guid(item.get_editor_property('item_guid'))


def find_garment():
    path = ITEMS[TRIALS[S['index']][2]][2]
    for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent):
        mesh = c.get_skeletal_mesh_asset()
        if mesh and mesh.get_path_name() == path and c.is_visible():
            return c
    return None


def finish(error=None):
    R['status'] = 'FAIL' if error else 'PASS'
    if error:
        R['error'] = error
    R['elapsed_s'] = time.perf_counter()-S['start']
    write()
    unreal.unregister_slate_post_tick_callback(S['callback'])
    unreal.EditorLevelLibrary.editor_end_play()
    unreal.SystemLibrary.quit_editor()


def armor_event(armor_slot):
    if S['phase'] not in ('equip','observe') or 'trial' not in S:
        return
    S['trial'].setdefault('acf_events',[]).append(dict(wall_s=time.perf_counter()-S['t0'],
        visible_target_mesh=bool(find_garment()), arguments=[str(armor_slot)]))
    S['runtime'].notify_equipment_changed()


def tick(dt):
    if S['busy']:
        return
    S['busy'] = True
    entered = time.perf_counter()
    try:
        S['frame'] += 1
        if entered-S['start'] > 540:
            raise RuntimeError('Latency suite timed out in '+S['phase'])
        if entered < S['due']:
            return
        phase = S['phase']
        if phase == 'load':
            if ED.get_editor_world().get_name() != 'HUB':
                LE.load_level('/Game/_Game/Hub/HUB')
            transition('play', 1.)
        elif phase == 'play':
            LE.editor_request_begin_play()
            transition('player', 4.)
        elif phase == 'player':
            world = ED.get_game_world()
            player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            if not player:
                return
            S.update(world=world, player=player)
            S['custom'] = component('EFCharacterCustomizationComponent')
            S['runtime'] = component('EFClothingRuntimeComponent') or component('EFClothingMorphV3RuntimeComponent')
            S['equipment'] = component('ACFEquipmentComponent')
            S['bridge'] = component('EFClothingEquipmentBridgeComponent')
            if not all(S[x] for x in ('custom', 'runtime', 'equipment')):
                return
            runtime = S['runtime']
            R['original_watchdog_s'] = runtime.get_editor_property('reconcile_watchdog_interval_seconds')
            R['native_equipment_bridge'] = S['bridge'].get_debug_summary() if S['bridge'] else None
            if MODE == 'watchdog':
                runtime.set_editor_property('reconcile_watchdog_interval_seconds', .25)
            R['effective_watchdog_s'] = runtime.get_editor_property('reconcile_watchdog_interval_seconds')
            fps_cap = int(os.environ.get('EF_CLOTHING_LATENCY_FPS','0'))
            R['requested_fps_cap'] = fps_cap
            if fps_cap:
                unreal.SystemLibrary.execute_console_command(world, f't.MaxFPS {fps_cap}')
            if MODE == 'armor_event':
                S['equipment'].get_editor_property('on_equipped_armor_changed').add_callable(armor_event)
            controller = unreal.GameplayStatics.get_player_controller(world, 0)
            klass = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectGameplayFreeCameraSubsystem')
            freecam = next(obj for obj in unreal.ObjectIterator(klass) if obj.get_outer() == world)
            assert freecam.start_gameplay_free_camera()
            camera = controller.get_view_target()
            position = player.get_actor_location()
            location = position+player.get_actor_forward_vector()*240+unreal.Vector(0,0,15)
            camera.set_actor_location_and_rotation(location,
                unreal.MathLibrary.find_look_at_rotation(location, position), False, True)
            unequip()
            transition('prepare', 3.)
        elif phase == 'prepare':
            if S['index'] >= len(TRIALS):
                finish()
                return
            gender, repeat, item, jitter = TRIALS[S['index']]
            if S.get('gender') != gender:
                assert S['custom'].select_gender(getattr(unreal.CharacterCreationGender, gender.upper()))
                S['gender'] = gender
                transition('prepare', 3.)
                return
            summary = S['runtime'].get_debug_summary()
            if 'managed=0 ' not in summary:
                if entered-S['due'] > 12:
                    raise RuntimeError('Cleanup did not settle: '+summary)
                return
            name, path, mesh = ITEMS[item]
            start = time.perf_counter()
            S['klass'] = unreal.load_class(None, path+'.'+path.rsplit('/',1)[1]+'_C')
            assert S['klass'], path
            S['class_load_s'] = time.perf_counter()-start
            transition('equip', jitter)
        elif phase == 'equip':
            gender, repeat, item, jitter = TRIALS[S['index']]
            S['trial'] = dict(gender=gender, repeat=repeat, garment=ITEMS[item][0], phase_jitter_s=jitter,
                              class_load_s=S['class_load_s'], events=[], callback_gaps_s=[], observer_cost_s=[])
            S['t0'] = time.perf_counter()
            S['game0'] = unreal.GameplayStatics.get_time_seconds(S['world'])
            S['last_sample'] = S['t0']
            S['frame0'] = S['frame']
            S['equipment'].add_item_to_inventory_by_class(S['klass'], 1, True)
            S['trial']['equip_call_s'] = time.perf_counter()-S['t0']
            S['notified'] = False
            S['last_key'] = None
            transition('observe')
        elif phase == 'observe':
            t = time.perf_counter()-S['t0']
            tr = S['trial']
            tr['callback_gaps_s'].append(entered-S['last_sample'])
            S['last_sample'] = entered
            garment = find_garment()
            summary = S['runtime'].get_debug_summary()
            state = str(S['runtime'].get_garment_runtime_state(garment)) if garment else 'NO_COMPONENT'
            key = (state, summary)
            if key != S['last_key']:
                tr['events'].append(dict(wall_s=t, game_s=unreal.GameplayStatics.get_time_seconds(S['world'])-S['game0'],
                                         frames=S['frame']-S['frame0'], state=state, summary=summary))
                S['last_key'] = key
            if garment and 'component_s' not in tr:
                tr['component_s'] = t
                tr['component'] = garment.get_path_name()
            if garment and MODE == 'notify' and not S['notified']:
                S['runtime'].notify_equipment_changed()
                tr['notification_s'] = time.perf_counter()-S['t0']
                S['notified'] = True
            if 'managed=1 ' in summary and 'managed_s' not in tr:
                tr['managed_s'] = t
            if 'WARMING' in state.upper() and 'warming_s' not in tr:
                tr['warming_s'] = t
            if 'READY' in state.upper():
                tr['ready_s'] = t
                tr['native_bridge'] = S['bridge'].get_debug_summary() if S['bridge'] else None
                tr['ready_game_s'] = unreal.GameplayStatics.get_time_seconds(S['world'])-S['game0']
                tr['frames'] = S['frame']-S['frame0']
                R['trials'].append(tr)
                write()
                unequip()
                S['index'] += 1
                transition('prepare', .1)
            elif t > 12:
                R['trials'].append(tr)
                raise RuntimeError('No Ready within 12s; inspect raw trial events: '+tr['garment']+' '+tr['gender'])
            tr['observer_cost_s'].append(time.perf_counter()-entered)
    except Exception:
        finish(traceback.format_exc())
    finally:
        S['busy'] = False


if __name__ != 'clothing_latency_support':
    write()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    S['callback'] = unreal.register_slate_post_tick_callback(tick)
