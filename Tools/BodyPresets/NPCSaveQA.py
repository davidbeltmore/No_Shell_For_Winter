"""Real NPC pool generation and ALS disk round trip; run through RunNPCSaveQA.ps1."""
import json
import time
import traceback
from pathlib import Path
import unreal

out = Path(unreal.Paths.project_dir()) / 'Saved/BodyPresetsQA/npc-save.json'
state = {'phase': 'ready', 'started': time.monotonic(), 'since': time.monotonic(), 'checks': []}
delegates = []
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
perf = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
throttle = perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
catalog = unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets')
profiles = {str(r.preset_id): {str(p.morph_name) for p in r.morph_profile} for r in catalog.bodies}
prefix = 'EF.AppearanceId.BodyPresetQA.'

def check(name, passed):
    state['checks'].append({'name': name, 'passed': bool(passed)})
    if not passed: raise RuntimeError(name)

def phase(name): state.update(phase=name, since=time.monotonic())

def subsystem(world):
    gi = unreal.GameplayStatics.get_game_instance(world).get_path_name()
    cls = unreal.load_class(None, '/Script/AscentSaveSystem.ALSLoadAndSaveSubsystem')
    return next(x for x in unreal.ObjectIterator(cls) if x.get_path_name().startswith(gi))

def fixtures(world):
    found = {}
    for pawn in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn):
        key = next((str(t) for t in pawn.tags if str(t).startswith(prefix)), None)
        if key: found[key] = pawn
    return found

def snapshot(world):
    result = {}
    for key, pawn in fixtures(world).items():
        comp = pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent)
        assert comp, key + ' missing appearance component'
        appearance = comp.capture_current_state()
        body_id = str(comp.get_body_preset_id())
        assert body_id in profiles, key + ' missing body ID'
        morphs = {str(m.morph_name): round(float(m.value), 5) for m in appearance.morph_values
                  if str(m.morph_name) in profiles[body_id] and m.target == unreal.CharacterCustomizationTarget.BODY}
        color = appearance.skin_color
        result[key] = {'body': body_id, 'morphs': morphs, 'skin': [round(v, 5) for v in (color.r,color.g,color.b)]}
    return result

def finish(error=''):
    state['error'] = error
    state['passed'] = not error
    state['elapsed_seconds'] = time.monotonic() - state['started']
    out.write_text(json.dumps(state, indent=2))
    unreal.unregister_slate_post_tick_callback(handle)
    perf.set_editor_property('bThrottleCPUWhenNotForeground', throttle)
    if editor.is_in_play_in_editor(): unreal.EditorLevelLibrary.editor_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()

def saved(ok): state['saved'] = bool(ok)
def loaded(ok): state['loaded'] = bool(ok)

def tick(delta):
    try:
        if time.monotonic() - state['started'] > 180: raise RuntimeError('180-second watchdog: ' + state['phase'])
        world = worlds.get_game_world()
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
        if not pawn: return
        age = time.monotonic() - state['since']
        if state['phase'] == 'ready' and age > 6:
            check('Real HUB player', 'HUB' in world.get_path_name())
            state['player_before'] = str(pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent).get_body_preset_id())
            start = time.perf_counter()
            origin = pawn.get_actor_location() + unreal.Vector(2200, 0, 100)
            for i in range(12):
                gender = 'Male' if i < 8 else 'Female'
                path = f'/Game/_Game/Characters/{gender}/ACFDummyEnemyBP{gender}.ACFDummyEnemyBP{gender}_C'
                npc = unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world, path,
                    origin + unreal.Vector((i % 4) * 180, (i // 4) * 180, 0), unreal.Rotator())
                assert npc, path
                npc.set_editor_property('tags', list(npc.tags) + [prefix + str(i)])
                movement = npc.get_component_by_class(unreal.CharacterMovementComponent)
                if movement: movement.disable_movement()
                controller = npc.get_controller()
                if controller and isinstance(controller, unreal.AIController):
                    brain = controller.get_component_by_class(unreal.BrainComponent)
                    if brain: brain.stop_logic('Body preset QA')
            state['spawn_12_ms'] = (time.perf_counter() - start) * 1000
            phase('generated')
        elif state['phase'] == 'generated' and age > 8:
            state['before'] = snapshot(world)
            check('12 generated NPC appearances', len(state['before']) == 12)
            males = [v for k,v in state['before'].items() if int(k[len(prefix):]) < 8]
            females = [v for k,v in state['before'].items() if int(k[len(prefix):]) >= 8]
            check('Male pool includes both presets', {x['body'] for x in males} == {'Male.01','Male.02'})
            check('Female NPCs remain female', all(x['body'] == 'Female.01' for x in females))
            check('Same body has varied morph values', any(len({json.dumps(x['morphs'],sort_keys=True) for x in males if x['body']==body}) > 1 for body in ['Male.01','Male.02']))
            check('NPC generation leaves player selection intact', state['player_before'] == str(pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent).get_body_preset_id()))
            callback = unreal.OnSaveFinished(); callback.bind_callable(saved); delegates.append(callback)
            subsystem(world).save_game_world('BodyPresets_QA_World', callback, True, False, 'Isolated body preset persistence QA')
            phase('saving')
        elif state['phase'] == 'saving' and 'saved' in state:
            check('ALS disk save succeeds', state['saved'])
            state['save_seconds'] = age
            for npc in fixtures(world).values():
                npc.get_component_by_class(unreal.EFCharacterCustomizationComponent).select_body_preset('Female.01', True)
            callback = unreal.OnLoadFinished(); callback.bind_callable(loaded); delegates.append(callback)
            subsystem(world).load_game_world('BodyPresets_QA_World', callback, True)
            phase('loading')
        elif state['phase'] == 'loading' and 'loaded' in state:
            check('ALS disk load succeeds', state['loaded'])
            state['load_seconds'] = age
            phase('settle')
        elif state['phase'] == 'settle' and age > 5:
            state['after'] = snapshot(world)
            check('All stable identities survive real load', set(state['before']) == set(state['after']))
            check('Body, profile morphs and skin restore exactly', state['before'] == state['after'])
            finish()
    except Exception:
        finish(traceback.format_exc())

handle = unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
if not editor.is_in_play_in_editor(): editor.editor_request_begin_play()
print('Body preset NPC save QA scheduled, 180-second watchdog.')
