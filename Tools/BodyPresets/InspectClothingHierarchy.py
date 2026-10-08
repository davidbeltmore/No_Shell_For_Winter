import json
from pathlib import Path
import unreal
out = Path(unreal.Paths.project_dir()) / 'Saved/BodyPresetsQA/clothing-hierarchy.json'
body = unreal.load_asset('/Game/DazToUnreal/Male2/Male2')
skeleton = body.get_editor_property('skeleton')
def hierarchy(mesh):
    modifier = unreal.SkeletonModifier(); assert modifier.set_skeletal_mesh(mesh)
    return {str(b): str(modifier.get_parent_name(b)) for b in modifier.get_all_bone_names()}
target = hierarchy(body)
director = unreal.load_asset('/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector')
report = {'body_skeleton': skeleton.get_path_name(), 'compatible': [str(x) for x in skeleton.get_editor_property('compatible_skeletons')], 'rows': []}
for row in director.get_editor_property('garments'):
    source = row.get_editor_property('source_garment')
    if not source: continue
    data = hierarchy(source)
    common = set(data) & set(target)
    def nearest(bone, tree):
        parent = tree[bone]
        while parent in tree and parent not in common: parent = tree[parent]
        return parent
    diffs = [{'bone': b, 'source': nearest(b,data), 'body': nearest(b,target)} for b in common if nearest(b,data) != nearest(b,target)]
    report['rows'].append({'source':source.get_path_name(), 'skeleton':source.get_editor_property('skeleton').get_path_name(), 'shared':len(common), 'source_bones':len(data), 'differences':diffs})
out.write_text(json.dumps(report, indent=2))
print(json.dumps(report))
