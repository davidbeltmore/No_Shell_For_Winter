"""Idempotently register current bodies, then compile and publish all unisex fits."""
import json
import pathlib
import runpy
import traceback
import unreal

OUT = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/ClothingUnisexQA'
OUT.mkdir(parents=True, exist_ok=True)
report = {'status': 'PENDING'}
try:
    assert pathlib.Path(unreal.Paths.get_project_file_path()).name == 'NoShellForWinter.uproject'
    director = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
    rows = list(director.get_editor_property('garments'))
    for row in rows:
        if str(row.get_editor_property('garment_id')) in ('UnderWearPanty_Female', 'UnderWearBikini_Female', 'RagPants'):
            row.set_editor_property('covers_genitals', True)
    director.set_editor_property('garments', rows)
    bodies = list(director.get_editor_property('bodies'))
    coverage_graph = unreal.EFClothingSurfaceDeformerBuilderLibrary.build_body_coverage_deformer()
    assert coverage_graph.get_editor_property('success'), str(coverage_graph.get_editor_property('report'))
    report['body_coverage_graph'] = str(coverage_graph.get_editor_property('report'))
    for name in ('Female', 'Male'):
        mesh = unreal.load_asset(f'/Game/DazToUnreal/{name}/{name}')
        target = next((b for b in bodies if b.get_editor_property('body_surface') == mesh), None)
        if target is None:
            target = unreal.EFClothingBodyTarget()
            target.set_editor_property('body_surface', mesh)
            if name == 'Female':
                target.set_editor_property('genital_material_slots', ['Genesis9_GP_Torso'])
            else:
                target.set_editor_property('material_slot_aliases', {'Genesis9_GP_Torso': 'None'})
                target.set_editor_property('excluded_fit_bone_branches', ['shaft_01', 'scrotum'])
                target.set_editor_property('genital_bone_branches', ['shaft_01', 'scrotum'])
            bodies.append(target)
        if name == 'Male':
            target.set_editor_property('bone_coverage_deformer', unreal.load_asset('/EFClothingMorph/Deformers/DG_EFBodyCoverageDQS'))
            source_deformer = mesh.get_default_mesh_deformer()
            assert source_deformer and source_deformer.get_path_name() == '/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth.DG_DualQuatSkin_Morph_Cloth'
            target.set_editor_property('bone_coverage_deformer_source', source_deformer)
            report['male_source_deformer'] = source_deformer.get_path_name()
    director.set_editor_property('bodies', bodies)
    assert unreal.EditorAssetLibrary.save_loaded_asset(director, only_if_is_dirty=False)
    options = unreal.EFClothingNativeSourceCompileOptions()
    options.set_editor_property('output_root', '/EFClothingMorph/_Internal/Compiled/V4')
    options.set_editor_property('maximum_push_cm', 2.5)
    options.set_editor_property('only_stale', True)
    options.set_editor_property('strict_catalog_certification', True)
    result = unreal.EFClothingFitCompilerLibrary.compile_native_source_catalog_v4(director, unreal.load_asset('/Game/DazToUnreal/Multiple/Multiple'), options)
    report['compile'] = str(result.get_editor_property('report'))
    report['rows'] = [str(r.get_editor_property('report')) for r in result.get_editor_property('rows')]
    assert result.get_editor_property('success'), report['compile']
    report['sync'] = unreal.EFClothingFitCompilerLibrary.sync_unisex_runtime_catalog(director)
    assert report['sync'].startswith('PASS:'), report['sync']
    report['authored_rows'] = len(rows)
    report['body_variants'] = len(director.build_body_variants())
    runpy.run_path(str(pathlib.Path(unreal.Paths.project_dir()) / 'Tools/ClothingMorphV4/Validate-EFClothingMorphV4Blueprints58.py'))
    report['blueprint_compile'] = 'PASS (seven clothing/Player Blueprints; no assets saved by compile)'
    report['status'] = 'PASS'
except Exception:
    report['status'] = 'FAIL'
    report['error'] = traceback.format_exc()
finally:
    (OUT / 'upgrade.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.log('UNISEX_UPGRADE ' + json.dumps(report))
if report['status'] != 'PASS':
    raise RuntimeError(report['error'])
