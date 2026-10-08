import json
from pathlib import Path
import unreal
assets=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/FullSample',True)
result={}
for a in assets:
    if str(a.asset_name) in ['CHT_PoseSearchDatabases','CHT_PoseSearchDatabases_Sparse','CHT_PoseSearchDatabases_Dense']:
        obj=a.get_asset()
        result[obj.get_path_name()]=json.loads(unreal.ProjectAnimationDiagnosticsLibrary.snapshot_object_properties(obj,''))
        task=unreal.AssetExportTask()
        task.object=obj
        task.filename=str(Path(unreal.Paths.project_dir())/'Saved/BodyPresetsQA'/ (str(a.asset_name)+'.t3d'))
        task.exporter=unreal.ObjectExporterT3D()
        task.automated=True
        task.prompt=False
        task.replace_identical=True
        unreal.Exporter.run_asset_export_task(task)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawn=unreal.GameplayStatics.get_player_pawn(world,0) if world else None
if pawn:
    for m in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        if m.get_name()=='CharacterMesh0':
            result['anim']=json.loads(unreal.ProjectAnimationDiagnosticsLibrary.snapshot_object_properties(m.get_anim_instance(),'MotionComp,Character,UseDense,MovementMode,Gait,Stance,State'))
(Path(unreal.Paths.project_dir())/'Saved/BodyPresetsQA/choosers.json').write_text(json.dumps(result,indent=2))
