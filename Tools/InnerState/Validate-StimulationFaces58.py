"""Bounded real HUB HUD test: three faces, exact thresholds and authored screen order."""
import unreal
import time
import json
import traceback
from pathlib import Path

face_out = Path(unreal.Paths.project_saved_dir()).resolve() / 'Migration/StimulationFace20261005' / ('PIE_' + time.strftime('%Y%m%d_%H%M%S'))
face_out.mkdir(parents=True, exist_ok=True)
face_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not face_editor.is_in_play_in_editor(), 'Run in a fresh disposable PIE session.'
assert 'HUB' in unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
face_perf = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
face_throttle = face_perf.get_editor_property('bThrottleCPUWhenNotForeground')
face_perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
face_cases = [(50, 'StimulationHappy', 'happy'), (-50, 'StimulationSad', 'sad'), (0, 'Stimulation', 'neutral'),
              (-20, 'Stimulation', None), (20, 'Stimulation', None), (-20.01, 'StimulationSad', None), (20.01, 'StimulationHappy', None)]
face_cases = [(v, symbol, shot, False) for v, symbol, shot in face_cases] + [(20.00001, 'Stimulation', None, True), (-20.00001, 'Stimulation', None, True)]
face_state = {'started': time.monotonic(), 'index': 0, 'applied': False, 'checks': [], 'world': None}

def face_finish(error=None):
    unreal.unregister_slate_post_tick_callback(face_handle)
    if face_state['world']:
        unreal.GameplayStatics.set_game_paused(face_state['world'], False)
    unreal.EditorLevelLibrary.editor_end_play()
    face_perf.set_editor_property('bThrottleCPUWhenNotForeground', face_throttle)
    shots = {name: [str(p) for p in face_out.glob(name + '*.png')] for name in ('happy', 'sad', 'neutral')}
    report = {'passed': not error and len(face_state['checks']) == len(face_cases) and all(c['passed'] for c in face_state['checks']) and all(shots.values()),
              'error': error, 'wall_seconds': time.monotonic()-face_state['started'], 'checks': face_state['checks'], 'screenshots': shots,
              'map': str(face_state['world'].get_path_name()) if face_state['world'] else None}
    (face_out/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Stimulation HUD QA: ' + json.dumps(report))

def face_tick(delta):
    try:
        age = time.monotonic()-face_state['started']
        if age > 45:
            face_finish('45-second watchdog'); return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
        if not pawn or age < 8: return
        face_state['world'] = world
        inner = pawn.get_component_by_class(unreal.ProjectInnerStateComponent)
        cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectSurvivalNeedsSubsystem')
        hud = next((s for s in unreal.ObjectIterator(cls) if s.get_path_name().startswith(world.get_path_name())), None)
        if not inner or not hud: return
        if face_state['index'] >= len(face_cases):
            face_finish(); return
        value, expected, shot, automatic = face_cases[face_state['index']]
        if not face_state['applied']:
            # Freeze physiological drift only while checking exact boundary values.
            unreal.GameplayStatics.set_game_paused(world, True)
            inner.set_debug_value('Stimulation', value)
            hud.set_needs_hud_visible(True)
            if automatic: unreal.GameplayStatics.set_game_paused(world, False)
            face_state['applied'] = True
            face_state['applied_at'] = time.monotonic()
            return
        if time.monotonic()-face_state['applied_at'] < (2.5 if automatic else 1.5): return
        if automatic: unreal.GameplayStatics.set_game_paused(world, True)
        card_cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectInnerDoctrineAttributeCardWidget')
        cards = [c for c in unreal.ObjectIterator(card_cls) if c.get_owning_player_pawn() == pawn and c.is_visible()]
        def owning_card(obj):
            outer = obj.get_outer()
            while outer:
                if outer in cards: return outer
                outer = outer.get_outer()
            return None
        glyph_cls = unreal.load_class(None, '/Script/EFProjectSystemsGameplay.ProjectInnerStateGlyph')
        by_symbol = {str(g.get_editor_property('Symbol')): (owning_card(g), g) for g in unreal.ObjectIterator(glyph_cls) if owning_card(g)}
        first, glyph = next(pair for name, pair in by_symbol.items() if name.startswith('Stimulation'))
        second, stress_glyph = by_symbol['Stress']
        value_text = next(t for t in unreal.ObjectIterator(unreal.TextBlock) if t.get_name().startswith('ValueText') and owning_card(t) == first)
        row = first.get_parent()
        first_index = row.get_child_index(first) if row else -1
        second_index = row.get_child_index(second) if row else -1
        order_ok = isinstance(row, unreal.HorizontalBox) and second.get_parent() == row and first_index == 0 and second_index == 1
        symbol = str(glyph.get_editor_property('Symbol'))
        label = str(value_text.get_text())
        tooltip = str(first.tool_tip_text)
        actual = inner.get_value('Stimulation')
        value_ok = abs(actual) < 20 and round(actual) == round(value) if automatic else actual == value
        face_state['checks'].append({'automatic_crossing': automatic, 'value': value, 'actual_value': inner.get_value('Stimulation'), 'glyph': symbol, 'label': label,
            'stimulation_index': first_index, 'stress_index': second_index, 'row': row.get_path_name() if row else None, 'tooltip': tooltip,
            'passed': value_ok and symbol == expected and str(stress_glyph.get_editor_property('Symbol')) == 'Stress' and order_ok and 'Stimulation:' in tooltip and (value <= 0 or label.startswith('+'))})
        if shot:
            unreal.SystemLibrary.execute_console_command(world, 'Shot SHOWUI filename="' + str(face_out/shot).replace('\\','/') + '"')
        face_state['index'] += 1
        face_state['applied'] = False
    except Exception:
        face_finish(traceback.format_exc())

face_handle = unreal.register_slate_post_tick_callback(face_tick)
face_editor.editor_request_begin_play()
print('Stimulation HUD QA scheduled; hard limit45s; output=' + str(face_out))
