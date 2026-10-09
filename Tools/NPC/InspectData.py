"""Read the smallest initialization/animation slice before authoring."""
import json,time
from pathlib import Path
import unreal
e=unreal.EditorAssetLibrary
out=Path(unreal.Paths.project_dir())/'Saved/NPCDAZ'
paths=['/Game/FullSample/Blueprints/Characters/NPC/DA_ACFMeleeEnemyBP','/Game/FullSample/Blueprints/Characters/NPC/DA_ACFZombieBP','/Game/FullSample/Integrations/Ultimate/NPC/DA_ACF_SkillGuy_BP','/Game/FullSample/Animations/AnimBPs/ACF_MMHumanoid_ABP']
r=[]
for p in paths:
 o=unreal.load_asset(p)
 if isinstance(o,unreal.Blueprint): o=unreal.get_default_object(o.generated_class())
 fields={}
 for k in ['mesh_components','fragments','moveset_layers','overlay_layers','default_ability_set','ai_controller_class']:
  try: fields[k]=str(o.get_editor_property(k))
  except Exception: pass
 r.append({'path':p,'fields':fields})
reg=unreal.AssetRegistryHelpers.get_asset_registry()
for p in ['/AscentCombatFramework/Blueprints/ACFCharacterBP','/AscentCombatFramework/Blueprints/AI/AIs/ACFAICharacterBP','/AscentCombatFramework/Blueprints/Actors/ACF_Enemy_BP','/AscentCombatFramework/Integrations/Actors/ACF_NPC_BP']:
 r.append({'path':p,'parent':str(e.find_asset_data(p).get_tag_value('ParentClass'))})
(out/'data.json').write_text(json.dumps(r,indent=2))
print(json.dumps(r))
