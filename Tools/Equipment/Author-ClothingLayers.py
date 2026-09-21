"""Bootstrap semantic layering in the sole Director; preserve existing fit tuning."""
import json
import pathlib
import traceback
import unreal

out = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/clothing_layer_authoring.json'
report = {'status': 'PENDING', 'source': '/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector'}
try:
    director = unreal.load_asset(report['source'])
    regions = list(director.get_editor_property('regions'))
    for name in ('Torso', 'Legs', 'Feet'):
        if not any(str(r.get_editor_property('region_id')) == name for r in regions):
            region = unreal.EFClothingBodyRegionDefinition()
            region.set_editor_property('region_id', name)
            # Partial garments must not hide a whole skin section; the existing
            # authored genital coverage remains the coverage authority.
            region.set_editor_property('hide_skin_when_covered', False)
            regions.append(region)
    director.set_editor_property('regions', regions)
    assignments = {
        'UnderWearBra_Female': ('UNDERWEAR', ('Torso',)),
        'UnderWearPanty_Female': ('UNDERWEAR', ('Legs',)),
        'UnderWearBikini_Female': ('UNDERWEAR', ('Torso', 'Legs')),
        'RagShirt': ('BASE', ('Torso',)),
        'RagPants': ('BASE', ('Legs',)),
        'RagFootWraps': ('BASE', ('Feet',)),
    }
    rows = list(director.get_editor_property('garments'))
    found = set()
    for row in rows:
        name = str(row.get_editor_property('garment_id'))
        if name not in assignments:
            continue
        found.add(name)
        layer, region_names = assignments[name]
        rule = row.get_editor_property('layer_rule')
        rule.set_editor_property('layer', getattr(unreal.EFClothingLayer, layer))
        occupied = list(rule.get_editor_property('occupied_regions'))
        for region_name in region_names:
            if not any(str(o.get_editor_property('region_id')) == region_name for o in occupied):
                occupancy = unreal.EFClothingRegionOccupancyRule()
                occupancy.set_editor_property('region_id', region_name)
                occupancy.set_editor_property('occupied_thickness_cm', 0.03 if layer == 'UNDERWEAR' else 0.08)
                occupancy.set_editor_property('minimum_outer_gap_cm', 0.03)
                occupied.append(occupancy)
        rule.set_editor_property('occupied_regions', occupied)
        row.set_editor_property('layer_rule', rule)
    assert found == set(assignments), 'Expected six catalog garments; missing ' + str(set(assignments) - found)
    director.set_editor_property('garments', rows)
    assert director.is_policy_valid(), director.get_policy_validation_error()
    report['sync'] = unreal.EFClothingFitCompilerLibrary.sync_unisex_runtime_catalog(director)
    assert report['sync'].startswith('PASS:'), report['sync']
    assert unreal.EditorAssetLibrary.save_loaded_asset(director, only_if_is_dirty=True)
    manifest = unreal.load_asset('/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingSystemManifest')
    report['body_profiles'] = []
    for profile in manifest.get_editor_property('body_profiles'):
        names = [str(r.get_editor_property('region_id')) for r in profile.get_editor_property('regions')]
        assert {'Torso', 'Legs', 'Feet'} <= set(names), names
        report['body_profiles'].append({'path': profile.get_path_name(), 'regions': names})
    report['status'] = 'PASS'
except Exception:
    report.update(status='FAIL', error=traceback.format_exc())
finally:
    out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))
if report['status'] != 'PASS':
    raise RuntimeError(report['error'])
