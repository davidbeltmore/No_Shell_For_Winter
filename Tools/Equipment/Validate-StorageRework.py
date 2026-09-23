"""Fresh PIE storage-context regression fixture; user saves guarded by launcher."""
import json, os, pathlib, time, traceback
import unreal
OUT = pathlib.Path(unreal.Paths.project_dir()) / 'Saved/EquipmentQA' / os.environ['EF_EQUIPMENT_QA_RUN']
S = {'phase': 'load', 'due': 0, 'start': time.monotonic(), 'busy': False}
R = {'status': 'PENDING', 'checks': []}
LE = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ED = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
def write(): (OUT / 'result.json').write_text(json.dumps(R, indent=2))
def check(ok, name):
    R['checks'].append({'name': name, 'pass': bool(ok)}); write()
    assert ok, name
def step(phase, delay=0):
    S.update(phase=phase, due=time.monotonic()+delay); R['phase']=phase; write()
def guid(item): return item.get_editor_property('item_guid')
def contains(inv, identity): return any(guid(i).to_string()==identity.to_string() for i in inv.get_inventory())
def find(page, name): return unreal.EFEquipmentAuthoringLibrary.find_inventory_widget(page, name)
def finish(error=None):
    if not unreal.EFEquipmentAuthoringLibrary.end_save_fixture(): error=(error or '')+' Save fixture cleanup failed.'
    R['status']='FAIL' if error else 'PASS'
    if error: R['error']=error
    write(); unreal.unregister_slate_post_tick_callback(S['callback'])
    unreal.EditorLevelLibrary.editor_end_play(); unreal.SystemLibrary.quit_editor()
def tick(dt):
    if S['busy']: return
    S['busy']=True
    try:
        if time.monotonic()-S['start']>180: raise RuntimeError('Storage QA timeout: '+S['phase'])
        if time.monotonic()<S['due']: return
        p=S['phase']
        if p=='load':
            if ED.get_editor_world().get_name()!='HUB': LE.load_level('/Game/_Game/Hub/HUB')
            step('play',1)
        elif p=='play': LE.editor_request_begin_play(); step('setup',5)
        elif p=='setup':
            w=ED.get_game_world(); pawn=unreal.GameplayStatics.get_player_pawn(w,0) if w else None
            if not pawn: return
            # Editor asset loading can consume wall-clock seconds within one
            # game frame. Allow native inventory and coordinator timers to run.
            game_time=unreal.GameplayStatics.get_time_seconds(w)
            if 'pawn_ready_time' not in S: S['pawn_ready_time']=game_time
            if game_time-S['pawn_ready_time']<1.0: return
            c=unreal.EFEquipmentFunctionLibrary.get_equipment_coordinator(pawn); inv=c.get_native_equipment()
            starter=unreal.load_class(None,'/Game/_Game/Items/Equipment/BP_StarterBag.BP_StarterBag_C')
            R['startup_inventory']=[{'class':i.get_editor_property('item_class').get_path_name(),'count':i.get_editor_property('count')} for i in inv.get_inventory()]
            check(starter is not None and sum(i.get_editor_property('count') for i in inv.get_all_items_of_class_in_inventory(starter))==1,
                  'Fresh character automatically receives exactly one Travel Bag')
            c.refresh_state();c.refresh_state()
            check(sum(i.get_editor_property('count') for i in inv.get_all_items_of_class_in_inventory(starter))==1,
                  'Repeated equipment refresh does not duplicate the starter bag')
            S.update(world=w,pawn=pawn,c=c,inv=inv)
            gi=unreal.GameplayStatics.get_game_instance(w)
            S['ui']=next(o for o in unreal.ObjectIterator(unreal.ANSUIPlayerSubsystem) if o.get_outer()==gi)
            for key,path in {'bag':'/Game/_Game/Items/Equipment/BP_Satchel2','item':'/Game/_Game/Clothes/UnderWear01/Panty'}.items():
                cls=unreal.load_class(None,path+'.'+path.split('/')[-1]+'_C')
                inv.add_item_to_inventory_by_class(cls,1,False)
                S[key]=inv.get_all_items_of_class_in_inventory(cls)[-1]
            actorcls=unreal.load_class(None,'/AscentCombatFramework/Integrations/Actors/ACF_Deposit_BP.ACF_Deposit_BP_C')
            S['deposit']=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,actorcls) if not a.get_component_by_class(unreal.ProjectLockpickableComponent))
            S['vendor']=S['deposit'].get_component_by_class(unreal.ACFVendorComponent)
            interaction=pawn.get_component_by_class(unreal.ACFInteractionComponent)
            interaction.set_current_best_interactable(S['deposit'])
            interaction.interact('')
            S['currency']=inv.get_current_currency_amount()
            step('deposit',2)
        elif p=='deposit':
            pages=unreal.WidgetLibrary.get_all_widgets_of_class(S['world'],unreal.EFEquipmentStoragePage,False)
            S['page']=next(page for page in pages if page.is_activated())
            panel=find(S['page'],'ACF_Sell_WB'); check(panel is not None,'Native deposit panel exists')
            for name in ['ACF_Buy_WB','ACF_Sell_WB']:
                check(find(find(S['page'],name),'Title').get_visibility()==unreal.SlateVisibility.COLLAPSED,
                      'Deposit hides repeated inner title: '+name)
            unreal.EFEquipmentFunctionLibrary.sell_items_to_vendor(S['vendor'],S['item'],S['pawn'],1,panel)
            check(contains(S['vendor'],guid(S['item'])) and not contains(S['inv'],guid(S['item'])),'Deposit transfers original GUID')
            check(S['inv'].get_current_currency_amount()==S['currency'],'Deposit does not change currency')
            item=next(i for i in S['vendor'].get_inventory() if guid(i).to_string()==guid(S['item']).to_string())
            unreal.EFEquipmentFunctionLibrary.buy_items(S['vendor'],item,S['pawn'],find(S['page'],'ACF_Buy_WB'))
            check(contains(S['inv'],guid(item)) and not contains(S['vendor'],guid(item)),'Withdrawal returns original GUID')
            check(S['inv'].get_current_currency_amount()==S['currency'],'Withdrawal does not change currency')
            S['ui'].remove_in_game_widget(S['page'],True,True)
            cls=unreal.load_class(None,'/Game/_Game/Widgets/Equipment/Global/WBP_EquipmentBag.WBP_EquipmentBag_C')
            S['page']=S['ui'].spawn_in_game_widget(cls,False,True,unreal.GameplayTag())
            step('bag',2)
        elif p=='bag':
            bag=S['page'].get_bag_inventory(); check(bag is not None,'Bag page resolves actual ACF bag inventory')
            unreal.EFEquipmentFunctionLibrary.sell_items_to_vendor(bag,S['item'],S['pawn'],1,find(S['page'],'ACF_Sell_WB'))
            check(contains(bag,guid(S['item'])),'Native bag deposit preserves GUID')
            check(S['inv'].get_current_currency_amount()==S['currency'],'Bag deposit does not change currency')
            merchant=unreal.EFEquipmentAuthoringLibrary.create_vendor_fixture(S['world'])
            unreal.EFEquipmentFunctionLibrary.sell_items_to_vendor(merchant,S['bag'],S['pawn'],1,None)
            check(contains(S['inv'],guid(S['bag'])) and contains(bag,guid(S['item'])),'Full bag sale rejected without losing objects')
            check(S['inv'].get_current_currency_amount()==S['currency'],'Rejected bag sale does not change currency')
            check('Empty this bag' in str(S['c'].get_last_message()),'Full bag rejection uses explicit English message')
            # Capture Slate through MCP at the visual gate. HighResShot omits UMG
            # and can leave misleading resized viewport imagery under the UI.
            step('visual',1)
        elif p=='visual':
            if (OUT/'visual_done.txt').exists(): step('bag_take')
        elif p=='bag_take':
            bag=S['page'].get_bag_inventory(); item=bag.get_inventory()[0]
            unreal.EFEquipmentFunctionLibrary.buy_items(bag,item,S['pawn'],find(S['page'],'ACF_Buy_WB'))
            check(contains(S['inv'],guid(item)) and not bag.get_inventory(),'Native bag withdrawal preserves GUID')
            S['ui'].remove_in_game_widget(S['page'],True,True)
            S['interaction']=S['pawn'].get_component_by_class(unreal.ACFInteractionComponent)
            cls=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentLockedChest.BP_EquipmentLockedChest_C')
            S['chest']=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(S['world'],cls) if not a.get_component_by_class(unreal.ProjectChestInventoryComponent).get_inventory())
            S['chest_name']=S['chest'].get_name()
            S['stock']=S['chest'].get_component_by_class(unreal.ProjectChestInventoryComponent)
            S['stock'].set_editor_property('procedural_mode',True,unreal.PropertyAccessChangeNotifyMode.NEVER)
            S['stock'].set_editor_property('profile',unreal.load_asset('/Game/_Game/Data/Equipment/DA_Chest_Default'),unreal.PropertyAccessChangeNotifyMode.NEVER)
            # Editor property changes rerun Blueprint construction, replacing SCS
            # components. Resolve the current lock after configuring the fixture.
            S['lock']=S['chest'].get_component_by_class(unreal.ProjectLockpickableComponent)
            S['stock']=S['chest'].get_component_by_class(unreal.ProjectChestInventoryComponent)
            S['interactable']=S['chest'].get_component_by_class(unreal.ACFInteractableComponent)
            check(str(S['interactable'].get_interaction_action_tag().get_editor_property('tag_name'))=='None','Locked chest suppresses native opening action before interaction')
            S['pick_class']=unreal.load_class(None,'/Game/_Game/Lockpicking/LockPick.LockPick_C')
            for item in S['inv'].get_all_items_of_class_in_inventory(S['pick_class']): S['inv'].remove_item(item,item.get_editor_property('count'))
            S['lock_ui']=next(o for o in unreal.ObjectIterator(unreal.ProjectLockpickingSubsystem) if o.get_outer()==S['world'])
            S['interaction'].set_current_best_interactable(S['chest'])
            R['lock_before']={'actor':S['chest'].get_path_name(),'locked':S['lock'].is_locked(),'session':S['lock'].is_session_active(),'pick_count':S['lock'].get_required_lockpick_count(S['pawn']),'required':S['lock'].get_editor_property('require_lockpick_item'),'starts_locked':S['lock'].get_editor_property('starts_locked')}
            S['interaction'].interact(S['lock'].build_begin_interaction_type())
            step('no_pick',0.3)
        elif p=='no_pick':
            R['lock_after']={'locked':S['lock'].is_locked(),'session':S['lock'].is_session_active(),'pick_count':S['lock'].get_required_lockpick_count(S['pawn'])}
            check(S['lock'].is_locked() and not S['lock'].is_session_active(),'Missing pick cannot begin lockpicking')
            check(not any(p.is_activated() for p in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'],unreal.EFEquipmentStoragePage,False)),'Locked local interaction cannot open native storage without a pick')
            check(not S['stock'].has_generated_contents(),'Locked chest has not generated loot early')
            S['inv'].add_item_to_inventory_by_class(S['pick_class'],2,False)
            S['pick_count']=S['lock'].get_required_lockpick_count(S['pawn'])
            S['interaction'].set_current_best_interactable(S['chest']); S['interaction'].interact('')
            check(S['lock_ui'].is_lockpick_prompt_open(),'Existing lockpick prompt is reused')
            S['lock_ui'].close_lockpick_prompt();check(S['lock'].is_locked(),'Prompt cancellation preserves lock')
            if OUT.name.startswith('LockpickInput'):
                S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact('')
                step('prompt_click');return
            S['interaction'].interact(S['lock'].build_begin_interaction_type())
            check(S['lock'].is_session_active(),'Existing lockpick minigame starts with a pick')
            stale=S['lock'].build_confirm_interaction_type()
            S['lock_ui'].close_lockpicking()
            check(S['lock'].is_locked() and not S['lock'].is_session_active(),'Minigame cancellation clears session and preserves lock')
            S['interaction'].interact(stale);check(S['lock'].is_locked(),'Cancelled session cannot unlock through stale confirmation')
            S['interaction'].interact(S['lock'].build_begin_interaction_type())
            S['interaction'].interact(S['lock'].build_confirm_interaction_type())
            check(S['lock'].is_locked() and S['lock'].is_in_failure_cooldown(),'Failed timing retains lock and native cooldown')
            check(S['lock'].get_required_lockpick_count(S['pawn'])==S['pick_count']-1,'Failed attempt consumes one lockpick')
            step('retry_lock',2.1)
        elif p=='retry_lock':
            if S['lock'].is_in_failure_cooldown(): return
            S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact(S['lock'].build_begin_interaction_type())
            R['retry']={'session':S['lock'].is_session_active(),'ui':S['lock_ui'].is_lockpicking_open(),'paused':unreal.GameplayStatics.is_game_paused(S['world'])}
            check(S['lock'].is_session_active() and S['lock_ui'].is_lockpicking_open(),'Retry starts a visible active minigame')
            step('lock_timing')
        elif p=='prompt_click':
            prompts=[p for p in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'],unreal.ProjectLockpickPromptWidget,False) if p.is_in_viewport()]
            if prompts:
                button=find(prompts[0],'LockpickButton')
                if button:
                    # Slate geometry library is not exposed by this engine's
                    # Python build. Locate the physical button in the capture.
                    R['click_target']={'widget':'LockpickButton','enabled':button.get_is_enabled()};write()
                    if 'input_probe' not in S:
                        S['input_probe']=True
                        R['prompt_state']={'visibility':str(prompts[0].get_visibility()),'enabled':prompts[0].get_is_enabled(),
                            'focus':prompts[0].has_keyboard_focus(),
                            'bound_button':str(prompts[0].get_editor_property('lockpick_button'))}
                        R['pointer_down']=unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(button,'Down',0,unreal.Vector2D())
                        # Keep the synthetic hover alive until release; a real
                        # desktop cursor elsewhere can clear it on another tick.
                        R['pointer_up']=unreal.EFEquipmentAuthoringLibrary.send_inventory_pointer(button,'Up',0,unreal.Vector2D())
                        S['click_button']=button;write()
            if S['lock_ui'].is_lockpicking_open():
                check(S['lock'].is_session_active(),'Slate button pointer events start the session and minigame')
                step('minigame_visual')
        elif p=='minigame_visual':
            if (OUT/'minigame_visual_done.txt').exists():
                check(unreal.EFEquipmentAuthoringLibrary.send_focused_inventory_key(S['click_button'],'BackSpace'),'Minigame receives BackSpace through existing Slate focus')
                check(S['lock'].is_locked() and not S['lock'].is_session_active(),'Keyboard cancellation retains the lock and clears session')
                check(S['lock'].get_required_lockpick_count(S['pawn'])==S['pick_count'],'Opening and cancellation consume no lockpicks')
                S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact('')
                step('keyboard_prompt',0.3)
        elif p=='keyboard_prompt':
            check(unreal.EFEquipmentAuthoringLibrary.send_focused_inventory_key(S['click_button'],'E'),'Prompt receives E through existing Slate focus')
            check(S['lock_ui'].is_lockpicking_open() and S['lock'].is_session_active(),'E opens a visible active minigame')
            token=S['lock'].build_confirm_interaction_type()
            S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact(S['lock'].build_begin_interaction_type())
            S['interaction'].interact('')
            check(S['lock'].build_confirm_interaction_type()==token and not S['lock_ui'].is_lockpick_prompt_open(),'Repeated begin and default interaction preserve the active session')
            S['lock_ui'].close_lockpicking()
            S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact('')
            step('enter_prompt',0.3)
        elif p=='enter_prompt':
            check(unreal.EFEquipmentAuthoringLibrary.send_focused_inventory_key(S['click_button'],'Enter'),'Prompt receives Enter through existing Slate focus')
            check(S['lock_ui'].is_lockpicking_open() and S['lock'].is_session_active(),'Enter opens a visible active minigame')
            step('failure_timing')
        elif p=='failure_timing':
            if S['lock'].get_current_pulse_value()<0.15:
                stale=S['lock'].build_confirm_interaction_type()
                unreal.EFEquipmentAuthoringLibrary.send_focused_inventory_key(S['click_button'],'SpaceBar')
                check(S['lock'].is_locked() and not S['lock'].is_session_active(),'Failed Spacebar attempt keeps chest locked')
                check(S['lock'].get_required_lockpick_count(S['pawn'])==S['pick_count']-1,'Failed attempt consumes exactly one lockpick')
                S['interaction'].interact(stale)
                check(S['lock'].get_required_lockpick_count(S['pawn'])==S['pick_count']-1,'Duplicate confirmation cannot consume another lockpick')
                step('keyboard_retry',2.1)
        elif p=='keyboard_retry':
            if S['lock'].is_in_failure_cooldown(): return
            S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact('')
            step('keyboard_retry_begin',0.3)
        elif p=='keyboard_retry_begin':
            unreal.EFEquipmentAuthoringLibrary.send_focused_inventory_key(S['click_button'],'E')
            check(S['lock_ui'].is_lockpicking_open() and S['lock'].is_session_active(),'Retry starts one active minigame')
            step('keyboard_timing')
        elif p=='keyboard_timing':
            if abs(S['lock'].get_current_pulse_value()-0.5)<S['lock'].get_active_target_half_range()*0.6:
                check(unreal.EFEquipmentAuthoringLibrary.send_focused_inventory_key(S['click_button'],'SpaceBar'),'Minigame receives Spacebar through existing Slate focus')
                step('unlocked',2)
        elif p=='lock_timing':
            if abs(S['lock'].get_current_pulse_value()-0.5)<S['lock'].get_active_target_half_range()*0.6:
                S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact(S['lock'].build_confirm_interaction_type())
                step('unlocked',0.5)
        elif p=='unlocked':
            check(not S['lock'].is_locked(),'Successful native timing unlocks chest')
            check(str(S['interactable'].get_interaction_action_tag().get_editor_property('tag_name'))!='None','Successful lockpick restores the native opening action')
            check(S['lock'].get_required_lockpick_count(S['pawn'])==S['pick_count']-2,'Success consumes the final lockpick exactly once')
            pages=[p for p in unreal.WidgetLibrary.get_all_widgets_of_class(S['world'],unreal.EFEquipmentStoragePage,False) if p.is_activated()]
            check(len(pages)==1,'Unlock continues native deposit interaction exactly once')
            S['page']=pages[0]
            check(S['stock'].has_generated_contents(),'Procedural chest generates on first successful opening')
            count=sum(i.get_editor_property('count') for i in S['stock'].get_inventory())
            check(1<=count<=3,'Native catalog profile quantity is within authored bounds')
            S['saved_stock']={guid(i).to_string():i.get_editor_property('count') for i in S['stock'].get_inventory()}
            S['ui'].remove_in_game_widget(S['page'],True,True)
            S['c'].set_base_carry_capacity(100000)
            check(bool(unreal.EFEquipmentAuthoringLibrary.begin_save_fixture(S['world'])),'Chest save uses isolated QA metadata')
            check(unreal.EFEquipmentAuthoringLibrary.save_world_fixture(S['world']),'Native ALS world save accepted')
            step('saved',1)
        elif p=='saved':
            if unreal.EFEquipmentAuthoringLibrary.is_save_fixture_busy(S['world']): return
            for item in S['stock'].get_inventory():
                check(S['c'].take_from_inventory(S['stock'],guid(item),item.get_editor_property('count'),False).get_editor_property('success'),'Generated chest items can be withdrawn')
            S['stock'].ensure_contents()
            check(S['stock'].has_generated_contents(),'Empty procedural chest remains initialized')
            check(not S['stock'].get_inventory(),'Empty chest does not regenerate loot')
            S['lock'].set_locked(True)
            check(unreal.EFEquipmentAuthoringLibrary.load_world_fixture(S['world']),'Native ALS world load accepted')
            step('loaded',1)
        elif p=='loaded':
            # ALS world load replaces the world and its actors. Persistent test
            # identity is the saved actor name, never a stale UObject wrapper.
            world=ED.get_game_world()
            if not world or unreal.EFEquipmentAuthoringLibrary.is_save_fixture_busy(world): return
            pawn=unreal.GameplayStatics.get_player_pawn(world,0)
            if not pawn: return
            cls=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentLockedChest.BP_EquipmentLockedChest_C')
            chest=next((a for a in unreal.GameplayStatics.get_all_actors_of_class(world,cls) if a.get_name()==S['chest_name']),None)
            if not chest: return
            S.update(world=world,pawn=pawn,chest=chest,
                lock=chest.get_component_by_class(unreal.ProjectLockpickableComponent),
                stock=chest.get_component_by_class(unreal.ProjectChestInventoryComponent),
                interaction=pawn.get_component_by_class(unreal.ACFInteractionComponent))
            check(not S['lock'].is_locked(),'Native save/load preserves unlocked state')
            check(S['stock'].has_generated_contents(),'Native save/load preserves generated flag')
            check({guid(i).to_string():i.get_editor_property('count') for i in S['stock'].get_inventory()}==S['saved_stock'],'Native save/load preserves remaining item GUIDs and quantities')
            S['lock'].set_locked(True)
            S['interaction'].set_current_best_interactable(S['chest']);S['interaction'].interact(S['lock'].build_begin_interaction_type())
            check(S['lock'].is_locked() and not S['lock'].is_session_active(),'Exhausted lockpicks prevent another attempt')
            step('finish',1)
        elif p=='finish': finish()
    except Exception: finish(traceback.format_exc())
    finally: S['busy']=False
write()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
S['callback']=unreal.register_slate_post_tick_callback(tick)
