"""Actual authored HUB/floor travel with an overburdened player and owned bag contents.

No parity fixture, readiness override, asset edits or automatic retries. This
checks equipment across the existing production travel APIs, not door input QA.
"""
import json
import os
import pathlib
import re
import time
import traceback
import unreal

OUT = pathlib.Path(unreal.Paths.project_saved_dir()) / 'EquipmentQA' / os.environ['EF_EQUIPMENT_QA_RUN']
OUT.mkdir(parents=True, exist_ok=True)
S = {'phase': 'load', 'start': time.perf_counter(), 'due': 0, 'busy': False}
R = {'status': 'PENDING', 'checks': [], 'scope': 'Authored runtime travel, equipped satchel and original GUIDs; no dungeon authoring changes'}
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LE = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

def write():
    (OUT / 'result.json').write_text(json.dumps(R, indent=2), encoding='utf-8')
    (OUT / 'progress.txt').write_text(S['phase'], encoding='utf-8')

def check(value, name):
    R['checks'].append({'name': name, 'pass': bool(value)})
    write()
    assert value, name

def advance(phase, delay=0.5):
    S.update(phase=phase, due=time.perf_counter()+delay)
    write()

def finish(error=None):
    R['status'] = 'FAIL' if error else 'PASS'
    if error:
        R['error'] = error
    write()
    unreal.unregister_slate_post_tick_callback(S['callback'])
    unreal.EditorLevelLibrary.editor_end_play()
    unreal.SystemLibrary.quit_editor()

def verify_player(world, label):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    check(pawn is not None, label + ': player is present')
    coordinator = unreal.EFEquipmentFunctionLibrary.get_equipment_coordinator(pawn)
    native = coordinator.get_native_equipment()
    game_mode = unreal.GameplayStatics.get_game_mode(world)
    R.setdefault('arrival_inventory', []).append({
        'label': label, 'pawn_class': pawn.get_class().get_path_name(),
        'authored_pawn_class': game_mode.get_editor_property('default_pawn_class').get_path_name(),
        'items': [{'class': i.get_editor_property('item_class').get_path_name(),
                   'guid': i.get_editor_property('item_guid').to_string(),
                   'count': i.get_editor_property('count')} for i in native.get_inventory()]})
    starter = unreal.load_class(None, '/Game/_Game/Items/Equipment/BP_StarterBag.BP_StarterBag_C')
    check(sum(i.get_editor_property('count') for i in coordinator.get_native_equipment().get_all_items_of_class_in_inventory(starter)) == 1,
          label + ': travel/possession retains exactly one starter Travel Bag')
    weight = coordinator.get_weight_summary()
    R.setdefault('player_observations', []).append({
        'label': label, 'world': world.get_path_name(),
        'expected_base_capacity': S['capacity'],
        'base_capacity': weight.get_editor_property('base_capacity'),
        'effective_weight': weight.get_editor_property('effective_weight'),
        'root_items': [{'guid': i.get_editor_property('item_guid').to_string(),
                        'class': i.get_editor_property('item_class').get_path_name(),
                        'equipped': i.get_editor_property('is_equipped')}
                       for i in coordinator.get_native_equipment().get_inventory()]})
    check(abs(weight.get_editor_property('base_capacity') - S['capacity']) < 0.000001, label + ': fractional base capacity survives travel')
    check(weight.get_editor_property('overburdened'), label + ': legitimate overload is retained')
    check(any(i.get_editor_property('item_guid').to_string() == S['bag'].to_string() and i.get_editor_property('is_equipped') for i in coordinator.get_native_equipment().get_inventory()), label + ': same satchel GUID remains equipped')
    children = coordinator.get_container_items(S['bag'])
    check(len(children) == 1 and children[0].get_editor_property('item_guid').to_string() == S['child'].to_string(), label + ': contained GUID has one owner')
    check(any(v.get_editor_property('reserved') for v in coordinator.get_slot_views()), label + ': Bikini reservation is rebuilt')
    return pawn

def verify_resolved_chests(world, floor):
    """Optional chest regression on actual committed Calysto population."""
    if not OUT.name.startswith('ProceduralChestCalysto'):
        return
    records = []
    def contents(stock):
        return sorted((i.get_editor_property('item_guid').to_string(),
                       i.get_editor_property('item_class').get_path_name(),
                       i.get_editor_property('count')) for i in stock.get_inventory())
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        stock = actor.get_component_by_class(unreal.ProjectChestInventoryComponent)
        if not stock:
            continue
        before = contents(stock)
        check(stock.has_generated_contents(), 'Calysto floor %d: resolved chest initialized before opening' % floor)
        lock = actor.get_component_by_class(unreal.ProjectLockpickableComponent)
        if lock:
            lock.set_locked(False)
        stock.ensure_contents()
        check(contents(stock) == before, 'Calysto floor %d: opening preserves resolved GUIDs and quantities' % floor)
        for entry in list(stock.get_inventory()):
            stock.remove_item(entry, entry.get_editor_property('count'))
        stock.ensure_contents()
        check(stock.has_generated_contents() and not stock.get_inventory(),
              'Calysto floor %d: exhausted external manifest never regenerates' % floor)
        records.append({'actor': actor.get_path_name(), 'before': before})
    R.setdefault('resolved_chests', []).extend(records)
    write()

def tick(dt):
    if S['busy']:
        return
    S['busy'] = True
    try:
        now = time.perf_counter()
        if now-S['start'] > 180:
            raise RuntimeError('Equipment travel deadline exceeded in ' + S['phase'])
        if now < S['due']:
            return
        phase = S['phase']
        if phase == 'load':
            if ED.get_editor_world().get_name() != 'HUB':
                LE.load_level('/Game/_Game/Hub/HUB')
            LE.editor_request_begin_play()
            advance('setup', 4)
        elif phase == 'setup':
            world = ED.get_game_world()
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            if not pawn:
                return
            # Native inventory/start-bag timers need gameplay frames, not only
            # wall time consumed by synchronous Editor asset loading. Do this
            # before adding a fixture bag, which correctly suppresses a grant.
            game_time = unreal.GameplayStatics.get_time_seconds(world)
            if 'pawn_ready_time' not in S:
                S['pawn_ready_time'] = game_time
            if game_time - S['pawn_ready_time'] < 1.0:
                return
            coordinator = unreal.EFEquipmentFunctionLibrary.get_equipment_coordinator(pawn)
            native = coordinator.get_native_equipment()
            ids = []
            for path in ('/Game/_Game/Items/Equipment/BP_Satchel2', '/Game/_Game/Clothes/UnderWear01/Panty', '/Game/_Game/Clothes/Bikini01/Bikini'):
                cls = unreal.load_class(None, path + '.' + path.rsplit('/', 1)[-1] + '_C')
                entries = native.get_all_items_of_class_in_inventory(cls)
                if not entries:
                    native.add_item_to_inventory_by_class(cls, 1, False)
                    entries = native.get_all_items_of_class_in_inventory(cls)
                check(bool(entries), 'Travel fixture owns ' + path.rsplit('/', 1)[-1])
                ids.append(entries[0].get_editor_property('item_guid'))
            S['bag'], S['child'], bikini = ids
            check(coordinator.request_equip(bikini, unreal.GameplayTag()).get_editor_property('success'), 'Travel fixture equips Bikini')
            check(coordinator.request_equip(S['bag'], unreal.GameplayTag()).get_editor_property('success'), 'Travel fixture equips Satchel')
            check(coordinator.move_to_container(S['child'], S['bag'], 1).get_editor_property('success'), 'Travel fixture transfers unique content into Satchel')
            S['capacity'] = coordinator.get_weight_summary().get_editor_property('effective_weight') - 0.125
            coordinator.set_base_carry_capacity(S['capacity'])
            verify_player(world, 'HUB before travel')
            instance = unreal.GameplayStatics.get_game_instance(world)
            S['director'] = next(obj for obj in unreal.ObjectIterator(unreal.EFCalystoDirectorSubsystem) if obj.get_outer() == instance)
            R['run_seed'] = 202609210058
            check(S['director'].request_start_new_run_with_seed(R['run_seed']), 'Existing authored runtime accepts a new floor request')
            advance('floor_1')
        elif phase in ('floor_1', 'floor_2'):
            snapshot = S['director'].get_snapshot()
            value = snapshot.get_editor_property('state')
            state = re.sub(r'[^a-z]', '', str(getattr(value, 'name', value)).split('.')[-1].lower())
            if state in ('failed', 'cancelled'):
                R['director_diagnostics'] = json.loads(S['director'].get_diagnostics_json())
                raise RuntimeError('Existing floor runtime did not accept the floor: ' + str(snapshot.get_editor_property('failure_message')))
            if state != 'ready':
                return
            floor = 1 if phase == 'floor_1' else 2
            check(snapshot.get_editor_property('last_committed_floor_number') == floor, 'Existing runtime committed floor ' + str(floor))
            world = ED.get_game_world()
            verify_player(world, 'Floor ' + str(floor))
            verify_resolved_chests(world, floor)
            R.setdefault('floors', []).append(json.loads(S['director'].get_diagnostics_json()))
            if floor == 1:
                check(S['director'].request_advance_floor(), 'Existing runtime accepts next-floor travel')
                advance('floor_2')
            else:
                check(S['director'].request_return_to_hub(), 'Existing runtime accepts HUB return')
                advance('hub_return')
        elif phase == 'hub_return':
            world = ED.get_game_world()
            if not world or 'HUB' not in world.get_name() or S['director'].is_travel_request_pending():
                return
            advance('hub_verify', 6)
        elif phase == 'hub_verify':
            verify_player(ED.get_game_world(), 'HUB after travel')
            if OUT.name.startswith('ProceduralChestCalysto'):
                check(bool(R.get('resolved_chests')), 'Authored two-floor run exercised at least one real resolved chest')
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        S['busy'] = False

write()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['callback'] = unreal.register_slate_post_tick_callback(tick)
