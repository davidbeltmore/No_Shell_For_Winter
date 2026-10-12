"""Bounded native item removal, immediate destruction/recreation, and load cancellation.

Run in a SaveGuard-protected PIE session. Uses existing appearance identities and
native inventory operations. Does not save assets or create replacement garments.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
out=root/'Saved/NPCOutfits/Lifecycle'/str(time.time_ns())
out.mkdir(parents=True)
editor=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.is_in_play_in_editor()
started=time.monotonic()
prefix='EF.AppearanceId.OutfitLifecycle.'
state={'stage':'spawn','checks':[],'busy':False}
pawns=[]
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
resident_cvar='Project.NPCOutfits.UseResidentPrerequisites'
previous_resident=unreal.SystemLibrary.get_console_variable_int_value(resident_cvar)
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)

def check(name,condition):
    state['checks'].append({'name':name,'status':'PASS' if condition else 'FAIL'})
    if not condition: raise AssertionError(name)

def spawn(world,gender,index):
    path=f'/Game/_Game/Characters/{gender}/ACF_SkillGuy_BP{gender}.ACF_SkillGuy_BP{gender}_C'
    player=unreal.GameplayStatics.get_player_pawn(world,0)
    pawn=unreal.ProjectRuntimeReflectionLibrary.spawn_actor_by_class_path(world,path,
        player.get_actor_location()+unreal.Vector(1600,index*220,25),unreal.Rotator())
    pawn.tags=list(pawn.tags)+[prefix+str(index)]
    pawn.set_actor_enable_collision(False)
    pawn.get_component_by_class(unreal.CharacterMovementComponent).disable_movement()
    return pawn

def ready(pawn):
    outfit=pawn.get_component_by_class(unreal.ProjectNPCOutfitComponent)
    if outfit and outfit.state==unreal.ProjectNPCOutfitState.FAILED: raise AssertionError(outfit.failure)
    return outfit and outfit.state==unreal.ProjectNPCOutfitState.READY

def snapshot(pawn):
    native=pawn.get_component_by_class(unreal.ACFEquipmentComponent)
    return sorted([{'guid':i.get_editor_property('item_guid').to_string(),'class':i.item_class.get_path_name(),
                    'count':i.count,'equipped':i.is_equipped,
                    'slot':str(i.equipment_slot.get_editor_property('TagName'))} for i in native.get_inventory()],key=lambda i:i['guid'])

def done(error=''):
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.SystemLibrary.execute_console_command(worlds.get_game_world(),resident_cvar+' '+str(previous_resident))
    state.update(status='FAIL' if error else 'PASS',reason=error,seconds=time.monotonic()-started)
    (out/'runtime.json').write_text(json.dumps(state,indent=2),encoding='utf-8')
    unreal.EditorLevelLibrary.editor_end_play()
    perf.set_editor_property('bThrottleCPUWhenNotForeground',throttle)

def tick(dt):
    if state['busy']: return
    state['busy']=True
    try:
        if time.monotonic()-started>120: raise TimeoutError('Lifecycle deadline: '+state['stage'])
        world=worlds.get_game_world()
        if not world or not unreal.GameplayStatics.get_player_pawn(world,0): return
        if state['stage']=='spawn':
            unreal.SystemLibrary.execute_console_command(world,resident_cvar+' 1')
            pawns.extend([spawn(world,g,i) for i,g in enumerate(['Male','Female'])])
            state['stage']='remove'; return
        if state['stage']=='remove':
            if not all(ready(p) for p in pawns): return
            state['before']=[snapshot(p) for p in pawns]
            state['expected']=[]
            state['body_before']=[str(p.get_component_by_class(unreal.EFCharacterCustomizationComponent).get_body_preset_id()) for p in pawns]
            for pawn in pawns:
                native=pawn.get_component_by_class(unreal.ACFEquipmentComponent)
                shirt=next(i for i in native.get_inventory() if 'RagShirt' in i.item_class.get_path_name())
                coordinator=pawn.get_component_by_class(unreal.EFEquipmentCoordinatorComponent)
                check('Unequip before native removal',coordinator.request_unequip(shirt.get_editor_property('item_guid')).success)
                native.remove_item(shirt,shirt.count)
                expected=snapshot(pawn)
                check('Native inventory removed shirt',not any('RagShirt' in i['class'] for i in expected))
                state['expected'].append(expected)
                # Intentionally no next tick between the final mutation and destruction.
                pawn.destroy_actor()
            pawns.clear()
            state['stage']='recreate'; return
        if state['stage']=='recreate':
            pawns.extend([spawn(world,g,i) for i,g in enumerate(['Male','Female'])])
            state['stage']='verify'; return
        if state['stage']=='verify':
            if not all(ready(p) for p in pawns): return
            state['after']=[snapshot(p) for p in pawns]
            check('Removed item stays removed with exact remaining GUIDs after recreate',state['after']==state['expected'])
            check('Appearance body survives recreate',state['body_before']==[str(p.get_component_by_class(unreal.EFCharacterCustomizationComponent).get_body_preset_id()) for p in pawns])
            pawns.append(spawn(world,'Male',2))
            state['stage']='cancel'; return
        if state['stage'] in ['cancel','cancel_async']:
            outfit=pawns[-1].get_component_by_class(unreal.ProjectNPCOutfitComponent)
            if not outfit: return
            resident=state['stage']=='cancel'
            expected=unreal.ProjectNPCOutfitState.PENDING if resident else unreal.ProjectNPCOutfitState.LOADING
            check('Destroy with resident prerequisites before equip' if resident else 'Destroy while async outfit load is pending',outfit.state==expected)
            state['resident_cancel_state' if resident else 'async_cancel_state']=str(outfit.state)
            for pawn in pawns: pawn.destroy_actor()
            pawns.clear()
            state['destroyed_at']=time.monotonic()
            state['stage']='drain_resident' if resident else 'drain'; return
        if state['stage'] in ['drain_resident','drain']:
            if time.monotonic()-state['destroyed_at']<1: return
            subsystem=next(o for o in unreal.ObjectIterator(unreal.ProjectNPCOutfitSubsystem) if o.get_outer()==world)
            state['pending_after_destroy']=subsystem.get_pending_count()
            check('Queue drained after destruction',state['pending_after_destroy']==0)
            if state['stage']=='drain_resident':
                unreal.SystemLibrary.execute_console_command(world,resident_cvar+' 0')
                pawns.append(spawn(world,'Male',3))
                state['stage']='cancel_async'; return
            done()
    except Exception: done(traceback.format_exc())
    finally: state['busy']=False

handle=unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
print('NPC outfit lifecycle QA scheduled: '+str(out))
