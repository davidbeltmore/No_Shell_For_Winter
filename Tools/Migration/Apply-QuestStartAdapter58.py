"""Apply the bounded default-quest bridge to the project-owned sample controller."""
import json
from pathlib import Path
import unreal

path='/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFUltimatePlayerControllerBP'
bp=unreal.load_asset(path)
assert unreal.ProjectBlueprintMigrationLibrary.adapt_default_quest_start(bp), 'Quest adapter did not compile; nothing saved'
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False), 'Quest controller save failed'
out=Path(unreal.Paths.project_saved_dir())/'Analysis/ProgressionRemaining'
out.mkdir(parents=True,exist_ok=True)
(out/'quest_adapter.json').write_text(json.dumps({'asset':path,'compiled':str(bp.get_editor_property('status')),'saved':True},indent=2),encoding='utf8')
unreal.log('PROJECT_QUEST_ADAPTER_SAVED')
