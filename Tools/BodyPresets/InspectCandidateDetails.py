import json
from pathlib import Path
import unreal
report = {}
for name in ['Male','Male2','Female']:
    mesh = unreal.load_asset(f'/Game/DazToUnreal/{name}/{name}')
    info = mesh.get_editor_property('asset_import_data')
    report[name] = {
        'source_file': info.get_first_filename() if info else None,
        'skeleton': str(mesh.get_editor_property('skeleton')),
        'physics': str(mesh.get_editor_property('physics_asset')),
        'post_process': str(mesh.get_editor_property('post_process_anim_blueprint')),
        'deformer': str(mesh.get_default_mesh_deformer()),
        'materials': [str(m.material_slot_name) for m in mesh.materials],
        'native': json.loads(unreal.EFBodyOnboardingLibrary.inspect_body(mesh, mesh.get_editor_property('skeleton')))
    }
out = Path(unreal.Paths.project_dir()) / 'Saved/BodyPresetsQA/candidate-details.json'
out.write_text(json.dumps(report,indent=2))
print(json.dumps({k:{a:b for a,b in v.items() if a!='native'} for k,v in report.items()}))
