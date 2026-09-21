"""Real ACF inventory/equipment, body swaps, coverage ownership and visible GPU fitting.

Run through the existing CODEX_RUN_MIGRATION_BASELINE_PIE startup hook. Never saves
the level, Player, character state or source meshes. Reports below Saved only.
"""
import json
import os
import pathlib
import runpy
import time
import traceback
import unreal

OUT = pathlib.Path(os.environ.get('EF_UNISEX_OUTPUT_DIR', str(pathlib.Path(unreal.Paths.project_dir()).resolve() / 'Saved/ClothingUnisexQA/PIE')))
OUT.mkdir(parents=True, exist_ok=True)
LE = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ITEMS = [
    ('RagShirt', '/Game/_Game/Clothes/Rags01/RagShirt'),
    ('RagPants', '/Game/_Game/Clothes/Rags01/RagPants'),
    ('RagFootWraps', '/Game/_Game/Clothes/Rags01/RagFootWrap'),
    ('UnderWearBra_Female', '/Game/_Game/Clothes/UnderWear01/Bra'),
    ('UnderWearPanty_Female', '/Game/_Game/Clothes/UnderWear01/Panty'),
    ('UnderWearBikini_Female', '/Game/_Game/Clothes/Bikini01/Bikini'),
]
S = {'phase': 'load', 'start': time.monotonic(), 'due': 0, 'index': 0, 'gender_index': 0,
     'report': {'status': 'PENDING', 'checks': [], 'screenshots': [], 'saved_assets': False}}
(OUT/'result.json').write_text(json.dumps(S['report']), encoding='utf-8')


def prop(obj, key):
    return obj.get_editor_property(key)


def component(fragment):
    return next((c for c in S['player'].get_components_by_class(unreal.ActorComponent)
                 if fragment in c.get_class().get_name()), None)


def current_body():
    expected = f'/Game/DazToUnreal/{S["gender"]}/{S["gender"]}.{S["gender"]}'
    candidates = [c for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
                  if c.is_visible() and c.get_skeletal_mesh_asset()
                  and c.get_skeletal_mesh_asset().get_path_name() == expected]
    assert len(candidates) == 1, 'Body selection is not unique: '+expected
    return candidates[0]


def assert_coverage(hidden):
    body = current_body()
    if S['gender'] == 'Female':
        assert body.is_material_section_shown(body.get_material_index('Genesis9_GP_Torso'), 0) != hidden
    else:
        owner = body
        while prop(owner, 'leader_pose_component'):
            owner = prop(owner, 'leader_pose_component')
        for bone in ('shaft_01', 'scrotum'):
            assert owner.is_bone_hidden_by_name(bone) == hidden, bone+' coverage ownership'


def transition(phase, delay=1):
    S['phase'], S['due'] = phase, time.monotonic()+delay
    (OUT/'progress.txt').write_text(f'{phase} garment={S["index"]} gender={S["gender_index"]}')
    (OUT/'result.json').write_text(json.dumps(S['report'], indent=2), encoding='utf-8')


def finish(error=None):
    S['report']['status'] = 'FAIL' if error else 'PASS'
    if error:
        S['report']['error'] = error
    (OUT/'result.json').write_text(json.dumps(S['report'], indent=2), encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(S['callback'])
    unreal.EditorLevelLibrary.editor_end_play()
    unreal.SystemLibrary.quit_editor()


def set_gender():
    name = ('Female', 'Male', 'Female')[S['gender_index']]
    assert S['custom'].select_gender(getattr(unreal.CharacterCreationGender, name.upper()))
    S['gender'] = name
    S['runtime'].force_reconcile()
    transition('ready', 3)


def tick(dt):
    if S.get('busy'):
        return
    S['busy'] = True
    try:
        if time.monotonic()-S['start'] > 350:
            raise RuntimeError('Bounded unisex PIE timeout: '+S['phase'])
        if time.monotonic() < S['due']:
            return
        phase = S['phase']
        if phase == 'load':
            runpy.run_path(str(pathlib.Path(unreal.Paths.project_dir()) / 'Tools/ClothingMorphV4/Validate-EFClothingMorphV4Blueprints58.py'))
            S['report']['blueprint_compile'] = 'PASS'
            if ED.get_editor_world().get_name() != 'HUB':
                LE.load_level('/Game/_Game/Hub/HUB')
            transition('play', 2)
        elif phase == 'play':
            LE.editor_request_begin_play()
            transition('player', 5)
        elif phase == 'player':
            world = ED.get_game_world()
            player = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            if not player:
                return
            if unreal.GameplayStatics.get_time_seconds(world) < 2:
                return
            S.update(world=world, player=player)
            S['controller'] = unreal.GameplayStatics.get_player_controller(world, 0)
            S['custom'] = component('EFCharacterCustomizationComponent')
            S['runtime'] = component('EFClothingRuntimeComponent') or component('EFClothingMorphV3RuntimeComponent')
            S['equipment'] = component('ACFEquipmentComponent')
            if not (S['custom'] and S['runtime'] and S['equipment']):
                if time.monotonic()-S['due'] < 30:
                    return
                raise RuntimeError('Missing runtime components: '+str([
                    c.get_class().get_name() for c in player.get_components_by_class(unreal.ActorComponent)]))
            director = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
            S['rows'] = {str(prop(r, 'garment_id')): r for r in prop(director, 'garments')}
            camera_class = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectGameplayFreeCameraSubsystem')
            S['freecam'] = next((obj for obj in unreal.ObjectIterator(camera_class)
                                if obj.get_outer() == world), None)
            assert S['freecam'], 'Gameplay free-camera subsystem unavailable'
            assert S['freecam'].start_gameplay_free_camera()
            S['camera'] = S['controller'].get_view_target()
            for light in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.DirectionalLight):
                light.set_actor_rotation(unreal.Rotator(pitch=-35, yaw=player.get_actor_rotation().yaw+180, roll=0), False)
            # Remove all equipped items only in this transient test session.
            for item in list(prop(S['equipment'].get_current_equipment(), 'equipped_items')):
                S['equipment'].unequip_item_by_guid(prop(item, 'item_guid'))
            if os.environ.get('EF_UNISEX_EXTENDED_ONLY') == '1':
                S['index'] = len(ITEMS)
            transition('equip', 2)
        elif phase == 'equip':
            if S['index'] >= len(ITEMS):
                S['overlap_gender'] = 0
                transition('overlap_equip', .1)
                return
            name, path = ITEMS[S['index']]
            S['name'], S['row'] = name, S['rows'][name]
            klass = unreal.load_class(None, path+'.'+path.rsplit('/',1)[1]+'_C')
            assert klass, path
            S['equipment'].add_item_to_inventory_by_class(klass, 1, True)
            S['gender_index'] = 0
            set_gender()
        elif phase == 'ready':
            body = current_body()
            source = prop(S['row'], 'source_garment')
            meshes = [c for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
                      if c.get_skeletal_mesh_asset() == source and c.is_visible()]
            assert len(meshes) == 1, f'{S["name"]}: expected one equipped source component, got {len(meshes)}'
            garment = meshes[0]
            status = str(S['runtime'].get_garment_runtime_state(garment))
            summary = S['runtime'].get_debug_summary()
            if 'READY' not in status.upper():
                if time.monotonic()-S['due'] < 18:
                    return
                raise RuntimeError(f'{S["name"]} {S["gender"]} {status}: {summary}')
            covers = bool(prop(S['row'], 'covers_genitals'))
            if S['gender'] == 'Male':
                owner = body
                chain = []
                while owner:
                    chain.append({'component': owner.get_name(), 'mesh': owner.get_skeletal_mesh_asset().get_path_name(),
                                  'shaft_index': owner.get_bone_index('shaft_01'), 'scrotum_index': owner.get_bone_index('scrotum'),
                                  'shaft_parent': str(owner.get_parent_bone('shaft_01')),
                                  'shaft_position': str(owner.get_socket_location('shaft_01')),
                                  'shaft_parent_position': str(owner.get_socket_location(owner.get_parent_bone('shaft_01')))})
                    leader = prop(owner, 'leader_pose_component')
                    if not leader:
                        break
                    owner = leader
                S['report']['bone_pose_chain'] = chain
                assert owner.is_bone_hidden_by_name('shaft_01') == covers, 'shaft coverage: '+str(chain)
                assert owner.is_bone_hidden_by_name('scrotum') == covers, 'scrotum coverage: '+str(chain)
            else:
                slot = body.get_material_index('Genesis9_GP_Torso')
                assert body.is_material_section_shown(slot, 0) == (not covers), 'Golden Palace coverage'
            record = {'garment': S['name'], 'gender': S['gender'], 'source': source.get_path_name(),
                      'body': body.get_skeletal_mesh_asset().get_path_name(), 'state': status,
                      'covers_genitals': covers, 'summary': summary}
            deformer = prop(body, 'mesh_deformer')
            record['body_deformer_override'] = deformer.get_path_name() if deformer else None
            if S['gender'] == 'Male' and covers:
                assert deformer and deformer.get_name() == 'DG_EFBodyCoverageDQS', 'Missing visibility-aware body deformer'
            S['report']['checks'].append(record)
            # Consistent front view of the body including chest, pelvis and feet.
            location = S['player'].get_actor_location()
            forward = S['player'].get_actor_forward_vector()
            camera_location = location + forward*240 + unreal.Vector(0,0,15)
            target = location + unreal.Vector(0,0,-5)
            S['camera'].set_actor_location_and_rotation(camera_location,
                unreal.MathLibrary.find_look_at_rotation(camera_location,target), False, True)
            S['side'] = False
            transition('capture', 1)
        elif phase == 'capture':
            path = OUT/f'{S["index"]}_{S["name"]}_{S["gender_index"]}_{S["gender"]}{"_side" if S["side"] else ""}.png'
            if path.exists():
                path.unlink()
            unreal.AutomationLibrary.take_high_res_screenshot(1400, 1000, str(path))
            S['report']['screenshots'].append(str(path))
            S['capture_path'] = path
            transition('capture_wait', 1)
        elif phase == 'capture_wait':
            if not S['capture_path'].is_file():
                if time.monotonic()-S['due'] < 12:
                    return
                raise RuntimeError('Gameplay screenshot was not produced')
            if not S['side'] and S['gender_index'] < 2:
                S['side'] = True
                location = S['player'].get_actor_location()
                camera_location = location + S['player'].get_actor_right_vector()*220 + unreal.Vector(0,0,15)
                S['camera'].set_actor_location_and_rotation(camera_location,
                    unreal.MathLibrary.find_look_at_rotation(camera_location,location), False, True)
                transition('capture', .5)
            else:
                transition('next_gender', .1)
        elif phase == 'next_gender':
            S['gender_index'] += 1
            if S['gender_index'] < 3:
                set_gender()
            else:
                for item in list(prop(S['equipment'].get_current_equipment(), 'equipped_items')):
                    S['equipment'].unequip_item_by_guid(prop(item, 'item_guid'))
                S['runtime'].force_reconcile()
                transition('uncovered', 1)
        elif phase == 'uncovered':
            body = current_body()
            slot = body.get_material_index('Genesis9_GP_Torso')
            assert body.is_material_section_shown(slot, 0), 'Coverage ownership leaked after unequip'
            S['index'] += 1
            transition('equip', .1)
        elif phase == 'overlap_equip':
            S['gender'] = ('Male', 'Female')[S['overlap_gender']]
            assert S['custom'].select_gender(getattr(unreal.CharacterCreationGender, S['gender'].upper()))
            for path in (ITEMS[0][1], ITEMS[1][1]):
                klass = unreal.load_class(None, path+'.'+path.rsplit('/',1)[1]+'_C')
                S['equipment'].add_item_to_inventory_by_class(klass, 1, True)
            transition('overlap_prepare', 2)
        elif phase == 'overlap_prepare':
            # Reuse the real ACF chest component as a transient second lower
            # garment. Both lower items normally compete for the same slot.
            # This changes only this PIE component, never an asset or slot rule.
            # The equipment bridge correctly rejects meshes that disagree with
            # real inventory. Suspend only that adapter for this synthetic
            # two-owner fixture; all real equipment/morph cases keep it active.
            S['equipment_bridge'] = component('EFClothingEquipmentBridgeComponent')
            if S['equipment_bridge']:
                S['equipment_bridge'].set_component_tick_enabled(False)
                S['report']['synthetic_overlap_adapter_suspended'] = True
            source = prop(S['rows'][ITEMS[0][0]], 'source_garment')
            extra = next(c for c in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
                         if c.is_visible() and c.get_skeletal_mesh_asset() == source)
            extra.set_skeletal_mesh_asset(prop(S['rows'][ITEMS[4][0]], 'source_garment'))
            S['overlap_component'] = extra
            S['runtime'].force_reconcile()
            transition('overlap_two', 3)
        elif phase == 'overlap_two':
            items = list(prop(S['equipment'].get_current_equipment(), 'equipped_items'))
            assert len(items) == 2, 'Expected real ACF chest and lower-body items'
            assert 'READY' in str(S['runtime'].get_garment_runtime_state(S['overlap_component'])).upper()
            assert_coverage(True)
            S['report']['checks'].append({'gender': S['gender'], 'coverage_owners': 2, 'status': 'PASS'})
            S['equipment'].unequip_item_by_guid(prop(items[0], 'item_guid'))
            S['runtime'].force_reconcile()
            transition('overlap_one', 2)
        elif phase == 'overlap_one':
            assert_coverage(True)
            S['report']['checks'].append({'gender': S['gender'], 'coverage_owners': 1, 'status': 'PASS'})
            for item in list(prop(S['equipment'].get_current_equipment(), 'equipped_items')):
                S['equipment'].unequip_item_by_guid(prop(item, 'item_guid'))
            S['runtime'].force_reconcile()
            transition('overlap_zero', 2)
        elif phase == 'overlap_zero':
            assert_coverage(False)
            assert not prop(current_body(), 'mesh_deformer'), 'Body deformer override leaked after final unequip'
            S['report']['checks'].append({'gender': S['gender'], 'coverage_owners': 0, 'status': 'PASS'})
            if S.get('equipment_bridge'):
                S['equipment_bridge'].set_component_tick_enabled(True)
            S['overlap_gender'] += 1
            if S['overlap_gender'] < 2:
                transition('overlap_equip', .1)
            else:
                S['morph_gender'] = 0
                transition('morph_equip', .1)
        elif phase == 'morph_equip':
            S['gender'] = ('Female', 'Male')[S['morph_gender']]
            assert S['custom'].select_gender(getattr(unreal.CharacterCreationGender, S['gender'].upper()))
            for path in (ITEMS[0][1], ITEMS[1][1]):
                klass = unreal.load_class(None, path+'.'+path.rsplit('/',1)[1]+'_C')
                S['equipment'].add_item_to_inventory_by_class(klass, 1, True)
            body = current_body()
            asset = body.get_skeletal_mesh_asset()
            data = unreal.AssetRegistryHelpers.get_asset_registry().get_asset_by_object_path(asset.get_path_name())
            names = str(data.get_tag_value('MorphTargetNames')).split(';')
            weights = {'Breasts Large': 1.85, 'Body Voluptuous': 1.0} if S['gender'] == 'Female' else {'Body Heavy': 1.0, 'Proportion Chest Size': .5}
            assert all(name in names for name in weights), 'Required morph fixture unavailable'
            S['morph_weights'] = weights
            for mesh in S['player'].get_components_by_class(unreal.SkeletalMeshComponent):
                if mesh.get_skeletal_mesh_asset() == asset:
                    for name, value in weights.items():
                        mesh.set_morph_target(name, value, False)
            S['runtime'].force_reconcile()
            transition('morph_ready', 4)
        elif phase == 'morph_ready':
            body = current_body()
            assert all(abs(body.get_morph_target(name)-value) < .001 for name,value in S['morph_weights'].items())
            for name in ('RagShirt', 'RagPants'):
                source = prop(S['rows'][name], 'source_garment')
                meshes = [m for m in S['player'].get_components_by_class(unreal.SkeletalMeshComponent)
                          if m.is_visible() and m.get_skeletal_mesh_asset() == source]
                assert len(meshes) == 1
                assert 'READY' in str(S['runtime'].get_garment_runtime_state(meshes[0])).upper()
            assert_coverage(True)
            S['report']['checks'].append({'gender': S['gender'], 'morphs': S['morph_weights'],
                                         'state': 'Ready', 'summary': S['runtime'].get_debug_summary()})
            S['move_started'] = time.monotonic()
            S['move_origin'] = S['player'].get_actor_location()
            S['move_direction'] = S['player'].get_actor_forward_vector()
            transition('morph_movement', 0)
        elif phase == 'morph_movement':
            S['player'].add_movement_input(S['move_direction'], .5, True)
            if time.monotonic()-S['move_started'] < 1.2:
                return
            delta = S['player'].get_actor_location()-S['move_origin']
            distance = (delta.x*delta.x + delta.y*delta.y + delta.z*delta.z)**.5
            assert distance > 10, 'Movement fixture did not move the character'
            S['report']['checks'].append({'gender': S['gender'], 'moving_morph_distance_cm': distance, 'status': 'PASS'})
            location = S['player'].get_actor_location()
            camera_location = location + S['player'].get_actor_forward_vector()*240 + unreal.Vector(0,0,15)
            S['camera'].set_actor_location_and_rotation(camera_location,
                unreal.MathLibrary.find_look_at_rotation(camera_location,location), False, True)
            transition('morph_capture', 0)
        elif phase == 'morph_capture':
            path = OUT/f'morph_extreme_{S["gender"]}.png'
            if path.exists():
                path.unlink()
            unreal.AutomationLibrary.take_high_res_screenshot(1400, 1000, str(path))
            S['report']['screenshots'].append(str(path))
            S['capture_path'] = path
            transition('morph_done', 2)
        elif phase == 'morph_done':
            assert S['capture_path'].exists()
            for item in list(prop(S['equipment'].get_current_equipment(), 'equipped_items')):
                S['equipment'].unequip_item_by_guid(prop(item, 'item_guid'))
            S['morph_gender'] += 1
            if S['morph_gender'] < 2:
                transition('morph_equip', .5)
            else:
                finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        S['busy'] = False


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['callback'] = unreal.register_slate_post_tick_callback(tick)
