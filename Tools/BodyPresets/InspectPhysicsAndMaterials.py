import json
from pathlib import Path
import unreal
out={}
for name in ['Male','Male2','Female']:
    mesh=unreal.load_asset(f'/Game/DazToUnreal/{name}/{name}')
    physics=mesh.get_editor_property('physics_asset')
    task=unreal.AssetExportTask()
    task.object=physics
    task.filename=str(Path(unreal.Paths.project_dir())/'Saved/BodyPresetsQA'/(name+'-physics.t3d'))
    task.exporter=unreal.ObjectExporterT3D()
    task.automated=True
    task.prompt=False
    task.replace_identical=True
    unreal.Exporter.run_asset_export_task(task)
    mats=[]
    for m in mesh.materials:
        material=m.material_interface
        mats.append({'slot':str(m.material_slot_name),'material':material.get_path_name() if material else None,'parent':str(material.get_editor_property('parent')) if isinstance(material,unreal.MaterialInstanceConstant) else None})
    out[name]=mats
(Path(unreal.Paths.project_dir())/'Saved/BodyPresetsQA/body-materials.json').write_text(json.dumps(out,indent=2))
