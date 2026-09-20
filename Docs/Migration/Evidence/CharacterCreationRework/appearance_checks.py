import json
r=root()
r.select_presentation_category('Skin')
scroll=next(w for w in controls() if w.get_name()=='MorphScrollBox')
def descend(w):
    yield w
    if isinstance(w,unreal.PanelWidget):
        for c in w.get_all_children():
            yield from descend(c)
children=list(descend(scroll))
sliders=[w for w in children if isinstance(w,unreal.Slider)]
swatches=[w for w in children if isinstance(w,unreal.Border) and 'NoTheme' in w.get_name()]
assert len(sliders)==6 and len(swatches)==2
initial=[s.get_value() for s in sliders]
report={}
for i, callback in [(0,'HandleSkinHueChanged'),(1,'HandleIrisHueChanged')]:
    previous=str(swatches[i].get_editor_property('BrushColor'))
    sliders[i*3].set_value(0.33)
    sliders[i*3+1].set_value(0.4)
    r.call_method(callback,(0.33,))
    changed=str(swatches[i].get_editor_property('BrushColor'))
    assert changed!=previous
    for j in range(3): sliders[i*3+j].set_value(initial[i*3+j])
    r.call_method(callback,(initial[i*3],))
    assert str(swatches[i].get_editor_property('BrushColor'))==previous
    report[callback]='PASS'
meshes=unreal.GameplayStatics.get_player_pawn(world(),0).get_components_by_class(unreal.SkeletalMeshComponent)
r.call_method('HandlePauseAnimationChanged',(True,))
paused=[m.get_editor_property('bPauseAnims') for m in meshes]
assert any(paused)
r.call_method('HandlePauseAnimationChanged',(False,))
report['pause_animation']='PASS'
report['hair_conditional_hidden']=next(w for w in controls() if isinstance(w,unreal.TextBlock) and str(w.get_text())=='Hair').get_parent().get_visibility()==unreal.SlateVisibility.COLLAPSED
result=report
(OUT/'AppearanceControls.json').write_text(json.dumps(result,indent=2))
