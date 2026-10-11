"""Real EFEquipment/ALS disk save/load with modified NPC inventory and appearance checks.

Run in a SaveGuard-protected editor session. Dedicated QA slot only; user saves
are restored after editor shutdown by SaveGuard.ps1.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/NPCOutfits/Save'/str(time.time_ns())
out.mkdir(parents=True)
started=time.monotonic()
slot='NPCOutfits_QA_'+out.name
prefix='EF.AppearanceId.NPCOutfitSaveQA.'
state={'stage':'spawn','checks':[],'status':'PENDING','busy':False,'slot':slot}
delegates=[]
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.is_in_play_in_editor()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)

def check(name,ok):
    state['checks'].append({'name':name,'status':'PASS' if ok else 'FAIL'})
    if not ok: raise AssertionError(name)

def fixtures(world):
    return {str(t):p for p in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Pawn) for t in p.tags if str(t).startswith(prefix)}

def snapshot(world):
    result={}
    for identity,pawn in fixtures(world).items():
        custom=pawn.get_component_by_class(unreal.EFCharacterCustomizationComponent)
        equipment=pawn.get_component_by_class(unreal.ACFEquipmentComponent)
        appearance=custom.capture_current_state()
        result[identity]={'body':str(custom.get_body_preset_id()),
                          'morphs':{str(m.morph_name):round(m.value,5) for m in appearance.morph_values},
                          'items':sorted([{'guid':i.get_editor_property('item_guid').to_string(),'class':i.item_class.get_path_name(),'count':i.count,
                                           'equipped':i.is_equipped,'slot':str(i.equipment_slot.get_editor_property('TagName'))}
                                          for i in equipment.get_inventory()],key=lambda i:i['guid'])}
    return result

def native_save(world):
    gi=unreal.GameplayStatics.get_game_instance(world).get_path_name()
    return next(o for o in unreal.ObjectIterator(unreal.ALSLoadAndSaveSubsystem) if o.get_path_name().startswith(gi))

def done(error=''):
    unreal.unregister_slate_post_tick_callback(handle)
    state.update(status='FAIL' if error else 'PASS',reason=error,seconds=time.monotonic()-started)
    (out/'runtime.json').write_text(json.dumps(state,indent=2),encoding='utf-8')
    unreal.EditorLevelLibrary.editor_end_play()
    perf.set_editor_property('bThrottleCPUWhenNotForeground',throttle)

def saved(ok): state['saved']=bool(ok)
def loaded(ok): state['loaded']=bool(ok)

def tick(dt):
    if state['busy']: return
    state['busy']=True
    try:
        if time.monotonic()-started>120: raise TimeoutError('120-second save QA deadline: '+state['stage'])
        world=worlds.get_game_world()
        player=unreal.GameplayStatics.get_player_pawn(world,0) if world else None
        if not player: return
        if state['stage']=='spawn':
            for i,gender in enumerate(['Male','Female']):
                path=f'/Game/_Game/Characters/{gender}/ACF_SkillGuy_BP{gender}.ACF_SkillGuy_BP{gender}_C'
                pawn=unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world,path,player.get_actor_location()+unreal.Vector(1500,i*250,25),unreal.Rotator())
                pawn.tags=list(pawn.tags)+[prefix+str(i)]
                pawn.set_actor_enable_collision(False)
                pawn.get_component_by_class(unreal.CharacterMovementComponent).disable_movement()
            state['stage']='ready'; return
        if state['stage']=='ready':
            pawns=fixtures(world)
            if len(pawns)!=2: return
            for pawn in pawns.values():
                outfit=pawn.get_component_by_class(unreal.ProjectNPCOutfitComponent)
                if not outfit: return
                if outfit.state==unreal.ProjectNPCOutfitState.FAILED: raise AssertionError(outfit.failure)
                if outfit.state!=unreal.ProjectNPCOutfitState.READY: return
            for pawn in pawns.values():
                coordinator=pawn.get_component_by_class(unreal.EFEquipmentCoordinatorComponent)
                equipment=pawn.get_component_by_class(unreal.ACFEquipmentComponent)
                shirt=next(i for i in equipment.get_inventory() if 'RagShirt' in i.item_class.get_path_name())
                check('Unequip shirt',coordinator.request_unequip(shirt.get_editor_property('item_guid')).success)
            state['stage']='save'; return
        if state['stage']=='save':
            state['before']=snapshot(world)
            callback=unreal.OnSaveFinished(); callback.bind_callable(saved); delegates.append(callback)
            unreal.EFEquipmentSaveLibrary.save_game_world(native_save(world),slot,callback,True,False,'NPC outfit modified inventory QA')
            state['stage']='saving'; return
        if state['stage']=='saving':
            if 'saved' not in state: return
            check('Disk save succeeded',state['saved'])
            for pawn in fixtures(world).values():
                equipment=pawn.get_component_by_class(unreal.ACFEquipmentComponent)
                coordinator=pawn.get_component_by_class(unreal.EFEquipmentCoordinatorComponent)
                shirt=next(i for i in equipment.get_inventory() if 'RagShirt' in i.item_class.get_path_name())
                check('Alter state after save',coordinator.request_equip(shirt.get_editor_property('item_guid'),shirt.item.get_possible_item_slots()[0]).success)
            callback=unreal.OnLoadFinished(); callback.bind_callable(loaded); delegates.append(callback)
            unreal.EFEquipmentSaveLibrary.load_game_world(native_save(world),slot,callback,True)
            state['stage']='loading'; return
        if state['stage']=='loading':
            if 'loaded' not in state: return
            check('Disk load succeeded',state['loaded'])
            state['stage']='restored'; return
        if state['stage']=='restored':
            pawns=fixtures(world)
            if len(pawns)!=2: return
            for pawn in pawns.values():
                component=pawn.get_component_by_class(unreal.ProjectNPCOutfitComponent)
                if not component or component.state!=unreal.ProjectNPCOutfitState.READY: return
            state['after']=snapshot(world)
            if state['after']!=state['before']: return
            check('Exact GUID/count/equipment/body/morph restoration',state['after']==state['before'])
            check('No duplicate GUIDs',all(len({i['guid'] for i in row['items']})==len(row['items']) for row in state['after'].values()))
            done()
    except Exception: done(traceback.format_exc())
    finally: state['busy']=False
handle=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('NPC outfit save QA scheduled: '+str(out))
