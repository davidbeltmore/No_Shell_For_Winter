"""Read-only UE 5.8 load/decompression gate for the seven project-owned legacy assets.
No resave: an empty version header alone is not evidence of broken gameplay.
"""
import hashlib,json
from pathlib import Path
import unreal

report={'animations':[],'resaved':False}
out=Path(unreal.Paths.project_saved_dir())/'Analysis/ProgressionRemaining/legacy_compatibility.json'
out.parent.mkdir(parents=True,exist_ok=True)
try:
    root='/Game/FullSample/Animations/LadderSystem/Animations/'
    for name in ['A_Climb-Idle','A_Climb-End_Bottom','A_Climb-End_Top','A_Climb-Start_Bottom','A_Climb-Start_Top','A_Climb-Loop_Up']:
        asset=unreal.load_asset(root+name)
        entry=json.loads(unreal.ProjectAnimationDiagnosticsLibrary.snapshot_anim_sequence_asset(asset))
        assert entry['play_length']>0 and entry['track_count']>0 and entry['sampled_keys']>0,entry
        entry['compressed_samples']=[]
        options=unreal.AnimPoseEvaluationOptions(evaluation_type=unreal.AnimDataEvalType.COMPRESSED)
        for time in [0.0,entry['play_length']*.5,entry['play_length']]:
            pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(asset,time,options)
            assert unreal.AnimPoseExtensions.is_valid(pose),name
            bones=unreal.AnimPoseExtensions.get_bone_names(pose)
            transforms=[unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.LOCAL).export_text() for bone in bones]
            assert bones and all('nan' not in t.lower() and 'inf' not in t.lower() for t in transforms),name
            entry['compressed_samples'].append({'time':time,'bones':len(bones),'pose_sha256':hashlib.sha256('\n'.join(transforms).encode()).hexdigest()})
        report['animations'].append(entry)
    path='/Game/FullSample/Assets/Items/BowAndArrow/Arrow'
    mesh=unreal.load_asset(path)
    assert isinstance(mesh,unreal.StaticMesh),str(mesh)
    subsystem=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    report['arrow']={'asset':path,'lods':subsystem.get_lod_count(mesh),'vertices_lod0':subsystem.get_number_verts(mesh,0),'bounds':mesh.get_bounds().export_text()}
    assert report['arrow']['lods']>0 and report['arrow']['vertices_lod0']>0
    assert 'nan' not in report['arrow']['bounds'].lower() and 'inf' not in report['arrow']['bounds'].lower()
    report['passed']=True
except Exception as error:
    report['passed']=False;report['error']=str(error)
    raise
finally:
    out.write_text(json.dumps(report,indent=2),encoding='utf8')
