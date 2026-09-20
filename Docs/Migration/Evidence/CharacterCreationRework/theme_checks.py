import json
r=root()
r.select_presentation_category('Skin')
ws=controls()
swatches=[w for w in ws if isinstance(w,unreal.Border) and 'AppearanceSwatchNoTheme' in w.get_name()]
before=[str(w.get_editor_property('BrushColor')) for w in swatches]
assert len(swatches)==2
theme=subsystem('/Script/EFProjectSystemsUI.EFProjectDynamicThemeSubsystem')
panel=next(w for w in ws if w.get_name()=='RightPanelBorder')
report={}
for preset in ['Red','Blue','Purple','Green','Black']:
    theme.set_theme_preset(getattr(unreal.EFProjectHUDThemePreset,preset.upper()))
    after=[str(w.get_editor_property('BrushColor')) for w in swatches]
    assert after==before, 'Appearance swatch recolored'
    brush=panel.get_editor_property('Background')
    report[preset]={'panel_tint':str(brush.get_editor_property('tint_color')),'swatches':after}
assert len({r['panel_tint'] for r in report.values()})==5
theme.set_theme_preset(unreal.EFProjectHUDThemePreset.BLUE)
result={'status':'PASS','profiles':report}
(OUT/'ThemeProfiles.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
