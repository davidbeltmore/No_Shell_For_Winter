"""Disposable HUB PIE ladder traversal at supported time speeds; no asset/map saves."""
import json,time,traceback
from pathlib import Path
import unreal
OUT=Path(unreal.Paths.project_saved_dir()).resolve()/'Analysis/CompatibilityClosure'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world and 'HUB' in world.get_path_name()
pc=unreal.GameplayStatics.get_player_controller(world,0);pawn=pc.get_controlled_pawn()
def spawn(kind,location):
    path=kind.get_path_name() if isinstance(kind,unreal.Class) else kind.static_class().get_path_name()
    return unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world,path,location,unreal.Rotator(pitch=0,yaw=0,roll=0))
platform=spawn(unreal.StaticMeshActor,unreal.Vector(-1300,0,175.5))
mesh=platform.get_component_by_class(unreal.StaticMeshComponent)
mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
assert mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
platform.set_actor_scale3d(unreal.Vector(4,4,3.51))
mesh.set_collision_profile_name('BlockAll')
mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
ladder=spawn(unreal.load_class(None,'/AscentCombatFramework/Blueprints/Actors/ACF_Ladder_BP.ACF_Ladder_BP_C'),unreal.Vector(-1100,0,0))
ladder.get_editor_property('HeightRef').set_world_location(unreal.Vector(-1205,0,355),False,False)
ladder.call_method('HeightCheck');assert ladder.get_editor_property('HasValidPlacement')
component=pawn.get_component_by_class(unreal.load_class(None,'/AscentCombatFramework/Blueprints/Components/ACFLadderComponentBP.ACFLadderComponentBP_C'))
input_candidates=[x for x in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if 'Default__' not in unreal.Object.get_path_name(x) and x.get_outer().get_world()==world]
assert len(input_candidates)==1,'Expected exactly one local player in this isolated PIE world'
inputs=input_candidates[0]
movement=unreal.load_asset('/AscentCombatFramework/Input/ACFMovementInput')
clock=next(x for x in unreal.ObjectIterator(unreal.ProjectDayCycleSubsystem) if x.get_path_name().startswith(world.get_path_name()))
performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle=performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
camera=spawn(unreal.CameraActor,unreal.Vector(-450,900,360))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(-1125,0,230)),False)
pc.set_view_target_with_blend(camera)
cases=[1.,2.,4.];report={'cases':[]};state={'phase':'start','next':time.monotonic(),'busy':False,'done':False}
def finish(error=None):
    if state['done']:return
    state['done']=True
    inputs.inject_input_vector_for_action(movement,unreal.Vector(),[],[])
    clock.request_time_speed(1.)
    performance.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
    if error:report['error']=error
    report['passed']=not error and len(report['cases'])==3
    (OUT/'ladder_time_speeds.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    unreal.unregister_slate_post_tick_callback(handle)
    (OUT/'close.request').write_text('ladder speed validation complete',encoding='utf8')
def tick(delta):
    if state['busy'] or state['done']:return
    state['busy']=True
    try:
        if time.monotonic()<state['next']:return
        if state['phase']=='start':
            if not cases:
                durations=[r['game_seconds'] for r in report['cases']]
                assert max(durations)-min(durations)<1.5,'Traversal differs by more than 1.5 simulation seconds'
                finish();return
            speed=cases.pop(0)
            pawn.set_actor_location(unreal.Vector(-1045,0,96),False,False)
            pawn.set_actor_rotation(unreal.Rotator(pitch=0,yaw=180,roll=0),False)
            pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=180,roll=0))
            assert clock.request_time_speed(speed),'Time restriction unexpectedly rejects isolated fixture'
            component.call_method('Set Ladder',(ladder,True,False))
            component.call_method('Start Ladder Climb',(False,))
            state.update(phase='climb',real=time.monotonic(),game=unreal.GameplayStatics.get_time_seconds(world),speed=speed,seen=False)
        else:
            climbing=component.get_editor_property('IsClimbing')
            state['seen']=state['seen'] or climbing
            inputs.inject_input_vector_for_action(movement,unreal.Vector(0,1,0),[],[])
            if time.monotonic()-state['real']>20:raise RuntimeError('Ladder timed out at speed '+str(state['speed']))
            mode=str(pawn.get_movement_component().get_editor_property('movement_mode'))
            if state['seen'] and not climbing and 'WALKING' in mode:
                position=pawn.get_actor_location()
                assert position.z>400,'Did not reach the platform'
                inputs.inject_input_vector_for_action(movement,unreal.Vector(),[],[])
                report['cases'].append({'speed':state['speed'],'game_seconds':unreal.GameplayStatics.get_time_seconds(world)-state['game'],'real_seconds':time.monotonic()-state['real'],'position':[position.x,position.y,position.z],'movement':mode,'effort_after':unreal.ProjectCompatibilityInspectionLibrary.get_physical_activity_intensity(pawn)})
                unreal.SystemLibrary.execute_console_command(world,'Shot SHOWUI filename="'+str(OUT/('ladder_speed_'+str(int(state['speed']))+'.png')).replace('\\','/')+'"')
                state.update(phase='start',next=time.monotonic()+1.)
    except Exception:finish(traceback.format_exc())
    finally:state['busy']=False
handle=unreal.register_slate_post_tick_callback(tick)
