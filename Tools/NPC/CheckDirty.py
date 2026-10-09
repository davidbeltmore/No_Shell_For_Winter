import unreal
print('DIRTY_CONTENT='+str([p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]))
print('DIRTY_MAPS='+str([p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]))
