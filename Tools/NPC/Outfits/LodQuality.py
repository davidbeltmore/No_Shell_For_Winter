"""Bounded visual diagnostics on one disposable native outfit fixture pawn.

Mechanical PASS requires separate review of every image. Never an FPS fixture.
"""
import time
import unreal


class LodQuality:
    phases = (
        ('spawn_front_lod0', 1, 'front', False),
        ('extreme_front_lod0', 1, 'front', True),
        ('extreme_front_lod1', 2, 'front', True),
        ('extreme_side_lod1', 2, 'side', True),
        ('extreme_back_lod1', 2, 'back', True),
        ('automatic_far', 0, 'far', True),
        ('automatic_near', 0, 'near', True),
        ('moving_extreme_lod1', 2, 'front', True),
    )

    def __init__(self, body_id, output, case='all'):
        if case == 'distance':
            self.phases = tuple(p for p in self.phases if p[0] in ('spawn_front_lod0', 'automatic_far', 'automatic_near'))
        elif case == 'movement':
            self.phases = tuple(p for p in self.phases if p[0] in ('spawn_front_lod0', 'moving_extreme_lod1'))
        catalog = unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets')
        definition = next(r for r in catalog.bodies if str(r.preset_id) == body_id)
        self.body_path = definition.mesh.get_path_name()
        self.female = definition.gender == unreal.CharacterCreationGender.FEMALE
        self.weights = ({'Breasts Large': 1.85, 'Body Voluptuous': 1.0} if self.female
                        else {'Body Heavy': 1.0, 'Proportion Chest Size': .5})
        self.output = output
        self.records = []
        self.phase_started = None
        self.image_requested = None
        self.ready_at = None
        self.last_observation = None

    @property
    def complete(self):
        return len(self.records) == len(self.phases)

    @staticmethod
    def vector(v):
        return [v.x, v.y, v.z]

    def coverage(self, body):
        if self.female:
            index = body.get_material_index('Genesis9_GP_Torso')
            assert index >= 0, 'Female coverage material missing'
            assert not body.is_material_section_shown(index, 0), 'Female coverage not hidden'
            return {'material': 'Genesis9_GP_Torso', 'hidden': True}
        owner = body
        while owner.get_editor_property('leader_pose_component'):
            owner = owner.get_editor_property('leader_pose_component')
        hidden = {bone: owner.is_bone_hidden_by_name(bone) for bone in ('shaft_01', 'scrotum')}
        assert all(hidden.values()), 'Male coverage not hidden'
        return {'owner': owner.get_path_name(), 'bones_hidden': hidden}

    def tick(self, world):
        if self.complete:
            return
        pawns = [p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
                 if any(str(t).startswith('EF.AppearanceId.OutfitQA.') for t in p.tags)]
        if len(pawns) != 1:
            return
        pawn = pawns[0]
        outfit = pawn.get_component_by_class(unreal.ProjectNPCOutfitComponent)
        if outfit.state != unreal.ProjectNPCOutfitState.READY:
            return
        now = time.monotonic()
        if self.ready_at is None:
            self.ready_at = now
        # Let the native fixture finish its initial screenshot first.
        if now-self.ready_at < 1:
            return
        components = list(pawn.get_components_by_class(unreal.SkeletalMeshComponent))
        bodies = [m for m in components if m.is_visible() and m.get_skeletal_mesh_asset()
                  and m.get_skeletal_mesh_asset().get_path_name() == self.body_path]
        assert len(bodies) == 1, 'Expected exactly one visible preset body'
        body = bodies[0]
        garments = [m for m in components if m.get_skeletal_mesh_asset()
                    and m.get_skeletal_mesh_asset().get_name() in ('RagShirt', 'RagPants', 'RagFootWraps')]
        assert len(garments) == 3, 'Expected three native FullRag garments'
        label, forced, view, extreme = self.phases[len(self.records)]
        camera = unreal.GameplayStatics.get_player_controller(world, 0).get_view_target()
        assert isinstance(camera, unreal.CameraActor), 'Expected disposable native fixture camera'
        if self.phase_started is None:
            self.phase_started = now
            self.origin = pawn.get_actor_location()
            self.move_direction = pawn.get_actor_forward_vector()
            for mesh in garments:
                mesh.set_forced_lod(forced)
            if extreme:
                asset = body.get_skeletal_mesh_asset()
                data = unreal.AssetRegistryHelpers.get_asset_registry().get_asset_by_object_path(asset.get_path_name())
                names = str(data.get_tag_value('MorphTargetNames')).split(';')
                assert all(name in names for name in self.weights), 'Required morph fixture unavailable'
                for mesh in components:
                    if mesh.get_skeletal_mesh_asset() == asset:
                        for name, value in self.weights.items():
                            mesh.set_morph_target(name, value, False)
            if label.startswith('moving'):
                pawn.set_actor_enable_collision(True)
                movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
                # Native crowd fixtures may have no controller. Enable physical
                # locomotion only on this disposable pawn; this is not AI QA.
                if not pawn.get_controller():
                    movement.set_editor_property('run_physics_with_no_controller', True)
                movement.set_movement_mode(unreal.MovementMode.MOVE_WALKING)
        elapsed = now-self.phase_started
        if elapsed >= 10:
            raise AssertionError(label+' did not settle within ten seconds: '+str(self.last_observation))
        if label.startswith('moving') and self.image_requested is None:
            pawn.add_movement_input(self.move_direction, .5, True)
        target = pawn.get_actor_location()
        direction = pawn.get_actor_forward_vector()
        distance = {'far': 2000, 'near': 130}.get(view, 280)
        if view == 'far': direction = direction*.8+pawn.get_actor_right_vector()*.6
        if view == 'side': direction = pawn.get_actor_right_vector()
        if view == 'back': direction = -direction
        eye = target+direction*distance+unreal.Vector(0, 0, 10)
        camera.set_actor_location_and_rotation(eye, unreal.MathLibrary.find_look_at_rotation(eye, target), False, True)
        summary = pawn.get_component_by_class(unreal.EFClothingMorphV3RuntimeComponent).get_debug_summary()
        lods = {m.get_skeletal_mesh_asset().get_name(): m.get_predicted_lod_level() for m in garments}
        self.last_observation = {'phase': label, 'actual_lods': lods, 'summary': summary,
                                 'camera': self.vector(camera.get_actor_location()),
                                 'radius_scale': unreal.SystemLibrary.get_console_variable_float_value('r.SkeletalMeshLODRadiusScale'),
                                 'lod_bias': unreal.SystemLibrary.get_console_variable_int_value('r.SkeletalMeshLODBias')}
        if label.startswith('moving'):
            movement = pawn.get_component_by_class(unreal.CharacterMovementComponent)
            self.last_observation['movement'] = {
                'mode': str(movement.get_editor_property('movement_mode')),
                'max_walk_speed': movement.get_editor_property('max_walk_speed'),
                'tick_enabled': movement.is_component_tick_enabled(),
                'run_without_controller': movement.get_editor_property('run_physics_with_no_controller'),
                'controller': str(pawn.get_controller()),
                'location': self.vector(pawn.get_actor_location()),
                'velocity': self.vector(pawn.get_velocity()),
            }
        ready = 'managed=3 ready=3 warming=0 passthrough=0 issues=0' in summary
        ready = ready and all(m.is_visible() and f'{m.get_name()}:Ready[gLOD={m.get_predicted_lod_level()},' in summary for m in garments)
        if forced: ready = ready and all(lod == forced-1 for lod in lods.values())
        if view == 'far': ready = ready and all(lod == 1 for lod in lods.values())
        if view == 'near': ready = ready and any(lod == 0 for lod in lods.values())
        if elapsed > 8 and not ready and not getattr(self, 'failure_image_requested', False):
            self.failure_image_requested = True
            self.screenshot(world, self.output/(label+'_unsettled.png'))
        settle = 1.2 if label.startswith('moving') else (4 if extreme else 1)
        if elapsed < settle or not ready:
            return
        observed_weights = {name: body.get_morph_target(name) for name in self.weights}
        if extreme:
            assert all(abs(observed_weights[name]-value) < .001 for name, value in self.weights.items()), 'Morph weights changed during diagnostic'
        coverage = self.coverage(body)
        delta = pawn.get_actor_location()-self.origin
        distance_moved = (delta.x**2+delta.y**2)**.5
        if label.startswith('moving'):
            assert distance_moved > 10, 'Movement did not displace the fixture pawn: '+str(self.last_observation['movement'])
        path = self.output/(label+'.png')
        if self.image_requested is None:
            self.image_requested = now  # Before screenshot: Slate can re-enter.
            self.pending = {'phase': label, 'forced_lod': forced, 'actual_lods': lods,
                            'body': self.body_path, 'body_lod': body.get_predicted_lod_level(),
                            'morphs': observed_weights, 'coverage': coverage, 'summary': summary,
                            'seconds': elapsed, 'movement_xy_cm': distance_moved,
                            'velocity_cm_s': self.vector(pawn.get_velocity()),
                            'relative_bones_cm': {name: self.vector(body.get_socket_location(name)-target)
                                                  for name in ('pelvis','foot_l','foot_r')},
                            'camera': self.vector(eye), 'target': self.vector(target),
                            'image': str(path), 'visual_status': 'PENDING'}
            self.screenshot(world, path)
            return
        if not path.exists() or path.stat().st_size == 0 or now-self.image_requested < .25:
            return
        self.records.append(self.pending)
        self.phase_started = None
        self.image_requested = None

    @staticmethod
    def screenshot(world, path):
        # Capture the actual viewport without a resolution change or additional
        # high-resolution render views that can select a different automatic LOD.
        unreal.SystemLibrary.execute_console_command(world, 'Shot filename="'+str(path).replace('\\', '/')+'" -nosuffix')
