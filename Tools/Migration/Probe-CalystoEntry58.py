"""Bounded PIE evidence capture; does not edit or save Content packages.

Run from the existing target editor's Python console. Starts one known failing
V6 run via its public API, observes structural components for at most 40 seconds,
then stops only the PIE session this script owns. This is a reproduction probe,
not a traversal or V7 acceptance test. Set builtins.calysto_probe_case=0 or 1.
"""
import builtins
import json
import time
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
if ROOT != Path('D:/Projects UE5/NoShellForWinter').resolve():
    raise RuntimeError('Probe only supports the writable UE 5.8 target')
CASE = int(getattr(builtins, 'calysto_probe_case', 0))
SEEDS = [(2959332854660340481, 1779679224), (2930986289486100775, 1190737158)]
RUN_SEED, EXPECTED_TOPOLOGY = SEEDS[CASE]
OUTPUT = ROOT / 'Saved/Migration/CalystoDungeonDirectorV7' / ('EntryProbe_' + time.strftime('%Y%m%d_%H%M%S'))
OUTPUT.mkdir(parents=True, exist_ok=False)
EDITOR = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LEVEL = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if EDITOR.get_game_world():
    raise RuntimeError('Probe requires no pre-existing PIE; preserve user session')


def path(obj):
    return str(obj.get_path_name()) if obj else ''


def safe(fn):
    try:
        value = fn()
        return value if isinstance(value, (int, float, bool, str, list, dict)) or value is None else str(value)
    except Exception as exc:
        return {'PENDING': str(exc)}


def dirty():
    return sorted(path(p) for p in list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()) +
                  list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()))


STATE = {'status': 'OBSERVING', 'gameplay_verified': False, 'run_seed': RUN_SEED,
         'expected_topology_seed': EXPECTED_TOPOLOGY, 'dirty_before': dirty(),
         'samples': [], 'events': [], 'started': time.monotonic(), 'last_sample': 0,
         'requested': False, 'seen_dungeon': False, 'owned_pie': False, 'finished': False}
DIRECTOR_CLASS = unreal.load_class(None, '/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem')


def subsystem(world):
    instance = unreal.GameplayStatics.get_game_instance(world)
    if not instance:
        return None
    prefix = path(instance) + '.'
    for candidate in unreal.ObjectIterator(unreal.Object):
        try:
            if candidate.get_class() == DIRECTOR_CLASS and path(candidate).startswith(prefix):
                return candidate
        except Exception:
            continue
    return None


def persist():
    (OUTPUT / 'evidence.json').write_text(json.dumps(STATE, indent=2, default=str), encoding='utf-8')


def finish(reason):
    if STATE['finished']:
        return
    STATE['finished'] = True
    STATE['status'] = 'CAPTURED'
    STATE['stop_reason'] = reason
    STATE['elapsed_seconds'] = time.monotonic() - STATE['started']
    STATE['dirty_after'] = dirty()
    persist()
    unreal.unregister_slate_post_tick_callback(builtins.calysto_entry_probe_handle)
    if STATE['owned_pie'] and EDITOR.get_game_world():
        LEVEL.editor_request_end_play()
    unreal.log('CALYSTO_ENTRY_PROBE_CAPTURED ' + str(OUTPUT))


def sample(world, director):
    actors = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor))
    rows = []
    markers = []
    nav = []
    for actor in actors:
        cls = path(actor.get_class())
        if 'StartPoint' in cls or 'FloorDoor' in cls or 'EndPoint' in cls:
            location = actor.get_actor_location()
            markers.append({'actor': path(actor), 'class': cls, 'location': str(location),
                            'bounds': safe(lambda: actor.get_actor_bounds(False)),
                            'floor_trace': safe(lambda: unreal.SystemLibrary.line_trace_single(
                                world, location + unreal.Vector(0, 0, 100), location - unreal.Vector(0, 0, 500),
                                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [actor],
                                unreal.DrawDebugTrace.NONE, True)),
                            'nav_projection': safe(lambda: unreal.NavigationSystemV1.project_point_to_navigation(
                                world, location, None, None, unreal.Vector(100, 100, 250)))})
        if 'NavMesh' in cls or 'Recast' in cls:
            nav.append({'actor': path(actor), 'class': cls,
                        'bounds': safe(lambda: actor.get_actor_bounds(False)),
                        'components': [path(c) for c in actor.get_components_by_class(unreal.ActorComponent)]})
        if 'MassiveDungeon' not in cls and not actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            continue
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            if len(rows) >= 160:
                break
            mesh = component.get_editor_property('static_mesh')
            count = component.get_instance_count() if isinstance(component, unreal.InstancedStaticMeshComponent) else 1
            rows.append({'actor': path(actor), 'component': path(component), 'mesh': path(mesh), 'count': count,
                         'collision': safe(component.get_collision_enabled),
                         'profile': safe(component.get_collision_profile_name),
                         'nav_relevant': safe(lambda: component.get_editor_property('can_ever_affect_navigation')),
                         'bounds': safe(lambda: unreal.SystemLibrary.get_component_bounds(component)),
                         'materials': [path(m) for m in component.get_materials()],
                         'instance_transform_sample': [str(component.get_instance_transform(i, world_space=True))
                             for i in range(min(count, 8))] if isinstance(component, unreal.InstancedStaticMeshComponent) else []})
    STATE['samples'].append({'elapsed': time.monotonic() - STATE['started'], 'world': path(world),
                              'actors': len(actors), 'markers': markers, 'navigation': nav, 'structure': rows,
                              'snapshot': safe(lambda: director.get_snapshot().export_text()) if director else None,
                              'nav_building': safe(lambda: unreal.NavigationSystemV1.is_navigation_being_built(world))})
    persist()


def tick(delta):
    try:
        now = time.monotonic()
        if now - STATE['started'] > 40:
            finish('bounded observation deadline')
            return
        world = EDITOR.get_game_world()
        if not world:
            return
        director = subsystem(world)
        if not STATE['requested'] and director and now - STATE['started'] > 2:
            STATE['requested'] = True
            STATE['events'].append({'request_start_run': RUN_SEED,
                                    'accepted': bool(director.request_start_new_run_with_seed(RUN_SEED))})
            persist()
        dungeon = 'DungeonGeneration' in path(world)
        if dungeon:
            STATE['seen_dungeon'] = True
            if now - STATE['last_sample'] >= 0.5 and len(STATE['samples']) < 72:
                STATE['last_sample'] = now
                sample(world, director)
        elif STATE['seen_dungeon']:
            finish('observed return from DungeonGeneration to ' + path(world))
    except Exception:
        STATE['exception'] = traceback.format_exc()
        finish('probe exception')


builtins.calysto_entry_probe_handle = unreal.register_slate_post_tick_callback(tick)
STATE['owned_pie'] = True
unreal.log('CALYSTO_ENTRY_PROBE_ARMED; start standard PIE now. ' + str(OUTPUT))
