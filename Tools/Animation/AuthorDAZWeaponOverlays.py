"""Bounded owned pose-policy authoring; no original Blueprint/mesh is saved."""
import json,time,unreal
from pathlib import Path
start=time.monotonic();e=unreal.EditorAssetLibrary
weapons=globals().get('DAZ_WEAPONS',['Pistol','Rifle'])
out=Path(unreal.Paths.project_dir())/('Saved/BowPose' if weapons==['Bow'] else 'Saved/GunPose');out.mkdir(parents=True,exist_ok=True)
dest='/Game/_Game/Characters/Animation/Weapons';report=[]
for weapon in weapons:
    assert time.monotonic()-start<90,'90-second authoring ceiling'
    source='/Game/FullSample/GASP/Animations/Overlays/Additive/ACF_BowAdditiveOverlay' if weapon=='Bow' else f'/Game/FullSample/GASP/Animations/Overlays/Additive/Weapons/{weapon}/ACF_MM{weapon}AdditiveOverlay'
    src=unreal.load_asset(source);src_cdo=unreal.get_default_object(src.generated_class());profiles={}
    for mode in ['Idle','Aim']:
        original=src_cdo.get_editor_property(mode+'Layering')
        path=f'{dest}/DA_DAZ{weapon}{mode}Layering'
        profile=unreal.load_asset(path) if e.does_asset_exist(path) else e.duplicate_asset(original.get_path_name(),path)
        for name in ['Legs','Pelvis','Spine','Head','ArmL','ArmL_Secondary','ArmR','ArmR_Secondary','UpperBody_Secondary']:
            s=profile.get_editor_property(name);enum=type(s.get_editor_property('OverlayType'))
            absolute=name in ['Spine','ArmL','ArmR']
            s.set_editor_property('OverlayType',enum.OVERLAY_POSE if absolute else enum.BASE_POSE)
            s.set_editor_property('OverlayWeight',1.0 if absolute or (weapon in ['Rifle','Bow'] and name=='Head') else 0.0)
            profile.set_editor_property(name,s)
        e.save_loaded_asset(profile,only_if_is_dirty=False);profiles[mode]=profile
    target=f'{dest}/ABP_DAZ{weapon}Overlay'
    bp=unreal.load_asset(target) if e.does_asset_exist(target) else e.duplicate_asset(source,target)
    cdo=unreal.get_default_object(bp.generated_class())
    for field in ['IdleOverlay','LocomotionStancesOverlays']:cdo.set_editor_property(field,src_cdo.get_editor_property(field))
    for field,mode in [('IdleLayering','Idle'),('AimLayering','Aim'),('BlockLayering','Idle'),('CustomLayering','Idle')]:cdo.set_editor_property(field,profiles[mode])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp);e.save_loaded_asset(bp,only_if_is_dirty=False)
    report.append({'source':source,'target':target,'profiles':{k:v.get_path_name() for k,v in profiles.items()}})
original=unreal.get_default_object(unreal.load_asset('/Game/FullSample/Animations/AnimBPs/ACF_MMHumanoid_ABP').generated_class())
rows=[{'tag':str(row.tag_name.get_editor_property('TagName')),'class':row.overlay.get_path_name() if row.overlay else None} for row in original.get_editor_property('OverlayLayers')]
(out/'author.json').write_text(json.dumps({'status':'PASS','seconds':time.monotonic()-start,'overlays':report,'original_table':rows},indent=2))
print(f'Authored {len(weapons)} owned overlays / {2*len(weapons)} layering profiles; original table:',rows)
