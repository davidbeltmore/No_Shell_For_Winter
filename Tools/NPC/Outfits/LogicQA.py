"""Run native pursuit in the current HUB's authored bounds, without editing the map.

Service interaction is a separate native mode. Preserve the prior completion
receipt before a focused retry; never interpret an old runtime.txt as a fresh pass.
"""
import argparse
import json
import runpy
import shutil
import time
from pathlib import Path
import unreal

parser=argparse.ArgumentParser()
parser.add_argument('--mode',choices=['ai','services','all'],default='ai')
parser.add_argument('--native',action='store_true',help='Use the existing native ACF comparison fixture')
parser.add_argument('--inspect-perception',action='store_true',help='Read-only sight/affiliation diagnostics during a failed native comparison')
parser.add_argument('--diagnose-listener-refresh',action='store_true',help='Diagnostic only: refresh the listener after the first sample; not an acceptance pass')
parser.add_argument('--diagnose-source-refresh',action='store_true',help='Diagnostic only: register the player as a sight source; not an acceptance pass')
args=parser.parse_args(globals().get('NPC_OUTFIT_ARGS',[]))
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/NPCOutfits/Logic'/str(time.time_ns())
out.mkdir(parents=True)
for source in ['Runtime/runtime.txt','Runtime/progress.txt','pie-host.json']:
    path=root/'Saved/NPCLogic'/source
    if path.exists(): shutil.copy2(path,out/('before-'+path.name))
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
volumes=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.NavMeshBoundsVolume)
assert len(volumes)==1, 'Choose an unambiguous authored navigation volume before pursuit QA'
origin,extent=volumes[0].get_actor_bounds(False)
assert extent.x>800 and extent.y>350, 'Authored bounds cannot contain the existing 1200 cm pursuit corridor'
arena=None
candidates=[]
# Sight uses ECC_Pawn in this project. Visibility alone can pass through a
# placed character's capsule. Find a clear corridor without moving those actors.
for fraction in [-0.65,0.65,-0.35,0.35,0]:
    x,y=origin.x-600,origin.y+extent.y*fraction
    hit=unreal.SystemLibrary.sphere_trace_single_by_profile(world,unreal.Vector(x,y,120),unreal.Vector(x+1200,y,120),100,'Pawn',False,[],unreal.DrawDebugTrace.NONE,True)
    data=hit.to_dict() if hit else {}
    candidates.append({'x':x,'y':y,'blocked_by':str(data.get('hit_actor')) if hit else None})
    if not hit:
        arena=(x,y)
        break
assert arena, 'No clear 1200 cm corridor inside the current navigation volume'
command='Project.NPCLogic.QA '+('' if args.mode=='all' else args.mode+' ')
if args.native: command+='native '
command+=f'arena_x={arena[0]:.3f} arena_y={arena[1]:.3f}'
(out/'invocation.json').write_text(json.dumps({'status':'PENDING','command':command,
    'volume':volumes[0].get_path_name(),'origin':[origin.x,origin.y,origin.z],
    'extent':[extent.x,extent.y,extent.z],'corridor_candidates':candidates,'prior_receipts':str(out)},indent=2),encoding='utf-8')
namespace=runpy.run_path(str(root/'Tools/NPC/RunLogicPIE.py'),init_globals={'NPC_LOGIC_COMMAND':command})
original_done=namespace['logic_done']
probe_handle=None
probe_samples=[]
probe_started=time.monotonic()

def probe_tick(dt):
    if len(probe_samples)>=2 or time.monotonic()-probe_started < (3 if not probe_samples else 9): return
    game=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    player=unreal.GameplayStatics.get_player_pawn(game,0) if game else None
    if not player: return
    sample={'seconds':time.monotonic()-probe_started,'player':player.get_path_name(),
            'player_location':str(player.get_actor_location()),'enemies':[]}
    try:
        sample['player_team']=str(player.get_combat_team().get_editor_property('TagName'))
        damage=player.get_component_by_class(unreal.ACFDamageHandlerComponent)
        sample['player_alive']=damage.get_is_alive() if damage else None
        sample['player_stimuli']=[]
        try: sample['world_players_only']=str(game.get_editor_property('bPlayersOnly'))
        except Exception: sample['world_players_only']='unexposed'
        for source in player.get_components_by_class(unreal.AIPerceptionStimuliSourceComponent):
            values={'component':source.get_path_name()}
            for key in ['auto_register_as_source','register_as_source_for_senses']:
                try: values[key]=str(source.get_editor_property(key))
                except Exception: pass
            sample['player_stimuli'].append(values)
        if args.diagnose_source_refresh and not probe_samples:
            sample['diagnostic_source_refresh']=unreal.AIPerceptionSystem.register_perception_stimuli_source(game,unreal.load_class(None,'/Script/AIModule.AISense_Sight'),player)
        for pawn in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Pawn):
            if pawn.get_class().get_name() not in ['ACFMeleeEnemyBPMale_C','ACFMeleeEnemyBP_C']: continue
            controller=pawn.get_controller()
            row={'pawn':pawn.get_path_name(),'location':str(pawn.get_actor_location()),
                 'rotation':str(pawn.get_actor_rotation()),'controller':str(controller)}
            row['team']=str(pawn.get_combat_team().get_editor_property('TagName'))
            team=pawn.get_component_by_class(unreal.ACFTeamComponent)
            if team: row['attitude_to_player']=str(team.get_attitude_towards_actor(player))
            if controller:
                row['controller_rotation']=str(controller.get_control_rotation())
                row['eyes']=str(controller.get_actor_eyes_view_point())
                eyes,_=controller.get_actor_eyes_view_point()
                trace=unreal.SystemLibrary.line_trace_single(game,eyes,player.get_actor_location(),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[pawn],unreal.DrawDebugTrace.NONE,True)
                row['visibility_trace_to_player_origin']=str(trace)
                pawn_hit=unreal.SystemLibrary.line_trace_single_by_profile(game,eyes,player.get_actor_location(),'Pawn',True,[pawn],unreal.DrawDebugTrace.NONE,True)
                row['pawn_profile_trace_to_player_origin']=str(pawn_hit.to_dict()) if pawn_hit else None
                row['line_of_sight_to_player']=controller.line_of_sight_to(player)
                for key in ['bIsAggressive','LoseTargetDistance']:
                    try: row[key]=str(controller.get_editor_property(key))
                    except Exception: pass
                threat=controller.get_component_by_class(unreal.ACFThreatManagerComponent)
                if threat:
                    row['player_potential_threat']=threat.is_actor_a_potential_threat(player)
                    row['player_default_threat']=threat.get_default_threat_for_actor(player)
                senses=controller.get_components_by_class(unreal.AIPerceptionComponent)
                row['senses']=[]
                for sense in senses:
                    row['sight_enabled']=sense.is_sense_enabled(unreal.load_class(None,'/Script/AIModule.AISense_Sight'))
                    row['player_perception']=str(sense.get_actors_perception(player))
                    row['perceived_sight']=[a.get_path_name() for a in sense.get_currently_perceived_actors(unreal.load_class(None,'/Script/AIModule.AISense_Sight'))]
                    for config in sense.get_editor_property('senses_config'):
                        data={'class':config.get_class().get_name()}
                        for key in ['implementation','sight_radius','lose_sight_radius','peripheral_vision_angle_degrees','detection_by_affiliation']:
                            try: data[key]=str(config.get_editor_property(key))
                            except Exception: pass
                        row['senses'].append(data)
                    if args.diagnose_listener_refresh and not probe_samples:
                        sense.request_stimuli_listener_update()
                        row['diagnostic_listener_refresh']=True
            sample['enemies'].append(row)
    except Exception as exc: sample['error']=str(exc)
    probe_samples.append(sample)
    (out/'perception.json').write_text(json.dumps(probe_samples,indent=2),encoding='utf-8')

if args.inspect_perception: probe_handle=unreal.register_slate_post_tick_callback(probe_tick)

def archive_done(error='', original=original_done):
    if probe_handle is not None: unreal.unregister_slate_post_tick_callback(probe_handle)
    original(error)
    host=json.loads((root/'Saved/NPCLogic/pie-host.json').read_text())
    shutil.copy2(root/'Saved/NPCLogic/pie-host.json',out/'host.json')
    native=root/'Saved/NPCLogic/Runtime/runtime.txt'
    # A timeout can leave the prior native receipt untouched. Preserve it only
    # as "before", rather than presenting it as this invocation's completion.
    if native.exists() and native.stat().st_mtime_ns != original.__globals__['logic_mtime']:
        shutil.copy2(native,out/'runtime.txt')
    diagnostic=args.diagnose_listener_refresh or args.diagnose_source_refresh
    (out/'runtime.json').write_text(json.dumps({'status':'PENDING' if diagnostic else host['status'],
        'diagnostic_native_status':host['status'] if diagnostic else None,
        'seconds':host['seconds'],'reason':host['error'],'command':command,
        'native_receipt':str(out/'runtime.txt') if (out/'runtime.txt').exists() else None},indent=2),encoding='utf-8')

original_done.__globals__['logic_done']=archive_done
print('Native logic retry scheduled: '+command+'; evidence '+str(out))
