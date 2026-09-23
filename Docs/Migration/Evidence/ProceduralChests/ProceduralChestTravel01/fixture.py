"""Actual map unload/reload and disk save of unloaded chest state, with guarded user saves."""
import json, os, pathlib, time, traceback
import unreal
OUT=pathlib.Path(unreal.Paths.project_dir())/'Saved/EquipmentQA'/os.environ['EF_EQUIPMENT_QA_RUN']
S={'phase':'start','start':time.monotonic(),'due':0,'busy':False}
R={'status':'PENDING','checks':[]}
ED=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
LE=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
MAP='/Game/_Game/Tests/ChestProcedural'
def write():(OUT/'result.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
def check(ok,name):R['checks'].append({'name':name,'pass':bool(ok)});write();assert ok,name
def step(phase,delay=0):S.update(phase=phase,due=time.monotonic()+delay);R['phase']=phase;write()
def stocks(world):
    cls=unreal.load_class(None,'/Game/_Game/Items/Chests/BP_EquipmentDeposit.BP_EquipmentDeposit_C')
    return {a.get_component_by_class(unreal.ProjectProceduralChestComponent).get_editor_property('stable_identity').to_string():a.get_component_by_class(unreal.ProjectChestInventoryComponent) for a in unreal.GameplayStatics.get_all_actors_of_class(world,cls)}
def contents(stock):return sorted((i.get_editor_property('item_guid').to_string(),i.get_editor_property('item_class').get_path_name(),i.get_editor_property('count')) for i in stock.get_inventory())
def verify(world,label):
    found=stocks(world);check(set(found)==set(S['expected']),label+' stable identities retained')
    for key,stock in found.items():
        check(stock.has_generated_contents(),label+' generated marker '+key)
        check(contents(stock)==S['expected'][key],label+' exact remaining GUIDs/counts '+key)
        stock.ensure_contents();check(contents(stock)==S['expected'][key],label+' opening does not refill '+key)
def finish(error=None):
    if not unreal.EFEquipmentAuthoringLibrary.end_save_fixture():error=(error or '')+' save fixture cleanup failed'
    R['status']='FAIL' if error else 'PASS'
    if error:R['error']=error
    write();unreal.unregister_slate_post_tick_callback(S['callback']);unreal.EditorLevelLibrary.editor_end_play();unreal.SystemLibrary.quit_editor()
def tick(dt):
    if S['busy']:return
    S['busy']=True
    try:
        if time.monotonic()-S['start']>240:raise RuntimeError('Chest travel deadline: '+S['phase'])
        if time.monotonic()<S['due']:return
        p=S['phase'];w=ED.get_game_world()
        if p=='start':LE.load_level(MAP);LE.editor_request_begin_play();step('generate',5)
        elif p=='generate':
            if not w or not unreal.GameplayStatics.get_player_pawn(w,0):return
            found=stocks(w);check(len(found)==3,'Three source test chests')
            for index,(key,stock) in enumerate(sorted(found.items())):
                stock.ensure_contents();check(stock.has_generated_contents(),'Generate before travel '+key)
                if index:
                    for item in list(stock.get_inventory()):stock.remove_item(item,item.get_editor_property('count'))
            S['expected']={key:contents(stock) for key,stock in found.items()};R['expected']=S['expected'];write()
            check(bool(unreal.EFEquipmentAuthoringLibrary.begin_save_fixture(w)),'Isolated native disk save fixture')
            unreal.GameplayStatics.open_level(w,'/Game/_Game/Hub/HUB');step('away',6)
        elif p=='away':
            if not w or 'HUB' not in w.get_name() or not unreal.GameplayStatics.get_player_pawn(w,0):return
            check(unreal.EFEquipmentAuthoringLibrary.save_world_fixture(w),'World save includes unloaded test-map chest records')
            unreal.GameplayStatics.open_level(w,MAP);step('returned',6)
        elif p=='returned':
            if not w or 'ChestProcedural' not in w.get_name():return
            verify(w,'Actual map round trip')
            for stock in stocks(w).values():
                for item in list(stock.get_inventory()):stock.remove_item(item,item.get_editor_property('count'))
            check(all(not s.get_inventory() for s in stocks(w).values()),'Deliberate post-save contents differ from saved snapshot')
            check(unreal.EFEquipmentAuthoringLibrary.load_world_fixture(w),'Load native disk slot after changing source contents')
            step('loaded_hub',6)
        elif p=='loaded_hub':
            if not w or 'HUB' not in w.get_name() or unreal.EFEquipmentAuthoringLibrary.is_save_fixture_busy(w):return
            unreal.GameplayStatics.open_level(w,MAP);step('disk_return',6)
        elif p=='disk_return':
            if not w or 'ChestProcedural' not in w.get_name():return
            verify(w,'Disk load of unloaded actor state');finish()
    except Exception:finish(traceback.format_exc())
    finally:S['busy']=False
write();unreal.EditorPythonScripting.set_keep_python_script_alive(True);S['callback']=unreal.register_slate_post_tick_callback(tick)
