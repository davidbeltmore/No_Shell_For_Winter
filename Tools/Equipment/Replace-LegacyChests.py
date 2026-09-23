"""Replace exact legacy HUB chest actors through Unreal, preserving unrelated actors.

Legacy assets are retired by a separate referencer-checked step after PIE passes.
"""
import json, shutil
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir());out=root/'Saved/EquipmentQA/StorageRework'
legacy=['/Game/_Game/Lockpicking/'+n for n in ['Locked','LockedChest','LockPickChest']]
legacy+=['/Game/_Game/Items/Chests/'+n for n in ['BP_CalystoLockedChest','BP_CalystoLockPickChest']]
native_deposit='/AscentCombatFramework/Integrations/Actors/ACF_Deposit_BP'
for path in ['/Game/_Game/Hub/HUB','/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector','/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy']:
    ext='.umap' if path.endswith('/HUB') else '.uasset'
    source=root/'Content'/(path[6:]+ext);backup=out/(path.split('/')[-1]+'.before_chest_replacement'+ext)
    if not backup.exists(): shutil.copy2(source,backup)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/_Game/Hub/HUB')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
target=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentLockedChest.BP_EquipmentLockedChest_C')
rows=[]
for old in list(actors.get_all_level_actors()):
    path=old.get_class().get_path_name().split('.')[0]
    if path not in legacy and path!=native_deposit: continue
    original=old.get_actor_transform()
    inventory=old.get_component_by_class(unreal.ACFStorageComponent)
    initial=list(inventory.get_editor_property('items')) if inventory else []
    old_lock=old.get_component_by_class(unreal.ProjectLockpickableComponent)
    row={'old':old.get_path_name(),'class':path,'transform':str(original),'initial_items':str(initial),'lock':{}}
    concrete=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentDeposit.BP_EquipmentDeposit_C') if path==native_deposit else target
    replacement=actors.spawn_actor_from_class(concrete,old.get_actor_location(),old.get_actor_rotation())
    assert replacement
    replacement.set_actor_scale3d(old.get_actor_scale3d())
    replacement.set_actor_label(old.get_actor_label())
    replacement.set_folder_path(old.get_folder_path())
    replacement.set_editor_property('tags',old.get_editor_property('tags'))
    new_inventory=replacement.get_component_by_class(unreal.ProjectChestInventoryComponent)
    new_inventory.set_editor_property('items',initial)
    lock=replacement.get_component_by_class(unreal.ProjectLockpickableComponent)
    if old_lock and lock:
        for prop in ['starts_locked','difficulty','failure_cooldown_seconds','target_center','show_prompt_before_lockpicking']:
            value=old_lock.get_editor_property(prop);lock.set_editor_property(prop,value);row['lock'][prop]=str(value)
    if lock: lock.set_editor_property('require_lockpick_item',True)
    check=list(new_inventory.get_editor_property('items'))
    assert len(check)==len(initial)
    assert replacement.get_actor_transform()==original
    row['new']=replacement.get_path_name();rows.append(row)
    assert actors.destroy_actor(old)
assert level.save_current_level()
result=unreal.ProjectLockpickingEditorLibrary.remap_chest_catalogs()
assert result.startswith('PASS:'),result
director=unreal.load_asset('/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector')
errors=list(director.get_authoring_errors())
(out/'replacement.json').write_text(json.dumps({'actors':rows,'catalogs':result,'director_errors':errors},indent=2))
assert not errors,errors
print('PASS: Exact HUB chest replacement and catalog remap; legacy deletion pending runtime validation.')
