"""One-time migration rebuild after disabling invoker-only generation; HUB only."""
import json,time,unreal
from pathlib import Path
rebuild_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert rebuild_world.get_path_name().startswith('/Game/_Game/Hub/HUB.')
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
rebuild_nav=unreal.NavigationSystemV1.get_navigation_system(rebuild_world)
assert rebuild_nav and 'Default__' not in rebuild_nav.get_path_name()
assert not rebuild_nav.get_editor_property('generate_navigation_only_around_navigation_invokers')
rebuild_start=time.monotonic()
unreal.SystemLibrary.execute_console_command(rebuild_world,'RebuildNavigation')
def rebuild_tick(dt):
    elapsed=time.monotonic()-rebuild_start
    if elapsed<1:return
    if elapsed>30:
        unreal.unregister_slate_post_tick_callback(rebuild_handle)
        raise RuntimeError('30-second HUB navigation rebuild ceiling')
    if rebuild_nav.is_navigation_being_built(rebuild_world):return
    meshes=unreal.GameplayStatics.get_all_actors_of_class(rebuild_world,unreal.RecastNavMesh)
    results=[]
    for mesh in meshes:
        for x,y in [(0,0),(1200,0),(0,-500),(1200,-500),(1000,-2000)]:
            point=rebuild_nav.project_point_to_navigation(rebuild_world,unreal.Vector(x,y,92),mesh,None,unreal.Vector(200,200,300))
            results.append({'nav':mesh.get_name(),'query':[x,y,92],'projected':None if point is None else [point.x,point.y,point.z]})
    passed=len(results)==10 and all(r['projected'] is not None for r in results)
    if passed:assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    (Path(unreal.Paths.project_dir())/'Saved/NPCLogic/navigation-rebuild.json').write_text(json.dumps({'status':'PASS' if passed else 'FAIL','seconds':elapsed,'queries':results},indent=2))
    unreal.unregister_slate_post_tick_callback(rebuild_handle)
    assert passed,results
rebuild_handle=unreal.register_slate_post_tick_callback(rebuild_tick)
