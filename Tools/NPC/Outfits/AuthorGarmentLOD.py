"""Scoped generated LOD1 authoring. Run inspect first, then --apply explicitly.

Preserves LOD0 through the native authoring check. Updates only selected garment LODs and their Director sampling policy.
Does not edit bodies, item classes or HUB. Generated fitting data is published separately.
"""
import argparse,json,shutil,time,traceback,hashlib
from pathlib import Path
import unreal
parser=argparse.ArgumentParser()
parser.add_argument('--garments',default='RagShirt,RagPants,RagFootWraps')
parser.add_argument('--ratio',type=float,default=.5)
parser.add_argument('--screen-size',type=float,default=.3)
parser.add_argument('--witness-spacing',type=float,default=.75)
parser.add_argument('--sampling-only',action='store_true',help='Change sampling on existing LOD1 without regenerating or saving garment geometry')
parser.add_argument('--apply',action='store_true')
args=parser.parse_args(globals().get('NPC_OUTFIT_ARGS',[]))
root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/NPCOutfits/GarmentLOD'/str(time.time_ns());out.mkdir(parents=True)
started=time.monotonic();report={'status':'PENDING','apply':args.apply,'sampling_only':args.sampling_only,'ratio':None if args.sampling_only else args.ratio,'screen_size':None if args.sampling_only else args.screen_size,'witness_spacing_cm':args.witness_spacing,'rows':[],'backups':[]}
def write():
    report['seconds']=time.monotonic()-started
    (out/'author.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert .1<=args.ratio<1 and 0<args.screen_size<1 and .5<=args.witness_spacing<=1
    assert not list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), 'Preserve pre-existing dirty packages'
    director=unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
    requested=set(args.garments.split(','))
    assert 0<len(requested)<=3
    rows=[row for row in director.garments if str(row.garment_id) in requested]
    assert len(rows)==len(requested),'Every requested garment must have one authoritative catalog row'
    sub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    for row in rows:
        mesh=row.source_garment
        if not isinstance(mesh,unreal.SkeletalMesh): mesh=unreal.load_asset(str(mesh))
        package=mesh.get_path_name().split('.')[0]
        assert package.startswith('/Game/')
        source=root/'Content'/(package[len('/Game/'):]+'.uasset')
        assert not args.sampling_only or sub.get_lod_count(mesh)==2, 'Sampling-only requires existing LOD0 and LOD1'
        report['rows'].append({'garment':str(row.garment_id),'mesh':mesh.get_path_name(),'before_vertices':[sub.get_num_verts(mesh,i) for i in range(sub.get_lod_count(mesh))],
                              'before_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                              'before_witness_spacing_cm':row.get_editor_property('reduced_lod_maximum_witness_spacing_cm')})
    if args.apply:
        # Back up exact source packages and related generated artifacts once;
        # these backups are rollback evidence, not another authoring catalog.
        paths=[root/'Content/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.uasset']
        for item in report['rows']:
            package=item['mesh'].split('.')[0]
            assert package.startswith('/Game/')
            paths.append(root/'Content'/(package[len('/Game/'):]+'.uasset'))
        generated=root/'Plugins/EFClothingMorph/Content/_Internal/Compiled'
        for file in generated.rglob('*.uasset'):
            if any(name.lower() in file.name.lower() for name in requested) or any(token in file.name.lower() for token in ['registry','manifest']): paths.append(file)
        for source in paths:
            assert source.resolve().is_relative_to(root)
            relative=source.relative_to(root);dest=out/'Backups'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,dest)
            report['backups'].append({'path':str(relative),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
        write()
        for item in report['rows']:
            if time.monotonic()-started>120: raise TimeoutError('120 s authoring ceiling before next garment')
            if args.sampling_only:
                package=item['mesh'].split('.')[0]
                source=root/'Content'/(package[len('/Game/'):]+'.uasset')
                item['after_source_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
                assert item['before_source_sha256']==item['after_source_sha256'], 'Garment source changed during sampling-only authoring'
                item['after_vertices']=item['before_vertices']
                item['saved']=False
                continue
            result=unreal.EFClothingNativeMeshAuthoringLibrary.generate_reduced_garment_lod(director,item['garment'],args.ratio,args.screen_size)
            # bool-success plus OutReport follows the local binding's tuple return.
            item['result']=str(result)
            success=result[0] if isinstance(result,tuple) else str(result).startswith('PASS:')
            assert success, item['result']
            mesh=unreal.load_asset(item['mesh'])
            item['after_vertices']=[sub.get_num_verts(mesh,i) for i in range(sub.get_lod_count(mesh))]
            assert item['after_vertices'][0]==item['before_vertices'][0] and len(item['after_vertices'])==2
            assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,only_if_is_dirty=False)
            item['saved']=True;write()
        authored=list(director.garments)
        for row in authored:
            if str(row.garment_id) in requested:
                row.set_editor_property('reduced_lod_maximum_witness_spacing_cm',args.witness_spacing)
        director.set_editor_property('garments',authored)
        assert unreal.EditorAssetLibrary.save_loaded_asset(director,only_if_is_dirty=False)
        report['director_saved']=True
    assert time.monotonic()-started<=120, 'Authoring exceeded 120 s total'
    report['status']='PASS'
except Exception:
    report.update(status='FAIL',error=traceback.format_exc())
finally:
    write();print(str(out/'author.json'))
if report['status']!='PASS': raise RuntimeError(report['error'])
