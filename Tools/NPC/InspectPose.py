import unreal,json
rows=[]
for p in ['/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale','/Game/_Game/Characters/Male/ACF_SkillGuy_BPMale']:
 a=unreal.get_default_object(unreal.load_asset(p).generated_class()); m=a.get_component_by_class(unreal.SkeletalMeshComponent)
 rows.append({'path':p,'rotation':str(m.get_editor_property('relative_rotation')),'location':str(m.get_editor_property('relative_location')),'simulate':m.is_simulating_physics()})
print(json.dumps(rows))
print(unreal.Rotator.__doc__[:1500])

