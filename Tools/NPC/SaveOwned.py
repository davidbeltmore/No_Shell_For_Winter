"""Save only task-owned dirty packages before a cold validation build."""
import unreal
dirty=list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
allowed=('/Game/_Game/Characters/','/Game/_Game/Data/CharacterCreation/DA_BodyPresets','/Game/_Game/Hub/HUB')
assert all(p.get_name().startswith(allowed) for p in dirty),[p.get_name() for p in dirty]
if dirty: assert unreal.EditorLoadingAndSavingUtils.save_packages(dirty,True)
print('Saved owned packages: '+str(len(dirty)))
