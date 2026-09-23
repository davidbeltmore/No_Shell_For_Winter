"""Visible Blueprint Details and generator-free real PIE. Uses isolated QA map/saves."""
import json, os, pathlib, time, traceback
import unreal
OUT=pathlib.Path(unreal.Paths.project_dir())/'Saved/EquipmentQA'/os.environ['EF_EQUIPMENT_QA_RUN']
S={'phase':'details_deposit','due':time.monotonic()+2,'start':time.monotonic(),'busy':False}
R={'status':'PENDING','checks':[]}
LE=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ED=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
def write(): (OUT/'result.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
def check(ok,name):
    R['checks'].append({'name':name,'pass':bool(ok)});write();assert ok,name
def step(phase,delay=0): S.update(phase=phase,due=time.monotonic()+delay);R['phase']=phase;write()
def entries(storage):return [(i.get_editor_property('item_class').get_path_name(),i.get_editor_property('count'),i.get_editor_property('item_guid').to_string()) for i in storage.get_inventory()]
def finish(error=None):
    R['status']='FAIL' if error else 'PASS'
    if error:R['error']=error
    write();unreal.unregister_slate_post_tick_callback(S['callback'])
    if ED.get_game_world():unreal.EditorLevelLibrary.editor_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    if S['busy']:return
    S['busy']=True
    try:
        if time.monotonic()-S['start']>420:raise RuntimeError('Procedural chest QA deadline: '+S['phase'])
        if time.monotonic()<S['due']:return
        phase=S['phase']
        if phase in ('details_deposit','details_locked'):
            name='BP_EquipmentDeposit' if phase=='details_deposit' else 'BP_EquipmentLockedChest'
            bp=unreal.load_asset('/Game/_Game/Items/Chests/'+name)
            if os.environ.get('EF_CHEST_ACTOR_DETAILS_QA') == '1':
                unreal.BlueprintEditorLibrary.compile_blueprint(bp)
                unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
                cls = unreal.load_class(None, '/Game/_Game/Items/Chests/'+name+'.'+name+'_C')
                actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
                candidates = [a for a in actors.get_all_level_actors() if a.get_class() == cls]
                check(bool(candidates), 'Existing HUB actor for '+name)
                actors.set_selected_level_actors([candidates[0]])
                check(candidates[0].get_component_by_class(unreal.ProjectProceduralChestComponent) is not None,
                      'Selected actor uses existing procedural component: '+name)
            else:
                check(unreal.ProjectLockpickingEditorLibrary.show_procedural_chest_details(bp),'Select visible ProceduralChest in '+name)
            step(phase+'_review',1)
        elif phase.endswith('_review'):
            marker=OUT/(phase+'.txt')
            if not marker.exists():return
            check(marker.read_text().startswith('PASS:'),'Actual Details screenshot reviewed: '+phase)
            step('details_locked' if phase=='details_deposit_review' else 'test_map')
        elif phase=='test_map':
            path='/Game/_Game/Tests/ChestProcedural'
            if unreal.EditorAssetLibrary.does_asset_exist(path):LE.load_level(path)
            else:
                check(unreal.EditorLevelLibrary.new_level(path),'Create owned generator-free test map')
                actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
                floor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,-100))
                floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
                floor.set_actor_scale3d(unreal.Vector(30,30,1));floor.set_actor_label('ChestTestFloor')
                actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,500),unreal.Rotator(-45,0,0))
                actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,500))
                actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(-400,0,100))
                cls=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentDeposit.BP_EquipmentDeposit_C')
                for index,kind in enumerate(['Wealth','Armament','Ammunition']):
                    actor=actors.spawn_actor_from_class(cls,unreal.Vector(index*220,0,0));actor.set_actor_label(kind)
                    cfg=actor.get_component_by_class(unreal.ProjectProceduralChestComponent)
                    cfg.set_editor_property('profile',unreal.load_asset('/Game/_Game/Data/Equipment/Chests/DA_Chest_'+kind))
                    cfg=actor.get_component_by_class(unreal.ProjectProceduralChestComponent);cfg.set_editor_property('procedural_mode',True)
                    cfg=actor.get_component_by_class(unreal.ProjectProceduralChestComponent);cfg.set_editor_property('stable_identity',actor.get_editor_property('actor_guid'))
                check(unreal.EditorLoadingAndSavingUtils.save_map(ED.get_editor_world(),path),'Save only owned test map')
            check(not any('Calysto' in a.get_class().get_name() or 'Dungeon' in a.get_class().get_name() for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()),'Test map contains no dungeon generator')
            LE.editor_request_begin_play();step('pie',5)
        elif phase=='pie':
            world=ED.get_game_world()
            if not world:return
            cls=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentDeposit.BP_EquipmentDeposit_C')
            chests=unreal.GameplayStatics.get_all_actors_of_class(world,cls)
            check(len(chests)==3,'Three authored chest types exist in generator-free PIE')
            R['contents']=[]
            for actor in chests:
                cfg=actor.get_component_by_class(unreal.ProjectProceduralChestComponent)
                stock=actor.get_component_by_class(unreal.ProjectChestInventoryComponent)
                check(cfg.get_editor_property('stable_identity').to_string()!='00000000000000000000000000000000','Stable instance identity: '+actor.get_name())
                cfg.validate_configuration();validation=cfg.get_editor_property('validation_result')
                check(validation.startswith('Valid:'),'Real catalog validates: '+actor.get_actor_label()+' '+validation)
                preview=cfg.preview_contents();R.setdefault('previews',[]).append(str(preview))
                check(not stock.has_generated_contents() and not stock.get_inventory(),'No BeginPlay generation: '+actor.get_actor_label())
                stock.ensure_contents();check(stock.has_generated_contents(),'Valid opening generated: '+actor.get_actor_label())
                before=entries(stock);check(bool(before),'Real catalog produced inventory: '+actor.get_actor_label())
                stock.ensure_contents();check(before==entries(stock),'Reopening retains exact GUIDs/counts: '+actor.get_actor_label())
                R['contents'].append({'chest':actor.get_actor_label(),'profile':cfg.get_editor_property('profile').get_path_name(),'items':before})
                for item in list(stock.get_inventory()):stock.remove_item(item,item.get_editor_property('count'))
                stock.ensure_contents();check(not stock.get_inventory() and stock.has_generated_contents(),'Empty chest never refills: '+actor.get_actor_label())
            unreal.EditorLevelLibrary.editor_end_play();step('reenter',2)
        elif phase=='reenter':
            # A new PIE GameInstance must start fresh; no cross-session global cache.
            LE.editor_request_begin_play();step('fresh',4)
        elif phase=='fresh':
            world=ED.get_game_world()
            if not world:return
            cls=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentDeposit.BP_EquipmentDeposit_C')
            check(all(not a.get_component_by_class(unreal.ProjectChestInventoryComponent).has_generated_contents() for a in unreal.GameplayStatics.get_all_actors_of_class(world,cls)),'Fresh game session has no stale process-global generation cache')
            finish()
    except Exception:finish(traceback.format_exc())
    finally:S['busy']=False
write();S['callback']=unreal.register_slate_post_tick_callback(tick)
