"""Read-only live data preflight. Run with Tools/InnerState/RunEditorPython58.py."""
import json
import time
from pathlib import Path
import unreal

start = time.monotonic()
report = {'status': 'PASS', 'project': unreal.Paths.project_dir(), 'settings': {}, 'garments': [], 'bodies': [], 'errors': []}
for cls, names in [('/Script/EFClothingMorphRuntime.EFClothingMorphV2Settings', ['bEnableForNonPlayerPawns']),
                   ('/Script/EFClothingMorphRuntime.EFClothingMorphSettings', ['MorphSyncIntervalSeconds', 'MaximumConcurrentBindingLoads'])]:
    obj = unreal.get_default_object(unreal.load_class(None, cls))
    report['settings'][cls] = {n: obj.get_editor_property(n) for n in names}
for name in ['RagShirt', 'RagPants', 'RagFootWrap']:
    path = '/Game/_Game/Clothes/Rags01/' + name
    obj = unreal.get_default_object(unreal.load_class(None, path + '.' + name + '_C'))
    report['garments'].append({'path': path, 'slots': [str(t.get_editor_property('TagName')) for t in obj.get_possible_item_slots()], 'class': obj.get_class().get_path_name()})
catalog = unreal.load_asset('/Game/_Game/Data/CharacterCreation/DA_BodyPresets')
for row in catalog.get_editor_property('bodies'):
    report['bodies'].append({n: str(row.get_editor_property(n)) for n in ['preset_id', 'mesh', 'enabled', 'allow_npc', 'npc_weight']})
report['seconds'] = time.monotonic() - start
report['pilot_equipment'] = []
for name in ['ACF_SkillGuy_BPMale', 'ACFMeleeEnemyBPMale']:
    cls = unreal.load_class(None, '/Game/_Game/Characters/Male/'+name+'.'+name+'_C')
    equipment = unreal.get_default_object(cls).get_component_by_class(unreal.ACFEquipmentComponent)
    report['pilot_equipment'].append({'class': cls.get_path_name(), 'slots': [str(t.get_editor_property('TagName')) for t in equipment.get_available_equipment_slot()]})
report['dirty_packages'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()] + [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
out = Path(unreal.Paths.project_dir())/'Saved/NPCOutfits'
out.mkdir(parents=True, exist_ok=True)
(out/'inspect.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
