"""Bounded structural gate: every roster variant, no gameplay session required."""
import json,time,traceback
from pathlib import Path
import unreal
start=time.monotonic(); root='/Game/_Game/Characters'; out=Path(unreal.Paths.project_dir())/'Saved/NPCDAZ'
e=unreal.EditorAssetLibrary; report={'checks':[],'errors':[]}
def check(name,ok):
    report['checks'].append({'name':name,'passed':bool(ok)})
    if not ok: report['errors'].append(name)
try:
    manifest=json.loads((Path(unreal.Paths.project_dir())/'Tools/NPC/Roster.json').read_text())
    expected={row['id'] for row in manifest['archetypes']}
    roster=unreal.load_asset(root+'/Data/DA_NPCRoster')
    check('Manifest archetypes match roster',len(roster.archetypes)==len(expected) and {str(row.id) for row in roster.archetypes}==expected)
    paths=set()
    for row in roster.archetypes:
        for gender,cls in [('Male',row.male),('Female',row.female)]:
            assert time.monotonic()-start<120,'Audit exceeded 120 seconds'
            actor=unreal.get_default_object(cls); path=cls.get_path_name().split('.')[0]; paths.add(path)
            bp=unreal.load_asset(path)
            check(path+' compiles',unreal.BlueprintEditorLibrary.compile_blueprint(bp))
            check(path+' owned parent',root+'/' in str(e.find_asset_data(path).get_tag_value('ParentClass')))
            check(path+' spawn variation','EF.BodyVariation' in {str(t) for t in actor.tags})
            check(path+' no shared identity',not any(str(t).startswith('EF.AppearanceId.') for t in actor.tags))
            mesh=actor.get_component_by_class(unreal.SkeletalMeshComponent)
            rotation=mesh.get_editor_property('relative_rotation')
            check(path+' upright mesh frame',abs(rotation.pitch)<.01 and abs(rotation.roll)<.01 and abs(rotation.yaw+90)<.01)
            check(path+' DAZ gender',mesh.skeletal_mesh_asset.get_path_name().startswith('/Game/DazToUnreal/'+gender+'/'))
            check(path+' owned animation',mesh.anim_class.get_path_name().startswith(root+'/Animation/'))
            init=actor.get_component_by_class(unreal.ACFCharacterInitializerComponent)
            data=init.get_editor_property('character_init_data_asset')
            check(path+' owned initializer',bool(data) and data.get_path_name().startswith(root+'/Data/'))
            check(path+' initializer preserves mesh',data.mesh_components[0].skeletal_mesh==mesh.skeletal_mesh_asset)
            check(path+' initializer preserves animation',data.mesh_components[0].anim_instance==mesh.anim_class)
    check('Two unique variants per archetype',len(paths)==2*len(expected))
    catalog=unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets')
    for row in catalog.bodies:
        morphs={str(n) for n in row.mesh.get_all_morph_target_names()}
        check(str(row.preset_id)+' profile targets exist',all(str(p.morph_name) in morphs for p in row.morph_profile))
        if row.gender==unreal.CharacterCreationGender.MALE and row.allow_npc:
            neutral=[p for p in row.morph_profile if p.get_editor_property('npc_neutral')]
            check(str(row.preset_id)+' valid neutral profile',bool(neutral) and all(not p.randomize_npc and p.minimum<=p.default_value<=p.maximum for p in neutral))
            profiles={str(p.morph_name):p for p in neutral}
            check(str(row.preset_id)+' current DK rest contract',str(row.preset_id) not in ['Male.01','Male.02'] or all(name in profiles and profiles[name].default_value==value for name,value in [('DK_Flacid 03',1),('DK_Erection',0)]))
    report.update(status='FAIL' if report['errors'] else 'PASS',variant_count=len(paths))
except Exception: report['status']='FAIL'; report['errors'].append(traceback.format_exc())
report['elapsed_seconds']=time.monotonic()-start; report['check_count']=len(report['checks'])
(out/'audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='checks'}))
assert report['status']=='PASS'
