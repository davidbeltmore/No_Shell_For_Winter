import json,time
from pathlib import Path
import unreal
out=Path(unreal.Paths.project_dir())/'Saved/BodyPresetsQA/pose-evaluation.json'
try: unreal.unregister_slate_post_tick_callback(handle)
except Exception: pass
state={'stage':0,'due':0,'records':[],'deadline':time.monotonic()+45}
def tick(dt):
    if time.monotonic()<state['due']: return
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    pawn=unreal.GameplayStatics.get_player_pawn(world,0)
    if not pawn:
        if time.monotonic()>state['deadline']: unreal.unregister_slate_post_tick_callback(handle)
        return
    custom=pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent)
    if state['stage'] in (0,3,6):
        custom.select_body_preset('Male.02' if state['stage']==3 else 'Male.01',False)
    else:
        record={'preset':str(custom.get_body_preset_id()),'stage':state['stage'],'meshes':[]}
        for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
            if mesh.get_name() not in ['CharacterMesh0','SkeletalMesh']: continue
            bones={name:str(mesh.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT)) for name in ['root','pelvis','spine_03','upperarm_l','hand_l','foot_l']}
            record['meshes'].append({'name':mesh.get_name(),'bones':bones,'properties':json.loads(unreal.ProjectAnimationDiagnosticsLibrary.snapshot_object_properties(mesh.get_anim_instance(),''))})
        state['records'].append(record)
        out.write_text(json.dumps(state['records'],indent=2))
    state['stage']+=1
    state['due']=time.monotonic()+2
    if state['stage']>=9: unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(tick)
if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
