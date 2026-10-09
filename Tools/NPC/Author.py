"""Idempotent NPC DAZ authoring. Only saves packages below /Game/_Game/Characters
and the shared body catalog. Source bodies/vendor packages are read-only.
Run after an inspection and protection baseline. 180-second operation ceiling.
"""
import json,re,shutil,time,traceback
from pathlib import Path
import unreal

project=Path(unreal.Paths.project_dir()); out=project/'Saved/NPCDAZ'; out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((project/'Tools/NPC/Roster.json').read_text())
root=manifest['root']; e=unreal.EditorAssetLibrary; bel=unreal.BlueprintEditorLibrary
started=time.monotonic(); report={'assets':[],'variants':[],'errors':[]}; owned={}; replacements={}

def restore_logic(src,bp):
    result=json.loads(unreal.ProjectCompatibilityInspectionLibrary.sync_owned_npc_logic_defaults(unreal.load_asset(src),bp,True))
    assert not result.get('error') and not result['errors'],result
def check_time(): assert time.monotonic()-started<180,'Authoring exceeded 180-second ceiling'
def backup(p):
    if not p.startswith('/Game/'): return
    file=project/'Content'/(p.removeprefix('/Game/')+'.uasset')
    dest=out/'Originals'/file.relative_to(project)
    if file.exists() and not dest.exists(): dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(file,dest)
def save(o):
    check_time(); p=o.get_path_name().split('.')[0]
    assert p.startswith(root+'/') or p=='/Game/_Game/Data/CharacterCreation/DA_BodyPresets',p
    # Python struct/array assignment can change memory without setting package dirty.
    assert e.save_loaded_asset(o,only_if_is_dirty=False),p
def duplicate(src,dst):
    check_time(); assert dst.startswith(root+'/')
    if e.does_asset_exist(dst): backup(dst); return unreal.load_asset(dst)
    asset=e.duplicate_asset(src,dst); assert asset,(src,dst)
    report['assets'].append({'source':src,'target':dst}); return asset
def parent_path(bp):
    value=str(e.find_asset_data(bp.get_path_name()).get_tag_value('ParentClass'))
    match=re.search("'([^']+)'",value)
    return match.group(1) if match else value
def map_bp(src,bp):
    old=unreal.load_asset(src)
    replacements[old]=bp
    replacements[old.generated_class()]=bp.generated_class()
    replacements[unreal.get_default_object(old.generated_class())]=unreal.get_default_object(bp.generated_class())
def framework(class_path):
    if class_path.startswith('/Script/'): return unreal.load_class(None,class_path)
    src=class_path.split('.')[0]
    if src.startswith(root+'/'): return unreal.load_asset(src).generated_class()
    if src in owned: return owned[src].generated_class()
    old=unreal.load_asset(src); assert isinstance(old,unreal.Blueprint),src
    parent=framework(parent_path(old))
    bp=duplicate(src,root+'/Framework/'+src.rsplit('/',1)[1])
    bel.reparent_blueprint(bp,parent)
    assert bel.compile_blueprint(bp),src
    restore_logic(src,bp)
    assert bel.compile_blueprint(bp),src
    owned[src]=bp; map_bp(src,bp)
    return bp.generated_class()
def cdo(bp): return unreal.get_default_object(bp.generated_class())
def components(bp):
    sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem); lib=unreal.SubobjectDataBlueprintFunctionLibrary
    result={}
    for h in sub.k2_gather_subobject_data_for_blueprint(bp):
        o=lib.get_object_for_blueprint(lib.get_data(h),bp)
        if o: result[o.get_path_name()]=o
    return list(result.values())
def configure_mesh(mesh,body,anim):
    mesh.set_editor_property('skeletal_mesh_asset',body)
    mesh.set_editor_property('override_materials',[])
    mesh.set_editor_property('anim_class',anim)
    mesh.set_editor_property('animation_mode',unreal.AnimationMode.ANIMATION_BLUEPRINT)
    mesh.set_editor_property('component_tags',['Mesh'])
    mesh.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE)
    mesh.set_editor_property('relative_scale3d',unreal.Vector(1,1,1))
    mesh.set_editor_property('relative_location',unreal.Vector(0,0,-88))
    mesh.set_editor_property('relative_rotation',unreal.Rotator(pitch=0,yaw=-90,roll=0))
def source_paths(row):
    if 'source' in row: return row['source'],row['data']
    if row['family']=='Services':
        return '/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/'+row['name'], '/Game/FullSample/Integrations/Ultimate/NPC/DA_'+row['name']
    return '/Game/FullSample/Blueprints/Characters/'+row['family']+'/'+row['name'], '/Game/FullSample/Blueprints/Characters/NPC/DA_'+row.get('data_name',row['name'])

try:
    anims={}
    for label,src in [('Humanoid','/Game/FullSample/Animations/AnimBPs/ACF_MMHumanoid_ABP'),('Zombie','/Game/FullSample/Assets/Characters/Zombie/ZombieABP')]:
        bp=duplicate(src,root+'/Animation/ABP_DAZ'+label)
        assert bel.compile_blueprint(bp); anims[label]=bp.generated_class(); save(bp)
    catalog=unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets'); backup(catalog.get_path_name().split('.')[0])
    bodies=list(catalog.bodies)
    for body in bodies:
        if body.gender!=unreal.CharacterCreationGender.MALE or not body.allow_npc: continue
        if str(body.preset_id) not in ['Male.01','Male.02']:
            neutral=[p for p in body.morph_profile if p.get_editor_property('npc_neutral')]
            assert neutral and all(not p.randomize_npc for p in neutral), 'Onboard this body with its own neutral profile first: '+str(body.preset_id)
            continue
        mesh=body.mesh; names={str(n) for n in mesh.get_all_morph_target_names()}
        assert {'DK_Flacid 03','DK_Erection'}<=names,str(body.preset_id)
        profiles=list(body.morph_profile)
        for name,value in [('DK_Flacid 03',1.0),('DK_Erection',0.0)]:
            profile=next((p for p in profiles if str(p.morph_name)==name),None)
            if profile is None: profile=unreal.EFBodyMorphProfileEntry(); profiles.append(profile)
            profile.morph_name=name; profile.minimum=0; profile.maximum=1; profile.default_value=value
            profile.randomize_npc=False; profile.set_editor_property('npc_neutral',True)
        body.morph_profile=profiles
    catalog.set_editor_property('bodies',bodies); save(catalog)
    entries=[]; variant_bps=[]
    for row in manifest['archetypes']:
        src,source_data=source_paths(row); original=unreal.load_asset(src); assert original,src
        parent=framework(parent_path(original)); variants={}
        for gender in ['Male','Female']:
            dst=root+'/'+gender+'/'+row['name']+gender
            bp=duplicate(src,dst)
            # Preserve existing authored enemy inheritance where it is already project-owned.
            current_parent=parent_path(bp)
            new_parent=framework(current_parent) if current_parent.startswith(root+'/') else parent
            bel.reparent_blueprint(bp,new_parent); assert bel.compile_blueprint(bp),dst
            # Reparenting changes SCS component keys; preserve effective source overrides.
            restore_logic(src,bp)
            actor=cdo(bp); actor.set_editor_property('tags',list(dict.fromkeys([str(t) for t in actor.tags]+['EF.BodyVariation','EF.NPC.'+row['id']])))
            body=unreal.load_asset('/Game/DazToUnreal/'+gender+'/'+gender)
            anim=anims[row.get('animation','Humanoid')]
            configure_mesh(actor.get_component_by_class(unreal.SkeletalMeshComponent),body,anim)
            data=duplicate(source_data,root+'/Data/'+gender+'/DA_'+row['name']+gender)
            meshes=list(data.get_editor_property('mesh_components')); assert meshes,source_data
            primary=meshes[0]; primary.skeletal_mesh=body; primary.anim_instance=anim; primary.material_overrides=[]; primary.component_tag='Mesh'
            primary.set_editor_property('override_relative_transform',False)
            data.set_editor_property('mesh_components',[primary])
            initializer=actor.get_component_by_class(unreal.ACFCharacterInitializerComponent); assert initializer
            initializer.set_editor_property('character_init_data_asset',data)
            # DAZ is a single complete body. Retain Blueprint variables but remove MetaHuman rendering payloads.
            if row.get('daz_face_adapter'):
                for comp in components(bp):
                    if isinstance(comp,unreal.SkeletalMeshComponent) and comp.get_name()!='CharacterMesh0':
                        comp.set_editor_property('skeletal_mesh_asset',None); comp.set_editor_property('anim_class',None)
                        comp.set_editor_property('hidden_in_game',True)
                    if comp.get_class().get_name() in ['GroomComponent','ADSMetahumanLipSync','LODSyncComponent']:
                        comp.set_editor_property('auto_activate',False)
                        if isinstance(comp,unreal.SceneComponent): comp.set_editor_property('hidden_in_game',True)
                        if comp.get_class().get_name()=='GroomComponent': comp.set_editor_property('groom_asset',None)
            save(data)
            assert bel.compile_blueprint(bp),dst
            variant_bps.append(bp); variants[gender]=bp.generated_class()
            report['variants'].append({'id':row['id'],'gender':gender,'path':dst,'data':data.get_path_name(),'source':src})
        entry=unreal.ProjectNPCArchetype()
        for key,value in {'id':row['id'],'role':row['role'],'male':variants['Male'],'female':variants['Female']}.items(): entry.set_editor_property(key,value)
        entries.append(entry)
    # Remap only owned packages. The source framework never becomes a consolidation target.
    for bp in list(owned.values())+variant_bps:
        # Compiling a parent reinstantiates descendants/CDOs. Rebuild the map each time.
        replacements={}
        for src,target in owned.items(): map_bp(src,target)
        count=unreal.ProjectCompatibilityInspectionLibrary.remap_owned_npc_references(bp,replacements)
        assert count>=0,bp.get_path_name()
        assert bel.compile_blueprint(bp),bp.get_path_name()
        save(bp)
    roster_path=root+'/Data/DA_NPCRoster'
    if e.does_asset_exist(roster_path): roster=unreal.load_asset(roster_path); backup(roster_path)
    else:
        factory=unreal.DataAssetFactory(); factory.set_editor_property('data_asset_class',unreal.ProjectNPCRoster)
        roster=unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_NPCRoster',root+'/Data',unreal.ProjectNPCRoster,factory)
    roster.set_editor_property('archetypes',entries); save(roster)
    report.update(status='PASS',archetypes=len(entries),variants_count=len(variant_bps),framework_count=len(owned))
except Exception:
    report['status']='FAIL'; report['errors'].append(traceback.format_exc()); raise
finally:
    report['elapsed_seconds']=time.monotonic()-started
    (out/'author.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k not in ['assets','variants']}))
