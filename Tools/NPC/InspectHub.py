"""Read-only check for source NPC classes still placed in the active project map."""
import json,unreal
from pathlib import Path
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
rows=[{'actor':a.get_path_name(),'label':a.get_actor_label(),'class':a.get_class().get_path_name()} for a in actors if isinstance(a,unreal.Pawn)]
out=Path(unreal.Paths.project_dir())/'Saved/NPCDAZ'
(out/'hub-pawns.json').write_text(json.dumps(rows,indent=2))
print(json.dumps(rows))
