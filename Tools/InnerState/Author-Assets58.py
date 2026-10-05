"""Idempotent target-only Inner State item authoring. Run through RunEditorPython58."""
import json
from pathlib import Path
import unreal

ROOT = '/Game/_Game/InnerState'
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = []

def blueprint(name, parent, folder=ROOT + '/Items'):
    path = folder + '/' + name
    bp = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not bp:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('ParentClass', parent)
        bp = tools.create_asset(name, folder, unreal.Blueprint, factory)
    assert bp, path
    return bp

def influence(source, **values):
    result = unreal.ProjectInnerStateInfluence()
    for key, value in dict(Source=source, Family=source, Remaining=8.0, **values).items():
        result.set_editor_property(key, value)
    return result

items = [
 ('BP_Antidote', 'Antidote', 'Neutralizes all remaining poison. Does not restore lost health.', {'bAntidote': True}),
 ('BP_Medicine', 'Medicine', 'Treats sickness for 24 game hours: severity falls by 6 points per hour.', {'SicknessTreatmentHours': 24.0}),
 ('BP_Washcloth', 'Clean washcloth', 'Restores 25 hygiene up to 75. A full bath restores 100.', {'bPartialWash': True}),
 ('BP_HealthPotion', 'Restorative potion', 'Restores 25% of maximum health. Poison debt is unchanged.', {'HealthRestoreFraction': .25}),
 ('BP_ComfortTea', 'Comfort tea', 'A modest wellbeing benefit for 8 game hours. Repeated cups refresh the benefit.', {'Influences': [influence('ComfortTea', WellbeingPerHour=.2, StressTarget=-10.0)]}),
 ('BP_Analgesic', 'Analgesic', 'Relieves pain by 25 points. Mild drowsiness reduces awareness capacity by 10 for 2 game hours.', {'Influences': [influence('Analgesic', AwarenessCapacityLoss=10.0)]}),
 ('BP_RottenRation', 'Spoiled ration', 'Restores 15 hunger but adds 25 fast poison: 3 seconds incubation, then 1% reference maximum health per second.', {'PoisonPoints':25.0, 'PoisonSpeed':unreal.ProjectPoisonSpeed.FAST, 'Toxin':'Food.Spoiled'}),
]
for name, label, description, values in items:
    bp = blueprint(name, unreal.ProjectInnerStateConsumable)
    cdo = unreal.get_default_object(bp.generated_class())
    info = cdo.get_editor_property('ItemInfo')
    info.set_editor_property('Name', label)
    info.set_editor_property('Description', description)
    template = unreal.get_default_object(unreal.load_class(None, '/Game/FullSample/Blueprints/Items/Consumable/ACFHealthPotionBP.ACFHealthPotionBP_C')).get_editor_property('ItemInfo')
    info.set_editor_property('ThumbNail', unreal.load_asset('/Game/FullSample/UI/Icons/T_SM_Potion_C_Red_Icon' if name == 'BP_HealthPotion' else '/Game/FullSample/UI/Icons/T_SM_Potion_C_Blu_Icon'))
    info.set_editor_property('WorldMesh', template.get_editor_property('WorldMesh'))
    cdo.set_editor_property('ItemInfo', info)
    for key, value in values.items(): cdo.set_editor_property(key, value)
    profile = unreal.ProjectSurvivalConsumableProfile()
    profile.set_editor_property('SourceId', name)
    if name == 'BP_RottenRation':
        profile.set_editor_property('NeedDeltas', [unreal.ProjectSurvivalNeedDelta(entry_name='Hunger', delta_value=15.0)])
    if name == 'BP_ComfortTea':
        profile.set_editor_property('NeedDeltas', [unreal.ProjectSurvivalNeedDelta(entry_name='Thirst', delta_value=10.0)])
    if name == 'BP_Analgesic':
        profile.set_editor_property('SensationDeltas', [unreal.ProjectSurvivalNeedDelta(entry_name='Pain', delta_value=-25.0)])
        effects = cdo.get_editor_property('Influences'); effects[0].set_editor_property('Remaining',2.0); cdo.set_editor_property('Influences', effects)
    cdo.set_editor_property('SurvivalProfile', profile)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp), name
    report.append({'asset':bp.get_path_name(), 'status':str(bp.get_editor_property('Status'))})

# Preserve the public project poison path; the project subclass retains DOT fields
# but delegates ticking/damage to the one Inner State ledger.
poison = unreal.load_asset('/Game/FullSample/Blueprints/StatusEffects/ACF_PoisonStatus_BP')
unreal.BlueprintEditorLibrary.reparent_blueprint(poison, unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectACFPoisonStatus'))
unreal.BlueprintEditorLibrary.compile_blueprint(poison)
assert unreal.EditorAssetLibrary.save_loaded_asset(poison)
report.append({'asset':poison.get_path_name(), 'status':str(poison.get_editor_property('Status'))})

# A project damage type for blunt weapons; no Marketplace asset edits.
blunt = blueprint('BP_ConcussiveDamage', unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectConcussiveDamageType'), ROOT + '/Damage')
unreal.BlueprintEditorLibrary.compile_blueprint(blunt)
unreal.EditorAssetLibrary.save_loaded_asset(blunt)
report.append({'asset':blunt.get_path_name(), 'status':str(blunt.get_editor_property('Status'))})

# Author a placeable ACF pickup, without altering the user's existing HUB map.
supplies = blueprint('BP_SurvivalSupplies', unreal.load_class(None, '/Script/InventorySystem.ACFWorldItem'), ROOT + '/Pickups')
storage = unreal.get_default_object(supplies.generated_class()).get_editor_property('StorageComponent')
storage.set_editor_property('Items', [unreal.BaseItem(item_class=unreal.load_class(None, ROOT+'/Items/'+name+'.'+name+'_C'), count=count)
    for name, count in [('BP_Antidote',1),('BP_Medicine',1),('BP_Washcloth',3),('BP_ComfortTea',2),('BP_Analgesic',2),('BP_HealthPotion',1)]])
unreal.BlueprintEditorLibrary.compile_blueprint(supplies)
unreal.EditorAssetLibrary.save_loaded_asset(supplies)
report.append({'asset':supplies.get_path_name(), 'status':str(supplies.get_editor_property('Status'))})

out = Path(unreal.Paths.project_saved_dir()) / 'Migration/InnerState20261005/assets_authored.json'
out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
