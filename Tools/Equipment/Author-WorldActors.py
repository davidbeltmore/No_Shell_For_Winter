"""Explicit prototype/pickup bootstrap; preserves authored nonempty assignments."""
import json
import pathlib
import unreal

OUT = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA/author_world_actors.json'
OUT.write_text(json.dumps({'result': 'PENDING: World actor authoring started.'}))
try:
    result = unreal.EFEquipmentAuthoringLibrary.build_world_actors()
    OUT.write_text(json.dumps({'result': result}, indent=2))
    if not result.startswith('PASS:'):
        raise RuntimeError(result)
    print(result)
except Exception as error:
    OUT.write_text(json.dumps({'result': 'FAIL: ' + str(error)}, indent=2))
    raise
