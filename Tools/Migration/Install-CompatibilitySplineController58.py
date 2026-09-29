"""Editor-only, explicit narrow install; preserves both authored ACF BP layers."""
from pathlib import Path
import shutil
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
backup=root/'Saved/Analysis/CompatibilityClosure/OwnedAbilitySetsBeforeRepair'
backup.mkdir(parents=True,exist_ok=True)
guy_path='/Game/FullSample/Integrations/Ultimate/Blueprint/NPCs/ACFGuy_BP'
guy_file=root/'Content/FullSample/Integrations/Ultimate/Blueprint/NPCs/ACFGuy_BP.uasset'
if not (backup/guy_file.name).exists():shutil.copy2(guy_file,backup/guy_file.name)
library=unreal.EditorAssetLibrary
source='/AscentCombatFramework/Blueprints/AI/Controllers/'
target='/Game/_Game/Blueprints/Compatibility/'
base_path=target+'BP_ProjectSplineControllerBase'
npc_path=target+'BP_ProjectSplineNPCController'
base=unreal.load_asset(base_path) if library.does_asset_exist(base_path) else library.duplicate_asset(source+'ACFAIControllerBP',base_path)
unreal.BlueprintEditorLibrary.reparent_blueprint(base,unreal.ProjectSplineAIController)
unreal.BlueprintEditorLibrary.compile_blueprint(base)
npc=unreal.load_asset(npc_path) if library.does_asset_exist(npc_path) else library.duplicate_asset(source+'ACF_NPCController_BP',npc_path)
unreal.BlueprintEditorLibrary.reparent_blueprint(npc,base.generated_class())
unreal.BlueprintEditorLibrary.compile_blueprint(npc)
guy=unreal.load_asset(guy_path)
defaults=unreal.get_default_object(guy.generated_class())
previous=defaults.get_editor_property('ai_controller_class')
assert previous.get_path_name() in [source+'ACF_NPCController_BP.ACF_NPCController_BP_C',npc_path+'.BP_ProjectSplineNPCController_C']
defaults.set_editor_property('ai_controller_class',npc.generated_class())
unreal.BlueprintEditorLibrary.compile_blueprint(guy)
assert unreal.get_default_object(guy.generated_class()).get_editor_property('ai_controller_class')==npc.generated_class()
for asset in [base,npc,guy]:assert library.save_loaded_asset(asset,False)
print('Installed own controller for ACFGuy only; both original Blueprint layers copied intact.')
