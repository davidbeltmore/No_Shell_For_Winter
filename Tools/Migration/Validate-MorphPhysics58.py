"""Bounded real PIE checks for configured morph physics; no asset or save writes."""
import json,math,time,traceback
from pathlib import Path
import unreal

def run():
    out=Path(unreal.Paths.project_saved_dir()).resolve()/'Analysis/CompatibilityClosure/Continuation_20260930'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    pc=unreal.GameplayStatics.get_player_controller(world,0);pawn=pc.get_controlled_pawn()
    old_view=pc.get_view_target()
    camera=unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world,'/Script/Engine.CameraActor',pawn.get_actor_location()+unreal.Vector(260,130,50),unreal.Rotator(pitch=0,yaw=0,roll=0))
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),pawn.get_actor_location()+unreal.Vector(0,0,25)),False)
    pc.set_view_target_with_blend(camera)
    meshes=pawn.get_components_by_class(unreal.SkeletalMeshComponent)
    primary=next(m for m in meshes if m.get_name()=='CharacterMesh0')
    custom=pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent)
    driver=pawn.get_component_by_class(unreal.EFMorphPhysicsConstraintComponent)
    bones=['l_pectoral','r_pectoral','LeftGlute','RightGlute','LeftThighFat','RightThighFat','Belly']
    clock=next(x for x in unreal.ObjectIterator(unreal.ProjectDayCycleSubsystem) if x.get_path_name().startswith(world.get_path_name()))
    inputs=next(x for x in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if 'Default__' not in unreal.Object.get_path_name(x) and x.get_outer().get_world()==world)
    move=unreal.load_asset('/AscentCombatFramework/Input/ACFMovementInput')
    performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
    throttle=performance.get_editor_property('bThrottleCPUWhenNotForeground')
    performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
    report={'checks':[],'samples':[],'passed':False}
    state={'phase':'female','at':time.monotonic(),'started':time.monotonic(),'busy':False,'done':False}
    def check(name,value):
        report['checks'].append({'name':name,'passed':bool(value)})
        if not value:raise AssertionError(name)
    def shot(name):
        unreal.SystemLibrary.execute_console_command(world,'Shot SHOWUI filename="'+str(out/(name+'.png')).replace('\\','/')+'"')
    def limits():
        return {b:unreal.ConstraintInstanceBlueprintLibrary.get_linear_limits(primary.get_constraint_by_name(b,True))[-1] for b in bones}
    def verify(gender):
        asset=primary.get_editor_property('physics_asset_override')
        check(gender+' profile',asset is not None and asset.get_name()=='PA_'+gender+'_Morph')
        check(gender+' owns simulation','TargetMesh=CharacterMesh0' in driver.get_debug_summary())
        check(gender+' seven live simulated branches',all(primary.is_simulating_physics(b) for b in bones))
        check(gender+' applied debug values','<unavailable>' not in driver.get_debug_summary())
        values=limits()
        report.setdefault('profiles',{})[gender]=values
        check(gender+' finite configured limits',all(math.isfinite(v) and 1<=v<=7 for v in values.values()))
    def set_morph(value):
        for mesh in meshes:mesh.set_morph_target('Breasts Large',value)
        driver.refresh_from_current_morph_state()
    def phase(name):
        state.update(phase=name,at=time.monotonic())
    def finish(error=None):
        if state['done']:return
        state['done']=True
        inputs.inject_input_vector_for_action(move,unreal.Vector(),[],[])
        if unreal.GameplayStatics.is_game_paused(world):clock.toggle_player_pause()
        clock.request_time_speed(1)
        set_morph(0)
        custom.select_gender(unreal.CharacterCreationGender.FEMALE)
        performance.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
        pc.set_view_target_with_blend(old_view)
        camera.destroy_actor()
        if error:report['error']=error
        report['passed']=not error
        unreal.unregister_slate_post_tick_callback(state['handle'])
        (out/'morph_runtime.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    def tick(delta):
        if state['busy'] or state['done']:return
        state['busy']=True
        try:
            elapsed=time.monotonic()-state['at']
            if time.monotonic()-state['started']>120:raise TimeoutError(state['phase'])
            for b in bones:
                pos=primary.get_socket_location(b)
                check_values=[pos.x,pos.y,pos.z]
                if not all(math.isfinite(v) for v in check_values):raise AssertionError('Nonfinite bone '+b)
                if (pos-pawn.get_actor_location()).length()>250:raise AssertionError('Unbounded displacement '+b)
            if state['phase']=='female' and elapsed>2:
                verify('Female');shot('morph_integrated_female')
                set_morph(1);phase('positive')
            elif state['phase']=='positive' and elapsed>2:
                check('positive morph expands breast limit',abs(limits()['l_pectoral']-(2+5/1.5))<.06)
                set_morph(-1);phase('negative')
            elif state['phase']=='negative' and elapsed>2:
                check('negative morph reduces breast limit',abs(limits()['l_pectoral']-1.5)<.06)
                set_morph(0);check('creator selects Male',custom.select_gender(unreal.CharacterCreationGender.MALE));phase('male')
            elif state['phase']=='male' and elapsed>2:
                verify('Male');shot('morph_integrated_male')
                check('pause accepted',clock.toggle_player_pause())
                state['frozen']={b:primary.get_socket_location(b) for b in bones}
                state['game']=unreal.GameplayStatics.get_time_seconds(world);phase('pause')
            elif state['phase']=='pause' and elapsed>1:
                check('pause freezes game',unreal.GameplayStatics.get_time_seconds(world)==state['game'])
                check('pause freezes simulated bones',all((primary.get_socket_location(b)-state['frozen'][b]).length()<.001 for b in bones))
                check('resume accepted',clock.toggle_player_pause())
                state['start_location']=pawn.get_actor_location()
                phase('walk')
            elif state['phase']=='walk':
                inputs.inject_input_vector_for_action(move,unreal.Vector(0,1,0),[],[])
                if elapsed>4:
                    inputs.inject_input_vector_for_action(move,unreal.Vector(),[],[])
                    check('native movement moves player',(pawn.get_actor_location()-state['start_location']).length()>100)
                    verify('Male');phase('rest')
            elif state['phase']=='rest' and elapsed>2:
                check('creator returns Female',custom.select_gender(unreal.CharacterCreationGender.FEMALE));phase('return')
            elif state['phase']=='return' and elapsed>2:
                verify('Female');finish()
        except Exception:finish(traceback.format_exc())
        finally:state['busy']=False
    state['handle']=unreal.register_slate_post_tick_callback(tick)
run()
