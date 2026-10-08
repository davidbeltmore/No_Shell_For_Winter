import json
import time
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())/'Saved/BodyPresetsQA'
report={'skeletons':{},'steps':[]}
for name,path in [('shared','/Game/FullSample/GASP/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin'),('local','/Game/DazToUnreal/Male2/Male2_Skeleton')]:
    obj=unreal.load_asset(path)
    report['skeletons'][name]=[str(v) for v in obj.get_editor_property('compatible_skeletons')]
state={'stage':0,'due':time.monotonic()+5,'start':time.monotonic()}
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
lib=unreal.ProjectAnimationDiagnosticsLibrary
def tick(dt):
    if time.monotonic()<state['due']:return
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    pawn=unreal.GameplayStatics.get_player_pawn(world,0) if world else None
    if not pawn:return
    custom=pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent)
    if not custom:return
    if state['stage'] in (0,2):
        custom.select_body_preset('Male.01' if state['stage']==0 else 'Male.02',False)
    else:
        record={'preset':str(custom.get_body_preset_id()),'meshes':[]}
        for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
            if mesh.get_name() not in ['CharacterMesh0','SkeletalMesh']:continue
            anim=mesh.get_anim_instance()
            record['meshes'].append({'mesh':mesh.get_name(),
                'component':json.loads(lib.snapshot_skeletal_mesh_component(mesh,'root,pelvis,head,hand,foot',24)),
                'blueprint':json.loads(lib.snapshot_anim_blueprint_for_mesh(mesh,'Retarget,Sequence,Blend,Motion')),
                'instance':json.loads(lib.snapshot_object_properties(anim,'AnimNode,Mesh,Skeleton,Retarget')) if anim else None})
        report['steps'].append(record)
        (root/'pose-diagnostic.json').write_text(json.dumps(report,indent=2))
    state['stage']+=1
    state['due']=time.monotonic()+2
    if state['stage']>=4:
        unreal.unregister_slate_post_tick_callback(handle)
        print('Pose diagnostic saved; Male2 remains active in PIE for inspection.')
handle=unreal.register_slate_post_tick_callback(tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
