"""Create one owned, minimal packaged initial-loading fixture, then restore HUB.

The placed NPC supplies production initialization observations independently of
the native QA group. An owned copy of the current game mode uses DefaultPawn to
avoid the player's inventory while preserving native ACF services.
"""
import json
import time
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
path = '/Game/_Game/QA/NPCOutfitColdStart'
out = root / 'Saved/NPCOutfits/ColdMap'
out.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
worlds = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.is_in_play_in_editor()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not unreal.EditorAssetLibrary.does_asset_exist(path), 'Never overwrite an existing QA map'
original = worlds.get_editor_world().get_path_name().split('.')[0]
report = {'status': 'PENDING', 'map': path, 'return_map': original}
try:
    assert unreal.EditorLevelLibrary.new_level(path)
    world = worlds.get_editor_world()
    mode_path = '/Game/_Game/QA/BP_NPCOutfitColdGameMode'
    assert not unreal.EditorAssetLibrary.does_asset_exist(mode_path)
    mode = unreal.EditorAssetLibrary.duplicate_asset(
        '/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFUltimateGameModeBP', mode_path)
    assert mode
    mode_class = unreal.EditorAssetLibrary.load_blueprint_class(mode_path)
    mode_defaults = unreal.get_default_object(mode_class)
    mode_defaults.set_editor_property('default_pawn_class', unreal.DefaultPawn)
    mode_defaults.set_editor_property('player_controller_class', unreal.PlayerController)
    mode_defaults.set_editor_property('hud_class', unreal.HUD)
    unreal.BlueprintEditorLibrary.compile_blueprint(mode)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mode, only_if_is_dirty=False)
    world.get_world_settings().set_editor_property('default_game_mode', mode_class)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 150))
    start.set_actor_label('ColdFixturePlayerStart')
    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
    floor.set_actor_scale3d(unreal.Vector(60, 60, 1))
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, -35, 0))
    sun.set_actor_label('ColdFixtureSun')
    actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500))
    cls = unreal.load_class(None, '/Game/_Game/Characters/Male/ACF_SkillGuy_BPMale.ACF_SkillGuy_BPMale_C')
    assert cls
    npc = actors.spawn_actor_from_class(cls, unreal.Vector(900, 0, 95), unreal.Rotator(0, 180, 0))
    npc.set_actor_label('ColdOutfitProbe')
    npc.set_editor_property('tags', list(npc.tags) + [unreal.Name('EF.AppearanceId.ColdOutfitProbe')])
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
    report.update(status='PASS', npc_class=cls.get_path_name(),
                  game_mode=world.get_world_settings().get_editor_property('default_game_mode').get_path_name())
finally:
    assert unreal.EditorLoadingAndSavingUtils.load_map(original)
    report['seconds'] = time.monotonic() - started
    (out / 'author.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report))
