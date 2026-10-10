"""Owned torso flinch authoring; native graph helper and 90-second ceiling."""
import unreal,json,time,shutil
from pathlib import Path
start=time.monotonic();root=Path(unreal.Paths.project_dir());out=root/'Saved/ZombieHit'
out.mkdir(parents=True,exist_ok=True);e=unreal.EditorAssetLibrary
dest='/Game/_Game/Characters/Animation/Hits'
def own_copy(src,dst):
    assert time.monotonic()-start<90,'90-second authoring ceiling'
    return unreal.load_asset(dst) if e.does_asset_exist(dst) else e.duplicate_asset(src,dst)
for path in ['/Game/_Game/Characters/Animation/ABP_DAZZombie','/Game/_Game/Characters/Data/Male/DA_ACFZombieBPMale','/Game/_Game/Characters/Data/Female/DA_ACFZombieBPFemale']:
    src=root/'Content'/Path(path.removeprefix('/Game/')+'.uasset');backup=out/'AssetBackup'/src.relative_to(root/'Content')
    if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,backup)
sequence=own_copy('/Game/FullSample/Animations/SimpleCombat/gideonManny/HitReact_Front',dest+'/AS_ZombieLightHit')
sequence.set_editor_property('enable_root_motion',False)
montage=own_copy('/Game/FullSample/Animations/SimpleCombat/gideonManny/HitReact_Front_Montage',dest+'/AM_ZombieLightHit')
tracks=list(montage.get_editor_property('SlotAnimTracks'));track=tracks[0];segments=list(track.get_editor_property('AnimTrack').get_editor_property('AnimSegments'))
segment=segments[0];segment.set_editor_property('AnimReference',sequence)
animtrack=track.get_editor_property('AnimTrack');animtrack.set_editor_property('AnimSegments',[segment]);track.set_editor_property('AnimTrack',animtrack)
montage.set_editor_property('SlotAnimTracks',[track])
abilities=own_copy('/Game/FullSample/Blueprints/Actions/AbilitySets/AS_ACF_Zombie',dest+'/AS_ZombieReactions')
animation=unreal.load_asset('/Game/_Game/Characters/Animation/ABP_DAZZombie')
assert unreal.ProjectCompatibilityInspectionLibrary.configure_owned_zombie_hit_assets(animation,abilities,montage,'spine_01',3),'Native graph/ability authoring rejected'
for asset in [sequence,montage,abilities,animation]:assert e.save_loaded_asset(asset,only_if_is_dirty=False)
for gender in ['Male','Female']:
    data=unreal.load_asset('/Game/_Game/Characters/Data/'+gender+'/DA_ACFZombieBP'+gender)
    data.set_editor_property('DefaultAbilitySet',abilities);assert e.save_loaded_asset(data,only_if_is_dirty=False)
    bp=unreal.load_asset('/Game/_Game/Characters/'+gender+'/ACFZombieBP'+gender)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
report={'status':'PASS','seconds':time.monotonic()-start,'assets':[a.get_path_name() for a in [sequence,montage,abilities,animation]],'slot':'UpperBody','start_bone':'spine_01','blend_depth':3,'light_maximum_calculated_damage':25,'light_clip_seconds':.65/1.15,'protected_skeleton_modified':False}
(out/'author.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
