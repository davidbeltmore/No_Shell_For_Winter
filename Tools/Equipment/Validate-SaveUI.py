"""Exercise original save/load page buttons against isolated QA slots only."""
import json
import os
import pathlib
import time
import traceback
import unreal

OUT = pathlib.Path(unreal.Paths.project_saved_dir()) / 'EquipmentQA' / os.environ['EF_EQUIPMENT_QA_RUN']
OUT.mkdir(parents=True, exist_ok=True)
S = {'phase': 'load', 'due': 0, 'start': time.perf_counter(), 'busy': False}
R = {'status': 'PENDING', 'checks': [], 'scope': 'Native save/load page pointer actions, isolated existing QA slot'}
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LE = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
QA = unreal.EFEquipmentAuthoringLibrary

def write():
    (OUT / 'result.json').write_text(json.dumps(R, indent=2), encoding='utf-8')
    (OUT / 'progress.txt').write_text(S['phase'], encoding='utf-8')

def check(value, name):
    R['checks'].append({'name': name, 'pass': bool(value)})
    write()
    assert value, name

def advance(phase, delay=0.5):
    S.update(phase=phase, due=time.perf_counter()+delay)
    write()

def within(obj, root):
    visited = set()
    while obj and obj.get_path_name() not in visited:
        if obj == root:
            return True
        visited.add(obj.get_path_name())
        parent = obj.get_parent() if isinstance(obj, unreal.Widget) else None
        obj = parent or obj.get_outer()
    return False

def descendants(root, cls):
    return [obj for obj in unreal.ObjectIterator(cls) if within(obj, root)]

def click(button, following):
    check(button is not None, 'Pointer target exists for ' + following)
    QA.send_inventory_pointer(button, 'Hover')
    check(QA.send_inventory_pointer(button, 'Down'), 'Native button accepts pointer press for ' + following)
    S['button'], S['following'] = button, following
    advance('release', 0.15)

def popup_button(label):
    popups = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.ANSNavPopUpWidget, False) if w.is_activated()]
    check(len(popups) == 1, 'Exactly one native confirmation popup is active')
    popup = popups[0]
    buttons = []
    for b in descendants(popup, unreal.Button):
        owner = b.get_outer().get_outer()
        labels = [str(t.get_text()).strip() for t in descendants(owner, unreal.TextBlock)]
        buttons.append((b, labels))
    R.setdefault('dialogs', []).append({'class': popup.get_class().get_path_name(), 'buttons': [{'path': b.get_path_name(), 'labels': labels} for b, labels in buttons]})
    matches = [b for b, labels in buttons if any(t.lower() == label.lower() for t in labels)]
    check(len(matches) == 1, 'Native popup has one ' + label + ' button')
    return matches[0]

def open_page(name):
    cls = unreal.load_class(None, '/Game/FullSample/Integrations/SharedAssets/Save/' + name + '.' + name + '_C')
    S['page'] = S['ui'].spawn_in_game_widget(cls, False, True, unreal.GameplayTag())
    check(S['page'] is not None, 'Native ' + name + ' opens')

def select_slot(following, rejected_name=None):
    cls = unreal.load_class(None, '/Game/FullSample/Integrations/SharedAssets/Save/ACF_SavegameButton_WB.ACF_SavegameButton_WB_C')
    rows = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], cls, False)
            if within(w, S['page']) and str(w.get_editor_property('SaveMetadata').get_editor_property('save_name')) == S['slot']]
    check(len(rows) == 1, 'Native save list displays exactly the isolated fixture slot')
    if rejected_name:
        S['rejected_row'] = rows[0]
        metadata = rows[0].get_editor_property('SaveMetadata')
        metadata.set_editor_property('save_name', rejected_name)
        rows[0].set_editor_property('SaveMetadata', metadata)
    click(QA.find_inventory_widget(rows[0], 'Button'), following)

def finish(error=None):
    if not QA.end_save_fixture():
        error = (error or '') + '\nQA save cleanup failed.'
    R['status'] = 'FAIL' if error else 'PASS'
    if error:
        R['error'] = error
    write()
    unreal.unregister_slate_post_tick_callback(S['callback'])
    unreal.EditorLevelLibrary.editor_end_play()
    unreal.SystemLibrary.quit_editor()

def tick(dt):
    if S['busy']:
        return
    S['busy'] = True
    try:
        now = time.perf_counter()
        if now-S['start'] > 300:
            raise RuntimeError('Save UI timeout in ' + S['phase'])
        if now < S['due']:
            return
        phase = S['phase']
        if phase == 'load':
            if ED.get_editor_world().get_name() != 'HUB':
                LE.load_level('/Game/_Game/Hub/HUB')
            LE.editor_request_begin_play()
            advance('setup', 4)
        elif phase == 'setup':
            world = ED.get_game_world()
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            if not pawn:
                return
            S['world'], S['pawn'] = world, pawn
            S['coordinator'] = unreal.EFEquipmentFunctionLibrary.get_equipment_coordinator(pawn)
            native = S['coordinator'].get_native_equipment()
            ids = []
            for path in ('/Game/_Game/Items/Equipment/BP_Satchel2', '/Game/_Game/Clothes/UnderWear01/Panty'):
                cls = unreal.load_class(None, path + '.' + path.rsplit('/', 1)[-1] + '_C')
                entries = native.get_all_items_of_class_in_inventory(cls)
                if not entries:
                    native.add_item_to_inventory_by_class(cls, 1, False)
                    entries = native.get_all_items_of_class_in_inventory(cls)
                check(bool(entries), 'Save UI fixture owns ' + path.rsplit('/', 1)[-1])
                ids.append(entries[0].get_editor_property('item_guid'))
            S['bag'], S['child'] = ids
            check(S['coordinator'].move_to_container(S['child'], S['bag'], 1).get_editor_property('success'), 'Save UI fixture stores real bag contents')
            S['coordinator'].request_equip(S['bag'], unreal.GameplayTag())
            S['capacity'] = S['coordinator'].get_weight_summary().get_editor_property('effective_weight') - 0.125
            S['coordinator'].set_base_carry_capacity(S['capacity'])
            prefix = QA.begin_save_fixture(world)
            check(bool(prefix), 'Save UI metadata is isolated')
            S['slot'] = prefix + 'World'
            S['reserved_slot'] = prefix + 'Metadata'
            check(QA.save_world_fixture(world), 'Create isolated existing slot for the original save page')
            S['path'] = pathlib.Path(unreal.Paths.project_saved_dir()) / 'SaveGames' / (S['slot'] + '.sav')
            S['original'] = S['path'].read_bytes()
            instance = unreal.GameplayStatics.get_game_instance(world)
            S['ui'] = next(obj for obj in unreal.ObjectIterator(unreal.ANSUIPlayerSubsystem) if obj.get_outer() == instance)
            S['native_save'] = next(obj for obj in unreal.ObjectIterator(unreal.ALSLoadAndSaveSubsystem) if obj.get_outer() == instance)
            open_page('ACF_SavePage_WB')
            advance('save_select', 2)
        elif phase == 'release':
            QA.send_inventory_pointer(S['button'], 'Up')
            advance(S['following'])
        elif phase == 'save_select':
            # A transient row requesting the reserved metadata name exercises
            # the real failure callback without corrupting any on-disk snapshot.
            select_slot('save_failure_confirm', S['reserved_slot'])
        elif phase == 'save_failure_confirm':
            click(popup_button('Yes'), 'save_failure_verify')
        elif phase == 'save_failure_verify':
            check(S['path'].read_bytes() == S['original'], 'Failed Save page action retains the original snapshot byte for byte')
            check(S['page'].is_activated(), 'Failed Save page action retains the native save page')
            error = str(unreal.EFEquipmentSaveLibrary.get_last_save_error(S['native_save']))
            check('reserved for the save list' in error, 'Failed Save page action reports its actual English error')
            check(not any(w.is_activated() for w in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.ANSNavPopUpWidget, False)), 'Failed Save page action does not display a success popup')
            metadata = S['rejected_row'].get_editor_property('SaveMetadata')
            metadata.set_editor_property('save_name', S['slot'])
            S['rejected_row'].set_editor_property('SaveMetadata', metadata)
            select_slot('save_confirm')
        elif phase == 'save_confirm':
            click(popup_button('Yes'), 'save_verify')
        elif phase == 'save_verify':
            check(S['path'].read_bytes() != S['original'], 'Original Save page button publishes a new verified world snapshot')
            click(popup_button('Ok'), 'open_load')
        elif phase == 'open_load':
            check(S['coordinator'].move_from_container(S['bag'], S['child'], 1).get_editor_property('success'), 'Change contents after UI save')
            S['coordinator'].set_base_carry_capacity(180)
            open_page('ACF_LoadPage_WB')
            advance('load_select', 2)
        elif phase == 'load_select':
            select_slot('load_confirm')
        elif phase == 'load_confirm':
            click(popup_button('Yes'), 'loaded')
        elif phase == 'loaded':
            world = ED.get_game_world()
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            if not pawn or pawn == S['pawn'] or QA.is_save_fixture_busy(world):
                return
            S['world'] = world
            coordinator = unreal.EFEquipmentFunctionLibrary.get_equipment_coordinator(pawn)
            check(abs(coordinator.get_weight_summary().get_editor_property('base_capacity')-S['capacity']) < 0.000001, 'Original Load page restores fractional base capacity')
            check(coordinator.get_weight_summary().get_editor_property('overburdened'), 'Original Load page accepts legitimate overburden')
            check(any(i.get_editor_property('item_guid').to_string() == S['child'].to_string() for i in coordinator.get_container_items(S['bag'])), 'Original Load page restores exact contained GUID')
            finish()
    except Exception:
        finish(traceback.format_exc())
    finally:
        S['busy'] = False

write()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['callback'] = unreal.register_slate_post_tick_callback(tick)
