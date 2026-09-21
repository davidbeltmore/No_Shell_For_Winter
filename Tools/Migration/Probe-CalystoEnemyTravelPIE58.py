"""Read runtime AI, stage a visible-player encounter, then exercise floor advance.

Transient PIE only; never assigns AI targets or saves assets. This is a diagnostic,
not a replacement for real-door or packaged acceptance.
"""
import builtins
import json
import os
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
if ROOT != Path('D:/Projects UE5/NoShellForWinter').resolve():
    raise RuntimeError('Wrong target project')
OUT = ROOT / 'Saved/Migration/CalystoDungeonDirectorV7/EnemyTravel_20260913' / os.environ.get('CODEX_ENEMY_PROBE_NAME', 'Baseline.json')
if not OUT.resolve().is_relative_to(ROOT / 'Saved'):
    raise RuntimeError('Output must remain under Saved')
SEED = int(os.environ.get('CODEX_ENEMY_PROBE_SEED', '5738796534536664893'))
LE = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
DC = unreal.load_class(None, '/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem')
DOOR_CLASS = unreal.load_class(None, '/Script/EFProceduralACFURuntime.EFCalystoFloorDoor')
INTERACTION_CLASS = unreal.load_class(None, '/Script/AscentCombatFramework.ACFInteractionComponent')
START = time.monotonic()
S = {'phase': 'load', 'phase_time': START, 'samples': [], 'events': [], 'seed': SEED}
CALLBACK = None

def path(obj):
    return obj.get_path_name() if obj else ''

def call(obj, name, *args):
    try:
        return getattr(obj, name)(*args)
    except Exception as e:
        return 'PENDING: ' + str(e)

def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception as e:
        return 'PENDING: ' + str(e)

def vec(v):
    return [v.x, v.y, v.z]

def director(world):
    gi = unreal.GameplayStatics.get_game_instance(world)
    prefix = path(gi) + '.'
    return next((o for o in unreal.ObjectIterator(DC) if path(o).startswith(prefix)), None)

def phase(name):
    S['phase'] = name
    S['phase_time'] = time.monotonic()
    unreal.log('ENEMY_TRAVEL_PROBE_PHASE=' + name)

def enemies(world):
    return [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
            if a.actor_has_tag('EF.Calysto.Population.Category.Enemy')]

def interact_floor_door(world, d):
    doors = unreal.GameplayStatics.get_all_actors_of_class(world, DOOR_CLASS)
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    if len(doors) != 1 or not player:
        return False
    door = doors[0]
    interaction = player.get_component_by_class(INTERACTION_CLASS)
    if not interaction:
        return False
    key = str(d.get_current_floor()) + ':' + path(door)
    if S.get('positioned_door') != key:
        dest = door.get_actor_location() - door.get_actor_right_vector() * 90.0
        player.set_actor_location(dest, False, True)
        interaction.enable_detection(False)
        interaction.enable_detection(True)
        S['positioned_door'] = key
        S['door_selected_samples'] = 0
        return False
    interaction.refresh_interactions()
    best = interaction.get_current_best_interactable_actor()
    overlap = any(path(a) == path(door) for a in interaction.get_overlapping_actors())
    if path(best) != path(door) or not overlap:
        S['door_selected_samples'] = 0
        return False
    S['door_selected_samples'] += 1
    if S['door_selected_samples'] < 3:
        return False
    S['events'].append({'real_floor_door_interaction': path(door), 'floor': d.get_current_floor(),
                        'best': path(best), 'overlap': overlap, 'selection_samples': S['door_selected_samples']})
    interaction.interact('CalystoEnemyTravelProof')
    return True

def sample(world, d):
    player = unreal.GameplayStatics.get_player_pawn(world, 0)
    snap = d.get_snapshot()
    rows = []
    for a in enemies(world):
        c = a.get_controller()
        brain = c.get_component_by_class(unreal.BehaviorTreeComponent) if c else None
        bb = c.get_component_by_class(unreal.BlackboardComponent) if c else None
        components = a.get_components_by_class(unreal.ActorComponent)
        ccomponents = c.get_components_by_class(unreal.ActorComponent) if c else []
        row = {'actor': path(a), 'class': path(a.get_class()), 'pos': vec(a.get_actor_location()),
               'velocity': vec(a.get_velocity()), 'controller': path(c),
               'state': str(call(c, 'get_ai_state')), 'target': str(call(c, 'get_target')),
               'battle': call(c, 'is_in_battle'), 'command': call(c, 'is_executing_command'),
               'move_status': str(call(c, 'get_move_status')),
               'aggressive': prop(c, 'bIsAggressive'), 'team': str(call(a, 'get_combat_team')),
               'brain_running': call(brain, 'is_running'), 'brain_paused': call(brain, 'is_paused'),
               'brain_tick': call(brain, 'is_component_tick_enabled'),
               'bb_paused': call(bb, 'get_value_as_bool', 'Paused'),
               'bb_target': str(call(bb, 'get_value_as_object', 'TargetActor')),
               'bb_target_distance': call(bb, 'get_value_as_float', 'TargetActorDistance'),
               'components': [path(x) for x in components], 'controller_components': [path(x) for x in ccomponents],
               'distance_player': a.get_distance_to(player) if player else None}
        row['perception'] = []
        for pc in ccomponents:
            if isinstance(pc, unreal.AIPerceptionComponent):
                row['perception'].append({'path': path(pc), 'active': pc.is_active(),
                    'perceived': str(call(pc, 'get_currently_perceived_actors', unreal.AISense_Sight)),
                    'config': str(prop(pc, 'senses_config'))})
        rows.append(row)
    S['samples'].append({'elapsed': time.monotonic() - START, 'game_time': unreal.GameplayStatics.get_time_seconds(world),
        'world': path(world), 'floor': d.get_current_floor(), 'epoch': d.get_run_epoch(),
        'state': str(prop(snap, 'State')), 'door_enabled': prop(snap, 'bDoorEnabled'),
        'failure': str(prop(snap, 'FailureReason')), 'player': path(player),
        'serial': prop(snap, 'GenerationSerial'), 'pcg_seed': prop(snap, 'PCGSeed'),
        'player_pos': vec(player.get_actor_location()) if player else [], 'enemies': rows})
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(S, indent=2, default=str), encoding='utf-8')

def finish(reason):
    global CALLBACK
    if CALLBACK is None:
        return
    unreal.unregister_slate_post_tick_callback(CALLBACK)
    CALLBACK = None
    S['result'] = reason
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(S, indent=2, default=str), encoding='utf-8')
    unreal.log('ENEMY_TRAVEL_PROBE_RESULT=' + reason)
    if LE.is_in_play_in_editor():
        unreal.EditorLevelLibrary.editor_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    try:
        now = time.monotonic()
        elapsed = now - S['phase_time']
        if now - START > 240:
            finish('TIMEOUT: ' + S['phase']); return
        if S['phase'] == 'load':
            phase('start'); LE.load_level('/Game/_Game/Hub/HUB'); return
        if S['phase'] == 'start':
            if elapsed > 2:
                phase('wait_pie'); LE.editor_request_begin_play()
            return
        world = unreal.EditorLevelLibrary.get_game_world()
        if not world or not LE.is_in_play_in_editor():
            return
        d = director(world)
        if not d:
            return
        name = world.get_name().lower()
        if S['phase'] == 'wait_pie':
            phase('preload'); return
        if S['phase'] == 'preload':
            if elapsed > 65:
                if not d.request_start_new_run_with_seed(SEED):
                    finish('START_REJECTED'); return
                phase('floor1')
            return
        if S['phase'] == 'floor1':
            if elapsed > 45 and 'hub' in name and not d.is_travel_request_pending():
                finish('START_TRAVEL_FAILED'); return
            if 'dungeongeneration' in name and prop(d.get_snapshot(), 'bDoorEnabled') is True:
                sample(world, d); phase('patrol')
            return
        if S['phase'] in ('patrol', 'encounter', 'floor2', 'floor3'):
            if now - S.get('last_sample', 0) >= 1:
                sample(world, d); S['last_sample'] = now
            if 'dungeongeneration' not in name:
                finish('UNEXPECTED_HUB: ' + S['phase']); return
        if S['phase'] == 'patrol' and elapsed > 15:
            melee = next((a for a in enemies(world) if 'melee' in path(a).lower()), None)
            if not melee:
                S['events'].append('NO_MELEE'); phase('encounter'); return
            player = unreal.GameplayStatics.get_player_pawn(world, 0)
            dest = melee.get_actor_location() + melee.get_actor_forward_vector() * 600.0
            # Do not set a target, move the enemy, alter hostility or disable collision.
            navigation = unreal.NavigationSystemV1.get_navigation_system(world)
            nav = navigation.project_point_to_navigation(world, dest, None, None, unreal.Vector(250,250,300))
            if isinstance(nav, unreal.Vector):
                dest = nav + unreal.Vector(0,0,100)
            player.set_actor_location(dest, False, True)
            facing = unreal.MathLibrary.find_look_at_rotation(dest, melee.get_actor_location())
            player.set_actor_rotation(unreal.Rotator(0, facing.yaw, 0), True)
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            pc.set_control_rotation(unreal.Rotator(-10, facing.yaw, 0))
            S['events'].append({'encounter_enemy': path(melee), 'player_placed': vec(dest), 'nav_result': str(nav)})
            phase('encounter'); return
        if S['phase'] == 'encounter' and elapsed > 1 and not S.get('captured_encounter'):
            S['captured_encounter'] = True
            screenshot = str(OUT.with_suffix('')) + '_Encounter.png'
            unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, screenshot)
            S['events'].append({'encounter_screenshot': screenshot})
        if S['phase'] == 'encounter' and elapsed > 8:
            if interact_floor_door(world, d):
                phase('floor2')
            return
        if S['phase'] == 'floor2' and d.get_current_floor() == 2 and prop(d.get_snapshot(), 'bDoorEnabled') is True:
            if interact_floor_door(world, d):
                phase('floor3')
            return
        if S['phase'] == 'floor3' and d.get_current_floor() == 3 and prop(d.get_snapshot(), 'bDoorEnabled') is True:
            sample(world, d); finish('THREE_FLOORS_READY_AI_REQUIRES_REVIEW')
    except Exception:
        finish(traceback.format_exc())

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
CALLBACK = unreal.register_slate_post_tick_callback(tick)
builtins._enemy_travel_probe = tick
unreal.log('ENEMY_TRAVEL_PROBE_ARMED=' + str(OUT))
