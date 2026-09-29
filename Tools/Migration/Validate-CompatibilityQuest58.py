"""Run in an existing SaveGuard-owned HUB PIE session; never saves map/assets.

Native objective completion interfaces exercise the tutorial's state machine;
the follow objective must finish through actual navigation, without debug skipping.
"""
import json, time, traceback
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.project_saved_dir()).resolve()/'Analysis/CompatibilityClosure'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world and 'HUB' in world.get_path_name()
pc=unreal.GameplayStatics.get_player_controller(world,0)
pawn=pc.get_controlled_pawn()
qm=pc.get_component_by_class(unreal.AQSQuestManagerComponent)
stats=pawn.get_component_by_class(unreal.ACFGASStatisticsComponent)
def tag(name):
    result=unreal.GameplayTag();result.import_text('(TagName="'+name+'")');return result
def spawn(kind,location):
    path=kind.get_path_name() if isinstance(kind,unreal.Class) else kind.static_class().get_path_name()
    return unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world,path,location,unreal.Rotator(pitch=0,yaw=0,roll=0))
def position(actor):
    p=actor.get_actor_location();return [p.x,p.y,p.z]
qt=tag('Quest.LearnAboutACF')
quest=qm.get_quest_from_db(qt)
assert not qm.is_quest_in_progress_by_tag(qt)
report={'checks':[],'follow_samples':[],'completed':False,'xp_before':stats.get_total_exps_acquired()}
def check(name,value):
    report['checks'].append({'name':name,'pass':bool(value)})
    if not value:raise RuntimeError(name)
check('quest waits for missing targets',not qm.is_quest_in_progress_by_tag(qt))
guy=spawn(unreal.load_class(None,'/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/ACFGuy_BP.ACFGuy_BP_C'),unreal.Vector(500,500,96))
vendor=spawn(unreal.load_class(None,'/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/ACF_Vendor_BP.ACF_Vendor_BP_C'),unreal.Vector(900,750,96))
guy.spawn_default_controller();vendor.spawn_default_controller()
spline=spawn(unreal.load_class(None,'/AscentCombatFramework/Integrations/Actors/ACF_SplinePath_BP.ACF_SplinePath_BP_C'),unreal.Vector())
spline.set_spline_points([unreal.Vector(500,500,0),unreal.Vector(1100,500,0)])
nav=next(n for n in unreal.ObjectIterator(unreal.NavigationSystemV1) if unreal.Object.get_path_name(n).startswith(world.get_path_name()+':'))
navmesh=spawn(unreal.RecastNavMesh,unreal.Vector(800,500,0))
bounds=spawn(unreal.NavMeshBoundsVolume,unreal.Vector(800,500,0))
# Complete the reflected AddComponent operation synchronously on this PIE actor.
box=bounds.call_method('AddComponentByClass',(unreal.BoxComponent.static_class(),False,unreal.Transform(),False))
box.set_box_extent(unreal.Vector(1200,1000,300),True)
box.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
nav.on_navigation_bounds_updated(bounds)
nav.register_navigation_invoker(guy,2000.,2500.)
pawn.set_actor_location(unreal.Vector(500,350,96),False,False)
camera=spawn(unreal.CameraActor,unreal.Vector(800,-350,330))
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(800,500,100)),False)
camera.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view',65.)
pc.set_view_target_with_blend(camera)
performance=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
old_throttle=performance.get_editor_property('bThrottleCPUWhenNotForeground')
performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
started=time.monotonic();state={'phase':'navigation','at':started,'busy':False,'done':False}
quest_ids=[n.get_quest_objective().get_objective_id() for n in quest.get_all_nodes() if isinstance(n,unreal.AQSObjectiveNode)]
gi=unreal.GameplayStatics.get_game_instance(world)
save=next(x for x in unreal.ObjectIterator(unreal.ALSLoadAndSaveSubsystem) if x.get_path_name().startswith(gi.get_path_name()))
def phase(name):state.update(phase=name,at=time.monotonic())
def finish(error=None):
    if state['done']:return
    state['done']=True
    if error:report['error']=error
    report['passed']=not error and all(x['pass'] for x in report['checks'])
    performance.set_editor_property('bThrottleCPUWhenNotForeground',old_throttle)
    (OUT/'quest_end_to_end.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    unreal.unregister_slate_post_tick_callback(handle)
    (OUT/'close.request').write_text('quest fixture complete',encoding='utf8')
def on_loaded(success):
    try:
        check('native load completes',success)
        loaded_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        controller=unreal.GameplayStatics.get_player_controller(loaded_world,0)
        manager=controller.get_component_by_class(unreal.AQSQuestManagerComponent)
        loaded_stats=controller.get_controlled_pawn().get_component_by_class(unreal.ACFGASStatisticsComponent)
        check('completed quest survives load',manager.is_quest_completed_by_tag(qt))
        check('all four objective replays rejected',all(not manager.complete_objective_by_node_id(x) for x in quest_ids))
        check('completed quest cannot restart',not manager.start_quest(qt))
        check('load and replay add no XP',loaded_stats.get_total_exps_acquired()==report['xp_before'])
        report['completed']=True;finish()
    except Exception:finish(traceback.format_exc())
load_callback=unreal.OnLoadFinished();load_callback.bind_callable(on_loaded)
def on_saved(success):
    try:
        check('native save completes',success)
        # ALS broadcasts before releasing its busy flag. Start load on a later tick.
        phase('load_pending')
    except Exception:finish(traceback.format_exc())
save_callback=unreal.OnSaveFinished();save_callback.bind_callable(on_saved)
def complete_talk():
    active=list(quest.get_all_active_objectives())
    check('expected talk objective',len(active)==1 and 'TalkTo' in active[0].get_class().get_name())
    candidates=[a for a in [guy,vendor] if a.get_component_by_class(unreal.AQSQuestTargetComponent).is_currently_target_of_objective()]
    check('active target exists',bool(candidates))
    candidates[0].get_component_by_class(unreal.AQSQuestTargetComponent).complete_objective(active[0].get_objective_id())
def tick(delta):
    if state['busy'] or state['done']:return
    state['busy']=True
    try:
        if time.monotonic()-started>120:raise RuntimeError('Quest fixture timeout: '+state['phase'])
        age=time.monotonic()-state['at']
        if state['phase']=='navigation' and age>2:
            if not unreal.ProjectCompatibilityInspectionLibrary.is_navigation_fixture_ready(guy,navmesh,unreal.Vector(1100,500,0)):return
            check('transient path has navigation',True)
            check('all runtime quest references remapped',unreal.ProjectCompatibilityInspectionLibrary.remap_transient_quest_targets(quest,guy,vendor,spline))
            phase('ready')
        elif state['phase']=='ready' and qm.is_quest_in_progress_by_tag(qt):
            check('deferred quest starts with valid targets',True)
            complete_talk();phase('follow')
        elif state['phase']=='follow':
            active=list(quest.get_all_active_objectives())
            report['follow_samples'].append({'seconds':age,'guy':position(guy),'objectives':[o.get_class().get_name() for o in active]})
            if active and 'FollowNPC' in active[0].get_class().get_name():return
            check('native follow completed without objective skip',position(guy)[0]>850)
            complete_talk();phase('vendor')
        elif state['phase']=='vendor' and age>0.5:
            complete_talk()
            check('quest completed',qm.is_quest_completed_by_tag(qt))
            check('authored tutorial grants no XP',stats.get_total_exps_acquired()==report['xp_before'])
            unreal.SystemLibrary.execute_console_command(world,'Shot SHOWUI filename="'+str(OUT/'quest_final_completed.png').replace('\\','/')+'"')
            phase('saving');save.save_game_world('CompatibilityQuest_FinalQA',save_callback,True)
        elif state['phase']=='load_pending' and age>0.5:
            phase('loading');save.load_game_world('CompatibilityQuest_FinalQA',load_callback,True)
    except Exception:finish(traceback.format_exc())
    finally:state['busy']=False
handle=unreal.register_slate_post_tick_callback(tick)
