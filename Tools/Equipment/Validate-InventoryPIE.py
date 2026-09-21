"""Bounded real-player inventory integration fixture. Never saves packages or slots."""
import json
import os
import pathlib
import time
import traceback
import unreal

OUT = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA' / os.environ.get('EF_EQUIPMENT_QA_RUN', 'PIE_01')
OUT.mkdir(parents=True, exist_ok=True)
S = {'phase': 'load', 'due': 0, 'start': time.perf_counter(), 'busy': False}
R = {'status': 'PENDING', 'scope': 'Real HUB player, existing ACF menu, no asset or save-game writes', 'checks': []}
LE = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

def write():
    (OUT / 'result.json').write_text(json.dumps(R, indent=2), encoding='utf-8')

def advance(phase, delay=0):
    S.update(phase=phase, due=time.perf_counter()+delay)
    R['phase'] = phase
    write()
    (OUT / 'progress.txt').write_text(phase)

def check(condition, name):
    R['checks'].append({'name': name, 'pass': bool(condition)})
    write()
    assert condition, name

def finish(error=None):
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
        if now-S['start'] > 360:
            raise RuntimeError('PIE fixture timeout in ' + S['phase'])
        if now < S['due']:
            return
        phase = S['phase']
        if phase == 'load':
            if ED.get_editor_world().get_name() != 'HUB':
                LE.load_level('/Game/_Game/Hub/HUB')
            advance('play', 1)
        elif phase == 'play':
            LE.editor_request_begin_play()
            advance('player', 4)
        elif phase == 'player':
            world = ED.get_game_world()
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            if not pawn:
                return
            S['world'], S['pawn'] = world, pawn
            S['coordinator'] = unreal.EFEquipmentFunctionLibrary.get_equipment_coordinator(pawn)
            check(S['coordinator'] is not None, 'Player receives composed equipment coordinator')
            native = S['coordinator'].get_native_equipment()
            check(native is not None, 'Coordinator uses player ACF inventory')
            S['native'] = native
            for item in native.get_inventory():
                if item.get_editor_property('is_equipped'):
                    S['coordinator'].request_unequip(item.get_editor_property('item_guid'))
            S['items'] = {}
            for name, path in {
                'Bra': '/Game/_Game/Clothes/UnderWear01/Bra',
                'Panty': '/Game/_Game/Clothes/UnderWear01/Panty',
                'Shirt': '/Game/_Game/Clothes/Rags01/RagShirt',
                'Pants': '/Game/_Game/Clothes/Rags01/RagPants',
                'Bikini': '/Game/_Game/Clothes/Bikini01/Bikini',
                'Satchel': '/Game/_Game/Items/Equipment/BP_Satchel2',
            }.items():
                cls = unreal.load_class(None, path + '.' + path.rsplit('/', 1)[-1] + '_C')
                entries = native.get_all_items_of_class_in_inventory(cls)
                if not entries:
                    native.add_item_to_inventory_by_class(cls, 1, False)
                    entries = native.get_all_items_of_class_in_inventory(cls)
                check(bool(entries), 'Fixture owns ' + name)
                S['items'][name] = entries[0]
            for name in ('Shirt', 'Pants', 'Bikini', 'Satchel'):
                unreal.EFEquipmentFunctionLibrary.use_inventory_item(native, S['items'][name])
            advance('verify', 3)
        elif phase == 'verify':
            views = S['coordinator'].get_slot_views()
            check(len(views) == 14, 'Registry exposes all 14 slots')
            check(any(v.get_editor_property('reserved') for v in views), 'Bikini reserves an additional slot')
            controller = unreal.GameplayStatics.get_player_controller(S['world'], 0)
            instance = unreal.GameplayStatics.get_game_instance(S['world'])
            candidates = [obj for obj in unreal.ObjectIterator(unreal.ANSUIPlayerSubsystem) if obj.get_outer() == instance]
            check(len(candidates) == 1, 'Exactly one native ACF UI subsystem for this game instance')
            ui = candidates[0]
            menu_class = unreal.load_class(None, '/Game/FullSample/Integrations/UIIntegrations/Widgets/ACF_InGameMenu_WB.ACF_InGameMenu_WB_C')
            S['menu'] = ui.spawn_in_game_widget(menu_class, False, True, unreal.GameplayTag())
            check(S['menu'] is not None, 'Existing ACF menu opened through native navigation')
            advance('ui', 3)
        elif phase == 'ui':
            panels = unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.EFEquipmentNativeLayoutWidget, False)
            check(bool(panels), 'Original ACF equipment panel uses the project behavior adapter')
            panel = panels[0]
            S['panel'] = panel
            check(len(panel.get_editor_property('outerwear_slots')) == 10, 'Ten outerwear slots configured')
            check(len(panel.get_editor_property('additional_slots')) == 3, 'Three fixed underwear and satchel slots configured')
            R['capture_components'] = []
            for comp in S['pawn'].get_components_by_class(unreal.SceneCaptureComponent2D):
                R['capture_components'].append({'name': comp.get_name(), 'class': comp.get_class().get_path_name(),
                    'active': comp.get_editor_property('capture_every_frame'), 'location': str(comp.get_world_location()),
                    'rotation': str(comp.get_world_rotation()), 'target': str(comp.get_editor_property('texture_target'))})
            find = unreal.EFEquipmentAuthoringLibrary.find_inventory_widget
            S['scroll'] = find(panel, 'EFOuterwearScroll')
            S['outer'] = find(panel, 'VerticalList')
            S['additional'] = find(panel, 'EFAdditionalList')
            inventory_class = unreal.load_class(None, '/Game/FullSample/Integrations/UIIntegrations/Widgets/ACF_Inventory_WB.ACF_Inventory_WB_C')
            inventories = unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], inventory_class, False)
            R['layout'] = {}
            for root_name, root, names in [('equipment', panel, ['SizeBox_2', 'HorizontalBox_71', 'EFOuterwearViewport']),
                                           ('inventory', inventories[0], ['Equipment', 'SizeBox', 'ItemInfo', 'SizeBox_2'])]:
                for name in names:
                    widget = find(root, name)
                    R['layout'][root_name + '.' + name] = {'size': str(unreal.SlateLibrary.get_local_size(widget.get_paint_space_geometry())),
                        'slot': str(widget.get_editor_property('slot'))}
                    slot = widget.get_editor_property('slot')
                    if isinstance(slot, unreal.HorizontalBoxSlot):
                        R['layout'][root_name + '.' + name]['allocation'] = str(slot.get_editor_property('size'))
            check(S['outer'].get_children_count() == 10, 'Ten native icons are present in the scroll column')
            check(S['additional'].get_children_count() == 3, 'Three native icons are present in the fixed column')
            check(find(panel, 'EFOuterwearViewport').get_editor_property('height_override') == 400, 'Viewport fits exactly five 80-unit native slots')
            check(unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['scroll'], 'Wheel', -5), 'Wheel event is handled over the slot column')
            advance('wheel_down', 0.6)
        elif phase == 'wheel_down':
            R['wheel_down_offset'] = S['scroll'].get_scroll_offset()
            check(R['wheel_down_offset'] > 0, 'Mouse wheel scrolls toward the remaining slots')
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['scroll'], 'Wheel', 20)
            advance('wheel_up', 0.6)
        elif phase == 'wheel_up':
            check(S['scroll'].get_scroll_offset() < 0.1, 'Reverse wheel returns to the first five slots')
            candidates = unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.EFEquipmentInventoryIconWidget, False)
            bra_class = S['items']['Bra'].get_editor_property('item_class')
            candidates = [w for w in candidates if w.get_editor_property('item').get_editor_property('item_class') == bra_class]
            check(bool(candidates), 'Native inventory contains the draggable Bra icon')
            S['drag_source'] = candidates[0]
            S['drop_target'] = S['additional'].get_child_at(0)
            check(unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['drag_source'], 'Down'), 'Inventory icon accepts pointer press')
            advance('drag_start', 0.1)
        elif phase == 'drag_start':
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['drag_source'], 'Drag', 0, unreal.Vector2D(24, 0))
            advance('drag_move', 0.1)
        elif phase == 'drag_move':
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['drop_target'], 'Drag')
            advance('drag_drop', 0.1)
        elif phase == 'drag_drop':
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['drop_target'], 'Up')
            advance('drag_verify', 0.5)
        elif phase == 'drag_verify':
            bra_guid = S['items']['Bra'].get_editor_property('item_guid')
            R['drag_message'] = str(S['coordinator'].get_last_message())
            R['drag_guid'] = bra_guid.to_string()
            R['equipped_guids'] = [v.get_editor_property('owner_guid').to_string() for v in S['coordinator'].get_slot_views()]
            check(R['drag_guid'] in R['equipped_guids'],
                  'Actual Slate drag/drop equips Bra through the coordinator')
            check(not any(v.get_editor_property('reserved') for v in S['coordinator'].get_slot_views()),
                  'Dropping Bra displaces the whole Bikini and releases its second slot')
            S['coordinator'].request_equip(S['items']['Bikini'].get_editor_property('item_guid'), unreal.GameplayTag())
            S['contents_button'] = unreal.EFEquipmentAuthoringLibrary.find_inventory_widget(S['panel'], 'EFOpenSatchelButton')
            check(unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['contents_button'], 'Down'), 'Contents button accepts pointer press')
            advance('bag_open', 0.1)
        elif phase == 'bag_open':
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['contents_button'], 'Up')
            advance('bag_ui', 0.5)
        elif phase == 'bag_ui':
            windows = unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.EFEquipmentSatchelWindowWidget, False)
            check(len(windows) == 1, 'Contents opens one native navigation window')
            S['bag_window'] = windows[0]
            panels = unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.EFEquipmentPanelWidget, False)
            check(len(panels) == 1, 'Satchel window has one bound contents panel')
            S['bag_panel'] = panels[0]
            find = unreal.EFEquipmentAuthoringLibrary.find_inventory_widget
            S['root_rows'] = find(panels[0], 'InventoryRows')
            S['bag_rows'] = find(panels[0], 'ContainerRows')
            check(S['root_rows'].get_children_count() > 0, 'Store list contains root items')
            # The first visible row is a whole, unique starting item in this fixture.
            S['stored_item'] = S['native'].get_inventory()[0]
            check(S['stored_item'].get_editor_property('count') == 1, 'Whole-item transfer fixture is unique')
            row = S['root_rows'].get_child_at(0)
            S['store_button'] = find(row, 'TransferButton')
            S['before_weight'] = S['coordinator'].get_weight_summary().get_editor_property('effective_weight')
            check(unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['store_button'], 'Down'), 'Store button accepts pointer press')
            advance('bag_store', 0.1)
        elif phase == 'bag_store':
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['store_button'], 'Up')
            advance('bag_store_verify', 0.5)
        elif phase == 'bag_store_verify':
            contents = S['coordinator'].get_container_items(S['items']['Satchel'].get_editor_property('item_guid'))
            guid = S['stored_item'].get_editor_property('item_guid').to_string()
            check(any(i.get_editor_property('item_guid').to_string() == guid for i in contents), 'Store click transfers the original GUID into the satchel')
            check(not any(i.get_editor_property('item_guid').to_string() == guid for i in S['native'].get_inventory()), 'Stored item is absent from root inventory')
            check(S['coordinator'].get_weight_summary().get_editor_property('effective_weight') < S['before_weight'], 'Equipped satchel reduces only transferred content weight')
            check(S['bag_rows'].get_children_count() == 1, 'Contents list refreshes after transfer')
            S['extract_button'] = unreal.EFEquipmentAuthoringLibrary.find_inventory_widget(S['bag_rows'].get_child_at(0), 'TransferButton')
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['extract_button'], 'Down')
            advance('bag_extract', 0.1)
        elif phase == 'bag_extract':
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['extract_button'], 'Up')
            advance('bag_extract_verify', 0.5)
        elif phase == 'bag_extract_verify':
            guid = S['stored_item'].get_editor_property('item_guid').to_string()
            check(any(i.get_editor_property('item_guid').to_string() == guid for i in S['native'].get_inventory()), 'Extract click restores the original GUID to root')
            check(S['bag_rows'].get_children_count() == 0, 'Extraction removes the content row')
            check(abs(S['coordinator'].get_weight_summary().get_editor_property('effective_weight') - S['before_weight']) < 0.0001, 'Round trip conserves original effective weight')
            R['status'] = 'AWAITING_VISUAL_REVIEW'
            write()
            advance('review')
        elif phase == 'review':
            if (OUT / 'visual_done.txt').exists():
                review = (OUT / 'visual_done.txt').read_text().strip()
                finish(R.get('input_failure') or (review if review.startswith('FAIL:') else None))
    except Exception:
        if S['phase'].startswith('bag_') or S['phase'] in ('wheel_down', 'wheel_up', 'drag_start', 'drag_move', 'drag_drop', 'drag_verify'):
            R['input_failure'] = traceback.format_exc()
            R['status'] = 'FAILED_INPUT_AWAITING_VISUAL_REVIEW'
            advance('review')
        else:
            finish(traceback.format_exc())
    finally:
        S['busy'] = False

write()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['callback'] = unreal.register_slate_post_tick_callback(tick)
