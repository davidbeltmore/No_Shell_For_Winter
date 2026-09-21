"""Read the sole clothing authoring catalog; never save packages."""
import json
import pathlib
import unreal

director = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
assert director
report = {'director': director.get_path_name(), 'valid': director.is_policy_valid(),
          'error': director.get_policy_validation_error(), 'garments': []}
for row in director.get_editor_property('garments'):
    rule = row.get_editor_property('layer_rule')
    report['garments'].append({
        'id': str(row.get_editor_property('garment_id')),
        'layer': str(rule.get_editor_property('layer')),
        'regions': [{name: str(r.get_editor_property(name)) for name in
                     ('region_id', 'occupied_thickness_cm', 'minimum_outer_gap_cm', 'exclusive_within_layer')}
                    for r in rule.get_editor_property('occupied_regions')],
        'covered_regions': [str(x) for x in row.get_editor_property('covered_body_region_ids')],
        'covers_genitals': row.get_editor_property('covers_genitals')})
out = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/clothing_layers_before.json'
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
