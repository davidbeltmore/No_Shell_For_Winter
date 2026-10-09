"""Read-only DAZ replacement payload gate; never resave native MetaHuman assets."""
import json,time,unreal
from pathlib import Path
started=time.monotonic();rows=[];errors=[]
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib=unreal.SubobjectDataBlueprintFunctionLibrary
for gender in ['Male','Female']:
    bp=unreal.load_asset('/Game/_Game/Characters/'+gender+'/ACF_NPCAI_BP'+gender)
    for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
        c=lib.get_object_for_blueprint(lib.get_data(handle),bp)
        if not c:continue
        if isinstance(c,unreal.SkeletalMeshComponent):
            mesh=c.get_editor_property('skeletal_mesh_asset')
            row={'variant':gender,'component':c.get_name(),'mesh':mesh.get_path_name() if mesh else None,'hidden':c.get_editor_property('hidden_in_game')}
            if c.get_name()=='CharacterMesh0':
                if not mesh or not mesh.get_path_name().startswith('/Game/DazToUnreal/'):errors.append(row)
            elif mesh:errors.append(row)
            rows.append(row)
        elif c.get_class().get_name()=='GroomComponent':
            groom=c.get_editor_property('groom_asset')
            row={'variant':gender,'component':c.get_name(),'groom':str(groom),'auto_activate':c.get_editor_property('auto_activate')}
            if groom or row['auto_activate']:errors.append(row)
            rows.append(row)
result={'status':'FAIL' if errors else 'PASS','components':rows,'errors':errors,'seconds':time.monotonic()-started}
(Path(unreal.Paths.project_dir())/'Saved/NPCLogic/metahuman-payload.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result));assert not errors
