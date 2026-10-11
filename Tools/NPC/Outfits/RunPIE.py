"""Bounded visual/equipment pilot in the live editor. SaveGuard must surround the QA editor session.

This fixture freezes navigation for fit inspection; it does not certify native AI.
Use Tools/NPC/RunLogicPIE.py for interaction and pursuit.
"""
import argparse
import json
import time
import traceback
from pathlib import Path
import unreal

parser = argparse.ArgumentParser()
parser.add_argument('--preset', default='FullRag')
parser.add_argument('--body', default='')
parser.add_argument('--archetype', default='SkillGuy,MeleeEnemy')
parser.add_argument('--count', type=int, default=4)
parser.add_argument('--seed', type=int, default=731)
parser.add_argument('--timeout', type=float, default=120)
args = parser.parse_args(globals().get('NPC_OUTFIT_ARGS', []))
assert 1 <= args.count <= 50 and 1 <= args.timeout <= 120
root = Path(unreal.Paths.project_dir()).resolve()
out = root/'Saved/NPCOutfits/PIE'/str(time.time_ns())
out.mkdir(parents=True)
(root/'Saved/NPCOutfits/latest-pie.txt').write_text(str(out), encoding='utf-8')
started = time.monotonic()
report = {'status': 'PENDING', 'parameters': vars(args), 'checks': [], 'errors': [], 'actors': [], 'visual': 'PENDING'}
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.is_in_play_in_editor()
perf = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle = perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
state = {'stage': 'spawn', 'pawns': [], 'busy': False, 'camera': None, 'old_view': None, 'due': 0}

def check(name, ok):
    report['checks'].append({'name': name, 'status': 'PASS' if ok else 'FAIL'})
    if not ok: raise AssertionError(name)

def done(error=''):
    unreal.unregister_slate_post_tick_callback(handle)
    if error: report['errors'].append(error)
    report['status'] = 'FAIL' if error else 'PASS'
    report['seconds'] = time.monotonic()-started
    (out/'runtime.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.EditorLevelLibrary.editor_end_play()
    perf.set_editor_property('bThrottleCPUWhenNotForeground', old_throttle)
    print('NPCOutfits '+report['status']+': '+str(out))

def tick(dt):
    if state['busy']: return
    state['busy'] = True
    try:
        now = time.monotonic()
        if now-started > args.timeout: raise TimeoutError('Internal PIE deadline exceeded: '+state['stage'])
        world = worlds.get_game_world()
        player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
        if not player or now < state['due']: return
        if state['stage'] == 'spawn':
            roster = unreal.load_asset('/Game/_Game/Characters/Data/DA_NPCRoster')
            selected = [row for row in roster.archetypes if str(row.id) in args.archetype.split(',')]
            check('Requested archetypes exist', len(selected) == len(args.archetype.split(',')))
            origin = player.get_actor_location()+unreal.Vector(1200, 0, 25)
            state['origin'] = origin
            for index in range(args.count):
                row = selected[(index//2) % len(selected)]
                gender = 'Male' if (args.body.startswith('Male') or not args.body and index%2 == 0) else 'Female'
                cls = row.male if gender == 'Male' else row.female
                transform = unreal.Transform(location=origin+unreal.Vector((index//4)*230, (index%4)*180, 0), rotation=unreal.Rotator(pitch=0, yaw=180, roll=0))
                pawn = unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world, cls.get_path_name(), transform.translation, unreal.Rotator(pitch=0, yaw=180, roll=0))
                pawn.tags = list(pawn.tags)+['EF.AppearanceId.OutfitQA.'+str(args.seed)+'.'+str(index)]
                pawn.set_actor_enable_collision(False)
                pawn.get_component_by_class(unreal.CharacterMovementComponent).disable_movement()
                controller = pawn.get_controller()
                if controller:
                    brain = controller.get_component_by_class(unreal.BrainComponent)
                    if brain: brain.stop_logic('Bounded outfit fit fixture')
                state['pawns'].append(pawn)
            state['stage'] = 'ready'
            state['settle_started'] = time.monotonic()
            return
        if state['stage'] == 'ready':
            for pawn in state['pawns']:
                component = pawn.get_component_by_class(unreal.ProjectNPCOutfitComponent)
                if not component: return
                if component.state == unreal.ProjectNPCOutfitState.FAILED:
                    coordinator = pawn.get_component_by_class(unreal.EFEquipmentCoordinatorComponent)
                    equipment = pawn.get_component_by_class(unreal.ACFEquipmentComponent)
                    report['failure_details'] = {'owner': pawn.get_class().get_path_name(), 'coordinator': str(coordinator.get_last_message()) if coordinator else 'missing',
                                                 'slots': [str(t.get_editor_property('TagName')) for t in equipment.get_available_equipment_slot()]}
                    raise AssertionError(component.failure)
                if component.state != unreal.ProjectNPCOutfitState.READY: return
            for pawn in state['pawns']:
                component = pawn.get_component_by_class(unreal.ProjectNPCOutfitComponent)
                custom = pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent)
                equipment = pawn.get_component_by_class(unreal.ACFEquipmentComponent)
                entries = list(equipment.get_inventory())
                garments = [entry for entry in entries if entry.item_class.get_path_name().startswith('/Game/_Game/Clothes/Rags01/')]
                check(pawn.get_name()+' three rags', len(garments) == 3)
                check(pawn.get_name()+' quantities', all(entry.count == 1 for entry in garments))
                check(pawn.get_name()+' equipped', all(entry.is_equipped for entry in garments))
                check(pawn.get_name()+' preset', str(component.selected_id) == args.preset)
                report['actors'].append({'class': pawn.get_class().get_path_name(), 'body': str(custom.get_body_preset_id()),
                                         'ready_seconds': component.ready_seconds,
                                         'clothing': pawn.get_component_by_class(unreal.EFClothingMorphV3RuntimeComponent).get_debug_summary()})
                if args.body: check('Body swap '+pawn.get_name(), custom.select_body_preset(args.body, True))
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            state['old_view'] = pc.get_view_target()
            camera_pose = unreal.Transform(location=state['origin']+unreal.Vector(-850, 270, 180))
            camera = unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world, '/Script/Engine.CameraActor', camera_pose.translation, unreal.Rotator())
            focus = state['origin']+unreal.Vector(0, 270, 0)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), focus), False)
            pc.set_view_target_with_blend(camera, 0)
            state['camera'] = camera
            state['stage'] = 'capture'
            state['due'] = now+3
            return
        if state['stage'] == 'capture':
            if args.body:
                for pawn in state['pawns']:
                    custom = pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent)
                    check('Selected body retained', str(custom.get_body_preset_id()) == args.body)
                    clothing = pawn.get_component_by_class(unreal.EFClothingMorphV3RuntimeComponent)
                    for slot in pawn.get_components_by_class(unreal.ACFArmorSlotComponent):
                        if slot.skeletal_mesh_asset and clothing.get_garment_runtime_state(slot) != unreal.EFClothingMorphV3RuntimeState.READY: return
            image = out/'pilot.png'
            unreal.AutomationLibrary.take_high_res_screenshot(1600, 1000, str(image))
            state['stage'] = 'finish'; state['due'] = now+2
            return
        if state['stage'] == 'finish':
            check('Screenshot captured', (out/'pilot.png').is_file())
            done()
    except Exception:
        done(traceback.format_exc())
    finally:
        state['busy'] = False

handle = unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('NPCOutfits PIE scheduled; fresh runtime receipt required: '+str(out))
