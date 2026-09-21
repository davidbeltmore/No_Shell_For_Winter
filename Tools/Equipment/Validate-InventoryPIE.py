"""Real-player inventory fixture. No package writes; QA save slots use isolated metadata."""
import json
import os
import pathlib
import time
import traceback
import unreal

OUT = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA' / os.environ.get('EF_EQUIPMENT_QA_RUN', 'PIE_01')
OUT.mkdir(parents=True, exist_ok=True)
S = {'phase': 'load', 'due': 0, 'start': time.perf_counter(), 'busy': False}
R = {'status': 'PENDING', 'scope': 'Real HUB player, native menu, no asset writes; dedicated QA slots with isolated native save metadata', 'checks': []}
DEATH_REPEATS = max(1, min(32, int(os.environ.get('EF_EQUIPMENT_QA_DEATH_REPEATS', '1'))))
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

def death_snapshot():
    return {
        'message': str(S['coordinator'].get_last_message()),
        'root': [i.get_editor_property('item_guid').to_string() for i in S['native'].get_inventory()],
        'pickups': [{'actor': a.get_path_name(), 'items': [i.get_editor_property('item_guid').to_string() for i in a.get_items()]}
                    for a in unreal.GameplayStatics.get_all_actors_of_class(S['world'], unreal.EFEquipmentWorldItem)]}

def begin_death():
    check(unreal.EFEquipmentAuthoringLibrary.trigger_death_fixture(S['world']), 'Real player enters native ACF death through zero-health callback')
    R.setdefault('death_cycles', []).append({'immediate': death_snapshot()})
    advance('death_verify', 0.3)

def finish(error=None):
    if not unreal.EFEquipmentAuthoringLibrary.end_save_fixture():
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
            check(S['coordinator'].has_native_death_adapter(), 'Player native death callback is routed without changing the Player asset')
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
            coordinator = S['coordinator']
            bag_guid = S['items']['Satchel'].get_editor_property('item_guid')
            child_guid = S['items']['Panty'].get_editor_property('item_guid')
            original_capacity = coordinator.get_weight_summary().get_editor_property('base_capacity')
            check(coordinator.move_to_container(child_guid, bag_guid, 1).get_editor_property('success'), 'World fixture stores Panty inside the equipped satchel')
            drop = coordinator.request_drop(bag_guid, 1)
            check(drop.get_editor_property('success'), 'Real player drops the full satchel: ' + str(drop.get_editor_property('message')))
            pickups = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(S['world'], unreal.EFEquipmentWorldItem)
                       if any(i.get_editor_property('item_guid').to_string() == bag_guid.to_string() for i in actor.get_items())]
            check(len(pickups) == 1, 'Exactly one native world pickup owns the full satchel')
            pickup = pickups[0]
            R['world_pickup_class'] = pickup.get_class().get_path_name()
            R['world_pickup_location'] = str(pickup.get_actor_location())
            check(len(pickup.get_items()) == 1, 'Contained clothing is not duplicated as loose world loot')
            coordinator.set_base_carry_capacity(coordinator.get_weight_summary().get_editor_property('effective_weight'))
            pickup.on_interacted_by_pawn(S['pawn'], '')
            check(not pickup.was_pickup_completed() and len(pickup.get_items()) == 1, 'Blueprint interaction rejects overweight pickup without destroying the bag')
            coordinator.set_base_carry_capacity(original_capacity)
            pickup.on_interacted_by_pawn(S['pawn'], '')
            check(any(i.get_editor_property('item_guid').to_string() == bag_guid.to_string() for i in S['native'].get_inventory()), 'Second Blueprint interaction recovers the same bag GUID')
            check(any(i.get_editor_property('item_guid').to_string() == child_guid.to_string() for i in coordinator.get_container_items(bag_guid)), 'Contained item GUID survives the real world round trip')
            check(coordinator.move_from_container(bag_guid, child_guid, 1).get_editor_property('success'), 'Clothing can be extracted after the world round trip')
            check(coordinator.request_equip(bag_guid, unreal.GameplayTag()).get_editor_property('success'), 'Recovered bag equips again')
            advance('save_round_trip', 0.5)
        elif phase == 'save_round_trip':
            prefix = unreal.EFEquipmentAuthoringLibrary.begin_save_fixture(S['world'])
            check(bool(prefix), 'Save fixture isolates native metadata without writing settings')
            R['qa_save_prefix'] = prefix
            instance = unreal.GameplayStatics.get_game_instance(S['world'])
            native_save = next(obj for obj in unreal.ObjectIterator(unreal.ALSLoadAndSaveSubsystem) if obj.get_outer() == instance)
            coordinator = S['coordinator']
            bag_guid = S['items']['Satchel'].get_editor_property('item_guid')
            child_guid = S['items']['Panty'].get_editor_property('item_guid')
            check(coordinator.move_to_container(child_guid, bag_guid, 1).get_editor_property('success'), 'Save fixture contains a real item in the equipped bag')
            saved_capacity = coordinator.get_weight_summary().get_editor_property('effective_weight') - 0.125
            coordinator.set_base_carry_capacity(saved_capacity)
            check(coordinator.get_weight_summary().get_editor_property('overburdened'), 'Real player is overburdened before saving')
            saved = unreal.EFEquipmentSaveLibrary.save_local_player(native_save, prefix + 'Player')
            check(saved, 'Verified local-player save: ' + str(unreal.EFEquipmentSaveLibrary.get_last_save_error(native_save)))
            check(coordinator.move_from_container(bag_guid, child_guid, 1).get_editor_property('success'), 'Live inventory changes after the saved snapshot')
            coordinator.request_unequip(S['items']['Shirt'].get_editor_property('item_guid'))
            coordinator.set_base_carry_capacity(180.0)
            loaded = unreal.EFEquipmentSaveLibrary.load_local_player(native_save, prefix + 'Player', False)
            check(loaded, 'Real ALS player components load through the verified adapter: ' + str(unreal.EFEquipmentSaveLibrary.get_last_save_error(native_save)))
            check(abs(coordinator.get_weight_summary().get_editor_property('base_capacity') - saved_capacity) < 0.000001, 'Load restores separate fractional base capacity')
            check(coordinator.get_weight_summary().get_editor_property('overburdened'), 'Legitimate overburden survives player load')
            check(any(i.get_editor_property('item_guid').to_string() == child_guid.to_string() for i in coordinator.get_container_items(bag_guid)), 'Loaded full bag restores its contained GUID')
            check(any(v.get_editor_property('reserved') for v in coordinator.get_slot_views()), 'Load rebuilds actual Bikini reservation')
            legacy_slot = unreal.EFEquipmentAuthoringLibrary.create_legacy_save_fixture(S['world'])
            check(bool(legacy_slot), 'Create isolated pre-equipment legacy file with incompatible Bra and Bikini')
            legacy_path = pathlib.Path(unreal.Paths.project_saved_dir()) / 'SaveGames' / (legacy_slot + '.sav')
            S['legacy_original'] = legacy_path.read_bytes()
            check(unreal.EFEquipmentSaveLibrary.load_local_player(native_save, legacy_slot, False),
                  'Actual legacy file converts and loads: ' + str(unreal.EFEquipmentSaveLibrary.get_last_save_error(native_save)))
            save_adapter = next(obj for obj in unreal.ObjectIterator(unreal.EFEquipmentSaveSubsystem) if obj.get_outer() == instance)
            R['migration_summary'] = str(save_adapter.get_last_migration_summary())
            check('Unequipped incompatible items:' in R['migration_summary'] and 'remain in your inventory' in R['migration_summary'],
                  'Successful legacy load publishes the English displacement summary')
            bra_guid = S['items']['Bra'].get_editor_property('item_guid').to_string()
            check(any(i.get_editor_property('item_guid').to_string() == bra_guid and not i.get_editor_property('is_equipped') for i in S['native'].get_inventory()),
                  'Displaced Bra retains its GUID and remains unequipped in the real player inventory')
            check(any(v.get_editor_property('reserved') for v in coordinator.get_slot_views()), 'Legacy migration applies Bikini reservations before conflicting effects')
            check(legacy_path.read_bytes() != S['legacy_original'], 'Validated legacy slot publishes its converted format')
            backups = legacy_path.parent / 'EFEquipmentBackups' / legacy_slot
            check(any(p.read_bytes() == S['legacy_original'] for p in backups.glob('*.sav')), 'Migration retains an exact original-file backup')
            check(unreal.EFEquipmentSaveLibrary.load_local_player(native_save, prefix + 'Player', False), 'Restore versioned full-bag fixture after legacy check')
            check(not str(save_adapter.get_last_migration_summary()), 'Ordinary versioned load clears the previous migration notice')
            check(coordinator.request_drop(bag_guid, 1).get_editor_property('success'), 'Loaded full satchel can be dropped before world saving')
            S['world_saved_capacity'] = coordinator.get_weight_summary().get_editor_property('effective_weight') - 0.125
            coordinator.set_base_carry_capacity(S['world_saved_capacity'])
            check(unreal.EFEquipmentAuthoringLibrary.save_world_fixture(S['world']), 'Native world records publish with a full dropped bag: ' + str(unreal.EFEquipmentSaveLibrary.get_last_save_error(native_save)))
            coordinator.set_base_carry_capacity(180.0)
            check(unreal.EFEquipmentAuthoringLibrary.load_world_fixture(S['world']), 'Verified world load starts a real level reopen')
            advance('world_loaded', 4)
        elif phase == 'world_loaded':
            world = ED.get_game_world()
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
            if not pawn or pawn == S['pawn'] or unreal.EFEquipmentAuthoringLibrary.is_save_fixture_busy(world):
                return
            S['world'], S['pawn'] = world, pawn
            coordinator = unreal.EFEquipmentFunctionLibrary.get_equipment_coordinator(pawn)
            S['coordinator'], S['native'] = coordinator, coordinator.get_native_equipment()
            bag_guid = S['items']['Satchel'].get_editor_property('item_guid')
            child_guid = S['items']['Panty'].get_editor_property('item_guid')
            check(abs(coordinator.get_weight_summary().get_editor_property('base_capacity') - S['world_saved_capacity']) < 0.000001, 'Level reopen restores fractional base capacity')
            check(coordinator.get_weight_summary().get_editor_property('overburdened'), 'Level reopen accepts an overburdened player')
            pickups = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.EFEquipmentWorldItem)
                       if any(item.get_editor_property('item_guid').to_string() == bag_guid.to_string() for item in actor.get_items())]
            check(len(pickups) == 1, 'Exactly one saved world pickup restores the original satchel GUID')
            check(not any(item.get_editor_property('item_guid').to_string() == bag_guid.to_string() for item in S['native'].get_inventory()), 'Dropped bag is absent from the loaded player inventory')
            coordinator.set_base_carry_capacity(180.0)
            pickups[0].on_interacted_by_pawn(pawn, '')
            check(any(item.get_editor_property('item_guid').to_string() == child_guid.to_string() for item in coordinator.get_container_items(bag_guid)), 'Contained GUID survives native world serialization and pickup')
            check(coordinator.move_from_container(bag_guid, child_guid, 1).get_editor_property('success'), 'World-loaded content remains extractable')
            check(coordinator.request_equip(bag_guid, unreal.GameplayTag()).get_editor_property('success'), 'World-loaded satchel equips')
            check(any(view.get_editor_property('reserved') for view in coordinator.get_slot_views()), 'World load rebuilds Bikini reservation')
            R['pickup_name_reuse'] = unreal.EFEquipmentAuthoringLibrary.probe_pickup_name_reuse(world)
            check(R['pickup_name_reuse'].startswith('PASS:'), R['pickup_name_reuse'])
            advance('open_menu', 1)
        elif phase == 'open_menu':
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
            S['close_button'] = unreal.EFEquipmentAuthoringLibrary.find_inventory_widget(S['bag_window'], 'CloseButton')
            check(unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['close_button'], 'Down'), 'Back to inventory accepts pointer press')
            advance('bag_close', 0.1)
        elif phase == 'bag_close':
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['close_button'], 'Up')
            advance('bag_close_verify', 0.5)
        elif phase == 'bag_close_verify':
            check(not S['bag_window'].is_activated(), 'Back to inventory closes the satchel window')
            check(S['menu'].is_activated(), 'Native inventory navigation resumes after closing satchel')
            S['vendor'] = unreal.EFEquipmentAuthoringLibrary.create_vendor_fixture(S['world'])
            check(S['vendor'] is not None, 'PIE vendor uses the native trade component')
            S['trade_bag'] = S['items']['Satchel'].get_editor_property('item_guid')
            S['trade_child'] = S['stored_item'].get_editor_property('item_guid')
            check(S['coordinator'].move_to_container(S['trade_child'], S['trade_bag'], 1).get_editor_property('success'), 'Sale fixture contains a unique item')
            S['trade_currency'] = S['native'].get_current_currency_amount()
            unreal.EFEquipmentFunctionLibrary.sell_items_to_vendor(S['vendor'], S['items']['Satchel'], S['pawn'], 1)
            advance('trade_cancel_press', 0.5)
        elif phase in ('trade_cancel_press', 'trade_sell_press', 'trade_buy_press'):
            windows = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.EFEquipmentSaleWidget, False) if w.is_activated()]
            check(len(windows) == 1, 'Trade opens exactly one active confirmation')
            S['trade_window'] = windows[0]
            find = unreal.EFEquipmentAuthoringLibrary.find_inventory_widget
            label = str(find(windows[0], 'BreakdownLabel').get_text())
            check('Total:' in label and ('Buy this' if phase == 'trade_buy_press' else 'Sell this') in label, 'Confirmation shows aggregate price and operation')
            check('Satchel' in label and label.replace('×', 'x').count('x 1:') >= 2, 'Confirmation breaks down bag and contained item')
            S['trade_button'] = find(windows[0], 'CancelButton' if phase == 'trade_cancel_press' else 'ConfirmButton')
            check(unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['trade_button'], 'Down'), 'Trade button accepts pointer press')
            advance(phase.replace('_press', '_release'), 0.1)
        elif phase in ('trade_cancel_release', 'trade_sell_release', 'trade_buy_release'):
            unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(S['trade_button'], 'Up')
            advance(phase.replace('_release', '_verify'), 0.5)
        elif phase == 'trade_cancel_verify':
            check(not S['trade_window'].is_activated(), 'Cancel closes the trade confirmation')
            check(S['native'].get_current_currency_amount() == S['trade_currency'], 'Cancelled sale leaves player currency unchanged')
            check(any(i.get_editor_property('item_guid').to_string() == S['trade_bag'].to_string() for i in S['native'].get_inventory()), 'Cancelled sale retains original full bag')
            unreal.EFEquipmentFunctionLibrary.sell_items_to_vendor(S['vendor'], S['items']['Satchel'], S['pawn'], 1)
            advance('trade_back', 0.5)
        elif phase == 'trade_back':
            windows = [w for w in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'], unreal.EFEquipmentSaleWidget, False) if w.is_activated()]
            check(len(windows) == 1, 'Keyboard cancel fixture has one active confirmation')
            S['trade_window'] = windows[0]
            target = unreal.EFEquipmentAuthoringLibrary.find_inventory_widget(windows[0], 'CancelButton')
            check(unreal.EFEquipmentAuthoringLibrary.send_inventory_back_key(target), 'Native Slate Backspace event is handled by the dialog')
            advance('trade_back_verify', 0.5)
        elif phase == 'trade_back_verify':
            check(not S['trade_window'].is_activated(), 'Backspace cancels the confirmation')
            check(S['native'].get_current_currency_amount() == S['trade_currency'], 'Keyboard cancellation preserves currency')
            unreal.EFEquipmentFunctionLibrary.sell_items_to_vendor(S['vendor'], S['items']['Satchel'], S['pawn'], 1)
            advance('trade_sell_press', 0.5)
        elif phase == 'trade_sell_verify':
            check(not S['trade_window'].is_activated(), 'Confirmed sale closes its native navigation window')
            check(not any(i.get_editor_property('item_guid').to_string() == S['trade_bag'].to_string() for i in S['native'].get_inventory()), 'Confirmed sale removes bag from player')
            stock = S['vendor'].get_inventory()
            check(len(stock) == 1 and stock[0].get_editor_property('item_guid').to_string() == S['trade_bag'].to_string(), 'Vendor receives original bag without duplicate loose contents')
            unreal.EFEquipmentFunctionLibrary.buy_items(S['vendor'], stock[0], S['pawn'])
            advance('trade_buy_press', 0.5)
        elif phase == 'trade_buy_verify':
            check(not S['trade_window'].is_activated(), 'Confirmed purchase closes its native navigation window')
            contents = S['coordinator'].get_container_items(S['trade_bag'])
            check(len(contents) == 1 and contents[0].get_editor_property('item_guid').to_string() == S['trade_child'].to_string(), 'Sell and buy UI preserves original contained GUID')
            check(len(S['vendor'].get_inventory()) == 0, 'Purchase removes the whole set from vendor stock')
            check(S['coordinator'].move_from_container(S['trade_bag'], S['trade_child'], 1).get_editor_property('success'), 'Bought bag contents remain extractable')
            check(S['coordinator'].request_equip(S['trade_bag'], unreal.GameplayTag()).get_editor_property('success'), 'Bought bag remains equipable')
            R['status'] = 'AWAITING_VISUAL_REVIEW'
            write()
            advance('review')
        elif phase == 'review':
            if (OUT / 'visual_done.txt').exists():
                review = (OUT / 'visual_done.txt').read_text().strip()
                error = R.get('input_failure') or (review if review.startswith('FAIL:') else None)
                if error:
                    finish(error)
                else:
                    check(S['coordinator'].move_to_container(S['trade_child'], S['trade_bag'], 1).get_editor_property('success'), 'Death fixture contains a unique item')
                    S['menu'].deactivate_widget()
                    begin_death()
        elif phase == 'death_verify':
            R['death_cycles'][-1]['delayed'] = death_snapshot()
            pickups = [a for a in unreal.GameplayStatics.get_all_actors_of_class(S['world'], unreal.EFEquipmentWorldItem)
                       if any(i.get_editor_property('item_guid').to_string() == S['trade_bag'].to_string() for i in a.get_items())]
            check(len(pickups) == 1 and len(pickups[0].get_items()) == 1, 'Real death emits one original full-bag pickup')
            check(not any(i.get_editor_property('item_guid').to_string() == S['trade_bag'].to_string() for i in S['native'].get_inventory()), 'Real death removes dropped bag from player')
            all_loot = [i for a in unreal.GameplayStatics.get_all_actors_of_class(S['world'], unreal.EFEquipmentWorldItem) for i in a.get_items()]
            check(not any(i.get_editor_property('item_guid').to_string() == S['trade_child'].to_string() for i in all_loot), 'Real death creates no duplicate loose child loot')
            pickups[0].on_interacted_by_pawn(S['pawn'], '')
            contents = S['coordinator'].get_container_items(S['trade_bag'])
            check(len(contents) == 1 and contents[0].get_editor_property('item_guid').to_string() == S['trade_child'].to_string(), 'Real death and native pickup preserve contained GUID')
            if len(R['death_cycles']) < DEATH_REPEATS:
                # Reuse the native revive API only in this opt-in repetition fixture.
                # Each cycle keeps the same real player, bag and contained GUID.
                S['pawn'].get_damage_handler_component().revive()
                check(S['coordinator'].request_equip(S['trade_bag'], unreal.GameplayTag()).get_editor_property('success'), 'Recovered bag reequips between native death cycles')
                advance('death_repeat', 0.3)
            else:
                finish()
        elif phase == 'death_repeat':
            begin_death()
    except Exception:
        if S['phase'].startswith(('bag_', 'trade_')) or S['phase'] in ('wheel_down', 'wheel_up', 'drag_start', 'drag_move', 'drag_drop', 'drag_verify'):
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
