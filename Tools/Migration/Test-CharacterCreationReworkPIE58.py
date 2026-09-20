"""Run through the local validation driver's exec action in a live HUB PIE session."""
import json

r = root()
def widget(name):
    return next(w for w in controls() if w.get_name() == name)
def rows():
    return [w for w in widget('MorphScrollBox').get_all_children() if isinstance(w, unreal.UserWidget)]
def invoke(name, *args):
    return r.call_method(name, args)
def entries():
    return r.get_displayed_morph_entries()

report = {}
def check(name, fn):
    try:
        report[name] = {'status': 'PASS', 'details': fn()}
    except Exception as e:
        report[name] = {'status': 'FAIL', 'error': str(e)}
    (OUT/'PIEFunctional.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

def categories():
    coverage = {}
    for gender, callback, expected in [('Female','HandleGenderFemaleClicked',446),('Male','HandleGenderMaleClicked',415)]:
        invoke(callback)
        seen = set()
        sections = {}
        for category in ['Head', 'Body']:
            r.select_presentation_category(category)
            r.select_presentation_section('All')
            all_entries = entries()
            all_keys = {str(e.target) + '|' + str(e.morph_name) for e in all_entries}
            assert len(all_keys) == len(all_entries), 'Duplicate controls'
            assert not seen.intersection(all_keys), 'Duplicate across tabs'
            seen.update(all_keys)
            classified = set()
            for section in sorted({e.section for e in all_entries}):
                r.select_presentation_section(section)
                part = entries()
                assert part and all(e.section == section for e in part)
                classified.update(str(e.target) + '|' + str(e.morph_name) for e in part)
                sections[category + '/' + section] = len(part)
            assert classified == all_keys, 'Section lost controls'
        assert len(seen) == expected, (gender,len(seen),expected)
        coverage[gender] = {'count':len(seen),'sections':sections}
    invoke('HandleGenderFemaleClicked')
    return coverage

def search():
    r.select_presentation_category('Body')
    r.select_presentation_section('Feet')
    widget('SearchTextBox').set_text('Body Pear Figure')
    matches = entries()
    assert len(matches) == 1 and matches[0].section == 'General & Proportions'
    widget('SearchTextBox').set_text('')
    assert all(e.section == 'Feet' for e in entries())
    return 'Search crosses subcategories and restores selected section'

def morph():
    r.select_presentation_category('Head')
    r.select_presentation_section('Eyes')
    row = rows()[0]
    # Python struct views borrow their backing Unreal array: keep it alive.
    data = entries()
    entry = data[0]
    pawn = unreal.GameplayStatics.get_player_pawn(world(),0)
    meshes = pawn.get_components_by_class(unreal.SkeletalMeshComponent)
    row.call_method('HandleSliderValueChanged',(0.25,))
    changed = [m.get_morph_target(entry.morph_name) for m in meshes]
    assert any(abs(v-0.25)<0.001 for v in changed), changed
    row.call_method('HandleResetClicked')
    reset = [m.get_morph_target(entry.morph_name) for m in meshes]
    assert all(abs(v-entry.default_value)<0.001 for v in reset), reset
    return {'name':str(entry.morph_name),'changed':changed,'reset':reset}

def presets():
    combo = widget('PresetComboBox')
    previous = [combo.get_option_at_index(i) for i in range(combo.get_option_count()) if combo.get_option_at_index(i) != 'EFCC_Rework_QA_20260920']
    r.select_presentation_category('Head')
    r.select_presentation_section('Eyes')
    row=rows()[0]
    row.call_method('HandleSliderValueChanged',(0.25,))
    widget('PresetNameTextBox').set_text('EFCC_Rework_QA_20260920')
    invoke('HandleSavePresetClicked')
    row.call_method('HandleResetClicked')
    invoke('HandleLoadPresetClicked')
    row_prefix = rows()[0].get_path_name()+'.'
    values=[str(w.get_text()) for w in controls() if isinstance(w,unreal.TextBlock) and w.get_path_name().startswith(row_prefix)]
    assert '0.25' in values, values
    rows()[0].call_method('HandleResetClicked')
    return {'round_trip':'PASS','previous_presets':previous,'legacy_load':'PENDING' if not previous else 'PENDING separate visual check'}

for test_name, test_fn in [('live_mesh_coverage',categories),('cross_category_search',search),('morph_callback_and_reset',morph),('preset_round_trip',presets)]:
    if not globals().get('cc_rework_only') or test_name in cc_rework_only:
        check(test_name,test_fn)
r.select_presentation_category('Head')
r.select_presentation_section('Eyes')
result=report
(OUT/'PIEFunctional.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
