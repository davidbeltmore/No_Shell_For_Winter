"""Isolated real-player practice QA. Must run with Run-PracticeProgression58.ps1 SaveGuard."""
import json, time, traceback, runpy, threading, sys, urllib.request, ctypes, os
from ctypes import wintypes
from pathlib import Path
import unreal

RESPAWN='-PracticeRespawn' in unreal.SystemLibrary.get_command_line()
ACTIVITIES='-PracticeActivities' in unreal.SystemLibrary.get_command_line()
RELOAD=RESPAWN or '-PracticeReload' in unreal.SystemLibrary.get_command_line()
OUT=(Path(unreal.Paths.project_saved_dir())/'Analysis/PracticeProgression'/('Activities' if ACTIVITIES else 'Respawn' if RESPAWN else 'Reload' if RELOAD else 'Visual')).resolve()
expected=json.loads((OUT.parent/'Visual/receipt.json').read_text(encoding='utf8')) if RELOAD else {}
OUT.mkdir(parents=True,exist_ok=True)
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
settings=unreal.get_default_object(unreal.load_class(None,'/Script/EFProjectSystemsGameplay.ProjectPracticeSettings'))
settings.set_editor_property('bEnabled',True) # QA process only, never saves project settings.
state={'phase':'load','started':time.monotonic(),'since':time.monotonic(),'checks':[]}
callbacks=[]
pressed_keys=set()
PROJECT_DIR=Path(unreal.Paths.project_dir()).resolve()
PLAY_MODE='PlayMode_InViewPort' if '-PracticeViewport' in unreal.SystemLibrary.get_command_line() else 'PlayMode_InEditorFloating'

def start_floating_pie():
    # HTTP only on this worker; all Unreal operations run through native MCP on
    # the editor thread. The protected editor preference is never modified.
    try:
        sys.path.insert(0,str(PROJECT_DIR/'Tools/Equipment'))
        from UnrealMcp import Client
        client=Client()
        schema=client.describe('EditorToolset.EditorAppToolset')
        before=client.call('EditorToolset.EditorAppToolset','IsPIERunning',{})
        result=client.call('EditorToolset.EditorAppToolset','StartPIE',{'options':{'bSimulate':False,'playMode':PLAY_MODE,'warmupSeconds':0}})
        close=urllib.request.Request('http://127.0.0.1:8000/mcp',method='DELETE',headers={'Mcp-Session-Id':client.session})
        try:
            with urllib.request.urlopen(close,timeout=10) as response:closed=response.status
        except Exception as error:closed=str(error)
        (OUT/'mcp_pie.json').write_text(json.dumps({'schema':schema,'before':before,'result':result,'session_close':closed},indent=2),encoding='utf8')
    except Exception:state['mcp_error']=traceback.format_exc()

def cls(name,module='EFProjectSystemsGameplay'):
    return unreal.load_class(None,'/Script/'+module+'.'+name)
def sub(world,name,module='EFProjectSystemsGameplay'):
    return next((x for x in unreal.ObjectIterator(cls(name,module)) if x.get_path_name().startswith(world.get_path_name())),None)
def phase(value):
    state.update(phase=value,since=time.monotonic())
    (OUT/'state.json').write_text(json.dumps(state,indent=2,default=str),encoding='utf8')
def check(name,condition):
    state['checks'].append({'name':name,'passed':bool(condition)})
    if not condition: raise RuntimeError(name)
def shot(world,name):
    unreal.SystemLibrary.execute_console_command(world,'Shot SHOWUI filename="'+str(OUT/(name+'.png')).replace('\\','/')+'"')

def key(vk,down):
    """Controller input by default; optional real keyboard bounded to this PIE window."""
    if '-PracticeOSInput' not in unreal.SystemLibrary.get_command_line():
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if world:
            name={0x57:'W',0x4f:'O',0x59:'Y',0x1b:'Escape'}[vk]
            unreal.SystemLibrary.execute_console_command(world,'Project.Practice.Key '+name+(' down' if down else ' up'))
        state['input_method']='native PlayerController InputKey (not OS keyboard)'
        if down:pressed_keys.add(vk)
        else:pressed_keys.discard(vk)
        return True
    native=ctypes.windll.user32
    native.GetForegroundWindow.restype=wintypes.HWND
    native.SetForegroundWindow.argtypes=[wintypes.HWND]
    native.BringWindowToTop.argtypes=[wintypes.HWND]
    native.SwitchToThisWindow.argtypes=[wintypes.HWND,wintypes.BOOL]
    native.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    native.GetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
    native.IsWindowVisible.argtypes=[wintypes.HWND]
    windows=[];own_windows=[]
    @ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    def visit(handle,param):
        pid=wintypes.DWORD();native.GetWindowThreadProcessId(handle,ctypes.byref(pid))
        title=ctypes.create_unicode_buffer(512);native.GetWindowTextW(handle,title,512)
        if pid.value==os.getpid():
            own_windows.append({'title':title.value,'visible':bool(native.IsWindowVisible(handle))})
            if native.IsWindowVisible(handle) and any(word in title.value.casefold() for word in ['preview','vista previa','previsualizaci','play in editor']):windows.append(handle)
        return True
    native.EnumWindows(visit,0)
    state['input_windows']=own_windows
    if down:
        if not windows:raise RuntimeError('No floating PIE window for bounded input QA')
        if native.GetForegroundWindow()!=windows[0]:
            native.SwitchToThisWindow(windows[0],True)
            native.keybd_event(0x12,0,0,0);native.keybd_event(0x12,0,2,0)
            native.BringWindowToTop(windows[0])
        native.SetForegroundWindow(windows[0])
        if native.GetForegroundWindow()!=windows[0]:return False # Let Windows process activation before retrying.
        pressed_keys.add(vk)
    else:pressed_keys.discard(vk)
    native.keybd_event(vk,0,0 if down else 2,0)
    return True
def finish(error=''):
    for vk in list(pressed_keys):key(vk,False)
    state['error']=error
    (OUT/'receipt.json').write_text(json.dumps(state,indent=2,default=str),encoding='utf8')
    phase('quit')
    if editor.is_in_play_in_editor(): unreal.EditorLevelLibrary.editor_end_play()
def saved(ok): state['saved']=bool(ok)
def save_system(world):
    gi=unreal.GameplayStatics.get_game_instance(world)
    return next(x for x in unreal.ObjectIterator(cls('ALSLoadAndSaveSubsystem','AscentSaveSystem')) if x.get_path_name().startswith(gi.get_path_name()))
def skill_nodes(skills):
    for tree in skills.get_skill_trees():
        for node in tree.get_all_nodes():
            if hasattr(node,'get_skill_to_grant'):yield tree,node
def tick(delta):
    if state.get('busy'):return
    state['busy']=True
    try:
        age=time.monotonic()-state['since']
        if state['phase']=='quit':
            if age>3 and not state.get('collected'):
                state['collected']=True
                if ACTIVITIES:
                    actor_editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
                    for actor in actor_editor.get_all_level_actors():
                        if actor.actor_has_tag('QA_PracticeLock'):actor_editor.destroy_actor(actor)
                editor_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
                unreal.SystemLibrary.execute_console_command(editor_world,'Editor.AsyncAssetCompilationFinishAll')
                unreal.SystemLibrary.collect_garbage()
            if age>10:
                unreal.unregister_slate_post_tick_callback(handle)
                # -ExecutePythonScript owns shutdown: its next editor tick first
                # destroys the async Slate notification, then defers QUIT_EDITOR.
                # Calling quit_editor here skips that cleanup (IsExitRequested).
                unreal.EditorPythonScripting.set_keep_python_script_alive(False)
            return
        if time.monotonic()-state['started']>360: raise RuntimeError('Timeout: '+state['phase'])
        if state.get('mcp_error'):raise RuntimeError(state['mcp_error'])
        if state['phase']=='load':
            phase('preparing') # Asset compilation may pump Slate; never re-enter this work.
            runpy.run_path(str(Path(unreal.Paths.project_dir())/'Tools/Migration/Create-PracticeAttributeTable58.py'))
            gas_settings=unreal.get_default_object(unreal.load_class(None,'/Script/AscentGASRuntime.ACFGASDeveloperSettings'))
            gas_settings.set_editor_property('SerializableAttributes',unreal.load_asset('/Game/_Game/Data/Progression/DT_ProjectSerializableAttributes'))
            play_settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
            play_settings.set_editor_property('NewWindowWidth',1280)
            play_settings.set_editor_property('NewWindowHeight',900)
            native_table=unreal.load_asset('/AscentCombatFramework/GASRuntime/ACF_SerializabeAttributes_DT')
            (OUT.parent/'native_attribute_rows.json').write_text(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(native_table),encoding='utf8')
            for path in ['/Game/FullSample/Player','/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar']:
                bp=unreal.load_asset(path)
                unreal.BlueprintEditorLibrary.compile_blueprint(bp)
                check('Blueprint compiles: '+path,'UP_TO_DATE' in str(bp.get_editor_property('status')))
            editor.load_level('/Game/_Game/Hub/HUB')
            if ACTIVITIES:
                # In-memory fixture must participate in PIE duplication; never save the map.
                fixture=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(cls('ProjectLockedInteractableActor'),unreal.Vector(0,0,5000))
                fixture.set_editor_property('tags',['QA_PracticeLock'])
            editor_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
            unreal.SystemLibrary.execute_console_command(editor_world,'Editor.AsyncAssetCompilationFinishAll')
            phase('ready')
            threading.Thread(target=start_floating_pie,daemon=True).start()
            return
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world: return
        if state['phase']=='new_menu' and age>5:
            save_system(world).set_new_game(True)
            unreal.GameplayStatics.open_level(world,'/Game/_Game/Hub/HUB')
            phase('new_character');return
        pawn=unreal.GameplayStatics.get_player_pawn(world,0)
        if not pawn:return
        progression=pawn.get_component_by_class(cls('ProjectPracticeProgressionComponent'))
        training=pawn.get_component_by_class(cls('ProjectPracticeComponent'))
        doctrine=pawn.get_component_by_class(cls('ProjectInnerDoctrineComponent'))
        stats=pawn.get_component_by_class(cls('ACFGASStatisticsComponent','AdvancedRPGSystem'))
        skills=pawn.get_component_by_class(cls('ACFSkillTreeComponent','AscentSkillTree'))
        needs=pawn.get_component_by_class(cls('ProjectSurvivalNeedsComponent'))
        clock=sub(world,'ProjectDayCycleSubsystem')
        emotes=sub(world,'ProjectEmoteSubsystem')
        if state['phase']=='ready' and age>12:
            check('project practice adapter on real player',training is not None and progression is not None)
            effects=list(stats.get_editor_property('StartingEffects'))
            state['starting_effects']=[effect.get_path_name() for effect in effects]
            check('validated curve adapters are active',all(any(name in path for path in state['starting_effects']) for name in ['GE_ProjectAttDependencies_GE','GE_ProjectStatInit_GE']))
            if ACTIVITIES:
                state['activities']=['PushUps','JumpRope','ReadBook','Meditate','Pray','RehearseSpeech','Balance','Constitution','Offensive','Defensive','Celerity','PracticeLock']
                state['activity_index']=0
                fixture=next(actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world,cls('ProjectLockedInteractableActor')) if actor.actor_has_tag('QA_PracticeLock'))
                fixture.set_actor_location(pawn.get_actor_location()+unreal.Vector(50,0,0),False,False)
                state['lock_fixture']=fixture
                fixture.get_component_by_class(cls('ProjectLockpickableComponent')).set_editor_property('Difficulty',100)
                state['durations']={}
                phase('activity_start');return
            if RELOAD:
                save_system(world).load_game_world('PracticeV2_QA',unreal.OnLoadFinished(),True)
                phase('loading');return
            check('one native ASC',len(pawn.get_components_by_class(cls('AbilitySystemComponent','GameplayAbilities')))==1)
            check('twelve training activities',len(training.get_available_training_definitions())==12)
            check('attributes cannot be purchased',not doctrine.spend_dxp_on_attribute(unreal.ProjectDoctrineAttribute.WILLPOWER))
            asc=pawn.get_component_by_class(cls('AbilitySystemComponent','GameplayAbilities'))
            before=[asc.get_gameplay_effect_count(effect,None,False) for effect in effects]
            stats.initialize_attribute_set()
            after=[asc.get_gameplay_effect_count(effect,None,False) for effect in effects]
            state['initial_effect_counts']={'before':before,'after_reinitialize':after}
            check('reinitialization does not duplicate starting effects',before==after)
            old=progression.get_learned_rank('Willpower')
            ratios={name:needs.get_need_normalized_value(name) for name in ['Hunger','Thirst','Sleep']}
            pain=needs.get_sensation_current_value('Pain')
            progression.practice('Willpower',2,100,True)
            check('need proportions survive learned rank change',all(abs(needs.get_need_normalized_value(name)-value)<.001 for name,value in ratios.items()))
            check('sensations retain absolute value',needs.get_sensation_current_value('Pain')==pain)
            check('Curse maximum remains 100',needs.get_sensation_max_value('Curse')==100)
            check('practice improves real GAS rank',progression.get_learned_rank('Willpower')>old)
            check('Doctrine reads GAS rank',doctrine.get_attribute_level(unreal.ProjectDoctrineAttribute.WILLPOWER)==int(progression.get_learned_rank('Willpower')))
            state['rank']=progression.get_learned_rank('Willpower')
            state['fraction']=progression.get_practice_progress('Willpower')
            state['points_before']=skills.get_skill_points()
            stats.add_exp(stats.get_totale_exp_to_next_level())
            check('native direct XP grants only one skill point',skills.get_skill_points()==state['points_before']+1 and stats.get_available_perks()==0)
            chosen=None
            for tree,node in skill_nodes(skills):
                result=skills.can_unlock_skill(tree.get_skill_tree_tag(),node.get_node_id())
                if result in (unreal.CanUnlockSkillResult.SKILL_UNLOCKED,unreal.CanUnlockSkillResult.SKILL_UPGRADED):chosen=(tree,node);break
            check('native skill tree has an available skill',chosen is not None)
            tree,node=chosen
            points=skills.get_skill_points()
            cost=node.get_skill_to_grant().get_editor_property('required_skill_point')
            skills.unlock_skill_from_tree(tree.get_skill_tree_tag(),node.get_node_id())
            check('native skill purchase spends once',skills.get_skill_points()==points-cost and skills.get_skill_current_level(node.get_node_id())==1)
            skills.respec()
            check('native respec refunds exactly',skills.get_skill_points()==points and skills.get_skill_current_level(node.get_node_id())==0)
            skills.unlock_skill_from_tree(tree.get_skill_tree_tag(),node.get_node_id())
            state['skill_id']=node.get_node_id().export_text()
            doctrine.handle_run_death()
            check('death preserves learned rank',progression.get_learned_rank('Willpower')==state['rank'])
            check('book metadata accepted',training.configure_book('QA_Book',48))
            sub(world,'ProjectSurvivalNeedsSubsystem').set_needs_hud_visible(True)
            check('training starts from real Y interaction route',emotes.request_start_interaction_by_id('Actions.Training.ReadBook'))
            phase('reading')
        elif state['phase']=='activity_start' and age>2:
            activity=state['activities'][state['activity_index']]
            for name in ['Hunger','Thirst','Sleep']:needs.set_need_current_value(name,90,True)
            check(activity+' starts through Y',emotes.request_start_interaction_by_id('Actions.Training.'+activity))
            phase('activity_running')
        elif state['phase']=='activity_running' and age>2:
            activity=state['activities'][state['activity_index']]
            check(activity+' remains active and progresses',training.is_training_active() and str(training.get_active_training_id())==activity and training.get_session_hours()>0)
            check(activity+' animation is active',pawn.get_component_by_class(cls('ProjectEmoteComponent')).is_emote_active())
            duration=training.get_session_duration();state['durations'][activity]=duration
            if activity in ['Meditate','Pray','RehearseSpeech']:check(activity+' is continuous',duration==0)
            if activity=='ReadBook':check('default book is 48 calendar hours',duration==48)
            if activity=='PushUps':check('initial physical cap is three hours',duration==3)
            training.cancel_training()
            state['activity_index']+=1
            phase('activity_start' if state['activity_index']<len(state['activities']) else 'lock_easy')
        elif state['phase']=='lock_easy' and age>2:
            state['lock_fixture'].get_component_by_class(cls('ProjectLockpickableComponent')).set_editor_property('Difficulty',1)
            check('easy lock starts',emotes.request_start_interaction_by_id('Actions.Training.PracticeLock'))
            phase('lock_compare')
        elif state['phase']=='lock_compare' and age>2:
            check('lock difficulty changes duration',0<training.get_session_duration()<state['durations']['PracticeLock'])
            state['lock_fixture'].set_actor_location(pawn.get_actor_location()+unreal.Vector(1000,0,0),False,False)
            phase('lock_left')
        elif state['phase']=='lock_left' and age>2:
            check('leaving the lock stops practice',not training.is_training_active())
            progression.practice('Strength',1,1.e9,False)
            check('strong character can start physical practice',emotes.request_start_interaction_by_id('Actions.Training.PushUps'))
            phase('physical_cap')
        elif state['phase']=='physical_cap' and age>2:
            check('learned strength raises physical cap to nine hours',training.is_training_active() and abs(training.get_session_duration()-9)<1.e-6)
            training.cancel_training()
            state['lock_fixture'].destroy_actor();del state['lock_fixture']
            finish()
        elif state['phase']=='reading' and age>3:
            check('reading is active',training.is_training_active())
            check('reading accrues time',training.get_session_hours()>0)
            check('reading has no timing minigame',not training.is_timing_minigame_session_active())
            widgets=[widget for widget in unreal.ObjectIterator(cls('ProjectPracticeWidget')) if widget.is_in_viewport()]
            check('one minimal passive practice HUD',len(widgets)==1 and widgets[0].is_in_viewport() and 'HIT_TEST_INVISIBLE' in str(widgets[0].get_visibility()))
            old_widgets=list(unreal.ObjectIterator(cls('ProjectTrainingLockpickWidget')))
            check('old central minigame panel is absent',not any(widget.is_in_viewport() for widget in old_widgets))
            controller=unreal.GameplayStatics.get_player_controller(world,0)
            check('training uses a free camera',controller.get_view_target()!=pawn)
            shot(world,'minimal_practice_before_input')
            state['camera_start']=controller.get_view_target().get_actor_location().to_tuple()
            state['pawn_start']=pawn.get_actor_location().to_tuple()
            phase('camera_focus')
        elif state['phase']=='camera_focus':
            if key(0x57,True):phase('camera_move')
            elif age>8:raise RuntimeError('PIE could not acquire keyboard focus')
        elif state['phase']=='camera_move' and age>1:
            key(0x57,False)
            controller=unreal.GameplayStatics.get_player_controller(world,0)
            check('W moves the free camera',(controller.get_view_target().get_actor_location()-unreal.Vector(*state['camera_start'])).length()>1)
            check('free camera leaves the trainee in place',(pawn.get_actor_location()-unreal.Vector(*state['pawn_start'])).length()<1)
            key(0x4f,True);key(0x4f,False);phase('camera_off')
        elif state['phase']=='camera_off' and age>1:
            check('O returns to the character camera',unreal.GameplayStatics.get_player_controller(world,0).get_view_target()==pawn)
            check('camera toggle preserves practice',training.is_training_active())
            key(0x4f,True);key(0x4f,False);phase('camera_on')
        elif state['phase']=='camera_on' and age>1:
            check('O restores the free camera',unreal.GameplayStatics.get_player_controller(world,0).get_view_target()!=pawn)
            check('x4 available during reading',clock.request_time_speed(4))
            state['read_start']=training.get_session_hours();phase('fast')
        elif state['phase']=='fast' and age>4:
            check('x4 reading advances',training.get_session_hours()>state['read_start'])
            shot(world,'reading_x4');phase('pause_start')
        elif state['phase']=='pause_start' and age>1:
            check('pause accepted',clock.toggle_player_pause())
            state['frozen']=training.get_session_hours();phase('paused')
        elif state['phase']=='paused' and age>2:
            check('pause freezes practice',training.get_session_hours()==state['frozen'])
            check('pause resumes',clock.toggle_player_pause())
            state['book_progress']=training.get_activity_progress()
            key(0x59,True);key(0x59,False);phase('cancelled')
        elif state['phase']=='cancelled' and age>1:
            check('cancelled training stopped',not training.is_training_active())
            check('cancel restores movement','WALKING' in str(pawn.get_movement_component().get_editor_property('movement_mode')).upper())
            check('resume book from Y',emotes.request_start_interaction_by_id('Actions.Training.ReadBook'))
            check('book retains partial progress',training.get_activity_progress()>=state['book_progress'])
            phase('resumed')
        elif state['phase']=='resumed' and age>1:
            needs.set_need_current_value('Thirst',1,True);phase('critical')
        elif state['phase']=='critical' and age>1:
            check('critical needs stop training',not training.is_training_active())
            needs.set_need_current_value('Thirst',90,True)
            state['rank']=progression.get_learned_rank('Willpower')
            state['fraction']=progression.get_practice_progress('Willpower')
            state['level']=stats.get_current_level();state['xp']=stats.get_current_exp();state['skills']=skills.get_skill_points()
            state['book_hours']=training.get_book_hours('QA_Book')
            unreal.SystemLibrary.execute_console_command(world,'Editor.AsyncAssetCompilationFinishAll')
            save=save_system(world)
            callback=unreal.OnSaveFinished();callback.bind_callable(saved);callbacks.append(callback)
            save.save_game_world('PracticeV2_QA',callback,True)
            phase('saving')
        elif state['phase']=='saving' and state.get('saved'):
            check('full ALS world saved',True)
            shot(world,'learned_hud');phase('finish')
        elif state['phase']=='finish' and age>2:finish()
        elif state['phase']=='loading' and age>4 and 'IDLE' in str(save_system(world).get_system_state()).upper():
            check('fresh process restores GAS rank',progression.get_learned_rank('Willpower')==expected['rank'])
            check('fresh process restores fractional learning',abs(progression.get_practice_progress('Willpower')-expected['fraction'])<1.e-8)
            check('fresh process restores native level and XP',stats.get_current_level()==expected['level'] and stats.get_current_exp()==expected['xp'])
            check('load does not award level points twice',skills.get_skill_points()==expected['skills'] and stats.get_available_perks()==0)
            restored=next((node for tree,node in skill_nodes(skills) if node.get_node_id().export_text()==expected['skill_id']),None)
            check('learned native skill is restored',restored is not None and skills.get_skill_current_level(restored.get_node_id())==1)
            check('book hours restored',abs(training.get_book_hours('QA_Book')-expected['book_hours'])<1.e-8)
            check('load does not resume unsupervised training',not training.is_training_active())
            if RESPAWN:
                defeat=pawn.get_component_by_class(cls('ProjectDefeatFlowComponent'))
                check('real defeat flow starts',defeat.debug_trigger_immediate_defeat('PracticeQA'))
                phase('defeat');return
            unreal.GameplayStatics.open_level(world,'/Game/_Game/Locations/StorySelection')
            phase('travel')
        elif state['phase']=='travel' and age>10 and unreal.GameplayStatics.get_current_level_name(world,True)=='StorySelection':
            state['travel_actual']={'rank':progression.get_learned_rank('Willpower'),'fraction':progression.get_practice_progress('Willpower'),'level':stats.get_current_level(),'xp':stats.get_current_exp(),'skills':skills.get_skill_points(),'book_hours':training.get_book_hours('QA_Book'),'paused':unreal.GameplayStatics.is_game_paused(world),'save_state':str(save_system(world).get_system_state())}
            check('travel preserves learned rank',progression.get_learned_rank('Willpower')==expected['rank'])
            check('travel preserves fractional learning',abs(progression.get_practice_progress('Willpower')-expected['fraction'])<1.e-8)
            check('travel preserves native level and XP',stats.get_current_level()==expected['level'] and stats.get_current_exp()==expected['xp'])
            check('travel preserves skill points',skills.get_skill_points()==expected['skills'] and stats.get_available_perks()==0)
            check('travel preserves book progress',abs(training.get_book_hours('QA_Book')-expected['book_hours'])<1.e-8)
            restored=next((node for tree,node in skill_nodes(skills) if node.get_node_id().export_text()==expected['skill_id']),None)
            check('travel restores learned native skill',restored is not None and skills.get_skill_current_level(restored.get_node_id())==1)
            unreal.GameplayStatics.open_level(world,'/Game/FullSample/Integrations/UIIntegrations/Level/MenuMap')
            phase('new_menu')
        elif state['phase']=='new_character' and age>10:
            check('new character has no previous learned rank',progression.get_learned_rank('Willpower')==0)
            check('new character has no previous practice fraction',progression.get_practice_progress('Willpower')==0)
            check('new character has no previous book progress',training.get_book_hours('QA_Book')==0)
            check('new character starts at native level one',stats.get_current_level()==1)
            finish()
        elif state['phase']=='defeat' and age>10:
            defeat=pawn.get_component_by_class(cls('ProjectDefeatFlowComponent'))
            current=str(defeat.get_current_phase()).upper()
            if 'DEFEATED_SCENE' in current:
                emotes.cancel_runtime_action('PracticeQA');return
            if 'NONE' not in current:return
            check('respawn preserves learned rank',progression.get_learned_rank('Willpower')==expected['rank'])
            check('respawn preserves fractional learning',abs(progression.get_practice_progress('Willpower')-expected['fraction'])<1.e-8)
            check('respawn preserves native level and XP',stats.get_current_level()==expected['level'] and stats.get_current_exp()==expected['xp'])
            check('respawn preserves skill points',skills.get_skill_points()==expected['skills'] and stats.get_available_perks()==0)
            check('respawn preserves book progress',abs(training.get_book_hours('QA_Book')-expected['book_hours'])<1.e-8)
            check('respawn player is alive',pawn.get_component_by_class(cls('ACFDamageHandlerComponent','AscentCombatFramework')).get_is_alive())
            finish()
    except Exception:finish(traceback.format_exc())
    finally:state['busy']=False

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle=unreal.register_slate_post_tick_callback(tick)
