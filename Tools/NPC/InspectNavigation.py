"""Read-only HUB bounds/config inspection, no navigation mutations."""
import json,unreal
from pathlib import Path
nav_report={'actors':[]}
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(a,(unreal.NavigationData,unreal.NavMeshBoundsVolume,unreal.PlayerStart)) or a.get_actor_label()=='Floor':
        origin,extent=a.get_actor_bounds(False)
        row={'name':a.get_name(),'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'location':str(a.get_actor_location()),'origin':str(origin),'extent':str(extent),'properties':{}}
        for k in ['runtime_generation','agent_radius','agent_height','cell_size','cell_height','can_be_main_nav_data','enable_drawing','supported_agents']:
            try: row['properties'][k]=str(a.get_editor_property(k))
            except Exception: pass
        if isinstance(a,unreal.StaticMeshActor):
            c=a.static_mesh_component
            row['mesh']={'asset':c.static_mesh.get_path_name(),'collision_profile':str(c.get_collision_profile_name()),'collision_enabled':str(c.get_collision_enabled()),'mobility':str(c.mobility)}
        nav_report['actors'].append(row)
nav_cdo=unreal.get_default_object(unreal.NavigationSystemV1)
nav_report['configuration']={}
for k in ['generate_navigation_only_around_navigation_invokers','auto_create_navigation_data','initial_building_locked','supported_agents']:
    try: nav_report['configuration'][k]=str(nav_cdo.get_editor_property(k))
    except Exception as exc: nav_report['configuration'][k]=str(exc)
(Path(unreal.Paths.project_dir())/'Saved/NPCLogic/navigation-editor.json').write_text(json.dumps(nav_report,indent=2))
print(json.dumps(nav_report))
