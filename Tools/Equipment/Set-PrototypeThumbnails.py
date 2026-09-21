"""Explicit prototype thumbnail migration; do not run during ordinary UI maintenance.
This replaces thumbnails on the six catalog garments and three prototype satchels.
"""
import json
import pathlib
import unreal

destination = '/Game/_Game/Widgets/Equipment/Assets/Icons'
# Replace only the known temporary/missing equipment thumbnails, using the same
# silhouette family as the native empty slots. Existing gameplay values are kept.
thumbnails = {
    '/Game/_Game/Clothes/UnderWear01/Bra': destination + '/T_Equipment_UnderwearTorso',
    '/Game/_Game/Clothes/UnderWear01/Panty': destination + '/T_Equipment_UnderwearLegs',
    '/Game/_Game/Clothes/Bikini01/Bikini': destination + '/T_Equipment_UnderwearLegs',
    '/Game/_Game/Clothes/Rags01/RagShirt': '/Game/FullSample/Assets/UI/Icons/armor',
    '/Game/_Game/Clothes/Rags01/RagPants': '/Game/FullSample/Assets/UI/Icons/men-legs',
    '/Game/_Game/Clothes/Rags01/RagFootWrap': destination + '/T_Equipment_Feet',
}
for tier in range(1, 4):
    thumbnails[f'/Game/_Game/Items/Equipment/BP_Satchel{tier}'] = destination + '/T_Equipment_Satchel'
for path, texture_path in thumbnails.items():
    bp = unreal.load_asset(path)
    cls = unreal.load_class(None, path + '.' + path.rsplit('/', 1)[-1] + '_C')
    default = unreal.get_default_object(cls)
    descriptor = default.get_item_info()
    descriptor.set_editor_property('thumb_nail', unreal.load_asset(texture_path))
    default.set_item_descriptor(descriptor)
    unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
print("PASS: Explicit prototype thumbnail migration completed.")
