"""Read the actual bounded witness incidence before accepting generated LODs."""
import json,time
from pathlib import Path
import unreal
start=time.monotonic();rows=[]
registry=unreal.load_asset('/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry')
for binding in registry.get_editor_property('native_source_bindings'):
    if not binding.get_name().startswith(('DA_RagShirt','DA_RagPants','DA_RagFootWraps')): continue
    for pair in binding.get_editor_property('lod_pair_bindings'):
        if time.monotonic()-start>120: raise TimeoutError('Witness inspection deadline')
        counts=[0]*len(pair.get_editor_property('vertex_bindings'))
        witnesses=pair.get_editor_property('witnesses')
        for w in witnesses:
            vertices=w.get_editor_property('garment_render_vertex_indices')
            weights=w.get_editor_property('garment_barycentrics')
            for v,weight in [(vertices.x,weights.x),(vertices.y,weights.y),(vertices.z,weights.z)]:
                if weight>1e-6: counts[v]+=1
        rows.append({'binding':binding.get_path_name(),'lod':pair.get_editor_property('garment_topology').get_editor_property('lod_index'),'vertices':len(counts),'witnesses':len(witnesses),'max_incidence':max(counts),'mean_incidence':sum(counts)/len(counts)})
out=Path(unreal.Paths.project_dir())/'Saved/NPCOutfits/GarmentLOD'/('witness-counts-'+str(time.time_ns())+'.json')
assert rows, 'No candidate garment bindings found'
for row in rows:
    row['within_incidence_bound']=row['max_incidence']<=256
    row['within_plane_capacity']=row['witnesses']<=row['vertices']*16
reduced=[row for row in rows if row['lod']==1]
passed=len(reduced)==9 and all(row['within_incidence_bound'] and row['within_plane_capacity'] for row in rows)
out.write_text(json.dumps({'status':'PASS' if passed else 'FAIL','seconds':time.monotonic()-start,'expected_reduced_bindings':9,'rows':rows},indent=2),encoding='utf-8')
print(str(out))

assert passed, 'Candidate bindings do not meet incidence/plane capacity or expected three-body coverage'
