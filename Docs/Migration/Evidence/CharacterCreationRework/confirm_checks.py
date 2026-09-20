import json
def current_rows():
    root().select_presentation_category('Head')
    root().select_presentation_section('Eyes')
    scroll=next(w for w in controls() if w.get_name()=='MorphScrollBox')
    return [w for w in scroll.get_all_children() if isinstance(w,unreal.UserWidget)]
def value():
    data=root().get_displayed_morph_entries()
    pawn=unreal.GameplayStatics.get_player_pawn(world(),0)
    return pawn.get_components_by_class(unreal.SkeletalMeshComponent)[0].get_morph_target(data[0].morph_name)
report={}
row=current_rows()[0]
baseline=value()
row.call_method('HandleSliderValueChanged',(0.31,))
root().call_method('HandleBackClicked')
assert root() is None
assert creator().open_character_creation_for_automation()
current_rows()
assert abs(value()-baseline)<0.001
report['cancel']='PASS'
current_rows()[0].call_method('HandleSliderValueChanged',(0.29,))
root().call_method('HandleStartGameClicked')
assert root() is None
assert creator().open_character_creation_for_automation()
current_rows()
assert abs(value()-0.29)<0.001
report['apply']='PASS'
current_rows()[0].call_method('HandleSliderValueChanged',(baseline,))
root().call_method('HandleStartGameClicked')
assert creator().open_character_creation_for_automation()
root().select_presentation_category('Head')
root().select_presentation_section('Eyes')
result=report
(OUT/'ConfirmCancel.json').write_text(json.dumps(result,indent=2))
