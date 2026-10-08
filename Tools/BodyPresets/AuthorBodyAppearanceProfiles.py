"""Finish scoped material and secondary-physics profiles without changing protected bodies."""
import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir())/'Saved/BodyPresetsQA'
report={}
mesh=unreal.load_asset('/Game/DazToUnreal/Male2/Male2')
materials=list(mesh.materials)
torso=next(m for m in materials if str(m.material_slot_name)=='Genesis9_Torso_Back')
body=next(m for m in materials if str(m.material_slot_name)=='Genesis9_Body')
report['originalTorsoMaterial']=str(torso.material_interface)
if not torso.material_interface:
    torso.material_interface=body.material_interface
    mesh.set_editor_property('materials',materials)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
report['torsoMaterial']=body.material_interface.get_path_name()
path='/Game/_Game/Characters/Physics/PA_Male2_Morph'
candidate=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if not candidate:
    candidate=unreal.EditorAssetLibrary.duplicate_asset(mesh.get_editor_property('physics_asset').get_path_name(),path)
assert candidate
result=unreal.ProjectCompatibilityInspectionLibrary.author_morph_physics_candidate(mesh,candidate)
assert not result.startswith('ERROR'),result
assert unreal.EditorAssetLibrary.save_loaded_asset(candidate)
report['physicsAuthoring']=result
catalog=unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets')
rows=list(catalog.bodies)
for row in rows:
    name={'Male.01':'Male','Male.02':'Male2','Female.01':'Female'}.get(str(row.preset_id))
    if name:
        physics=unreal.load_asset(f'/Game/_Game/Characters/Physics/PA_{name}_Morph')
        assert physics
        row.set_editor_property('morph_physics_asset',physics)
catalog.set_editor_property('bodies',rows)
assert unreal.EditorAssetLibrary.save_loaded_asset(catalog)
report['status']='AUTHORED; runtime and cold reload validation PENDING'
(root/'appearance-profiles.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
