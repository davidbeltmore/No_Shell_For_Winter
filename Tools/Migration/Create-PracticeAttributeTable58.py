"""Extend a project-owned copy of ACF's serialization table; never edit the source table."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_saved_dir())/'Analysis/PracticeProgression'
out.mkdir(parents=True,exist_ok=True)
source='/AscentCombatFramework/GASRuntime/ACF_SerializabeAttributes_DT'
destination='/Game/_Game/Data/Progression/DT_ProjectSerializableAttributes'
native=unreal.load_asset(source)
if not native: raise RuntimeError('Missing native attribute table')
rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(native))
(out/'native_attribute_rows.json').write_text(json.dumps(rows,indent=2),encoding='utf8')
attribute_template=next(row for row in rows if 'Strength' in json.dumps(row))
names=['Willpower','Offensive','Defensive','Faith','Cunning','Celerity','Charisma']
for name in names:
    # Export format is checked, rather than guessed, before an own asset is changed.
    field=attribute_template.get('Attribute')
    if not isinstance(field,dict) or 'Attribute' not in field:
        raise RuntimeError('Unexpected native FGameplayAttribute JSON schema; inspect native_attribute_rows.json')
    row=json.loads(json.dumps(attribute_template))
    row['Name']=name
    row['Attribute']['AttributeName']=name
    row['Attribute']['Attribute']='/Script/EFProjectSystemsGameplay.ProjectDoctrineAttributeSet:'+name
    row['Attribute']['AttributeOwner']="/Script/CoreUObject.Class'/Script/EFProjectSystemsGameplay.ProjectDoctrineAttributeSet'"
    rows.append(row)
table=unreal.load_asset(destination)
if not table:
    table=unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(destination.rsplit('/',1)[1],destination.rsplit('/',1)[0],native)
if not table: raise RuntimeError('Could not create own attribute table')
if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(rows)):
    raise RuntimeError('Attribute table import failed')
round_trip=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
if len(round_trip)!=len(rows): raise RuntimeError('Row count mismatch')
for name in names:
    row=next(x for x in round_trip if x['Name']==name)
    if row['Attribute']['Attribute'] != '/Script/EFProjectSystemsGameplay.ProjectDoctrineAttributeSet:'+name:
        raise RuntimeError('Attribute reference did not survive import')
unreal.EditorAssetLibrary.save_loaded_asset(table,only_if_is_dirty=False)
(out/'attribute_table.json').write_text(json.dumps({'source':source,'destination':destination,'rows':round_trip},indent=2),encoding='utf8')

# Native tag-based attribute lookup must resolve Doctrine on the same ASC too.
tag_source='/AscentCombatFramework/GASRuntime/Config/ACF_AttributesSetByCallers_DT'
tag_destination='/Game/_Game/Data/Progression/DT_ProjectAttributesSetByCallers'
tag_native=unreal.load_asset(tag_source)
tag_rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(tag_native))
(out/'native_attribute_tag_rows.json').write_text(json.dumps(tag_rows,indent=2),encoding='utf8')
template=next(row for row in tag_rows if row.get('Attribute',{}).get('AttributeName')=='Strength')
for name in names:
    row=json.loads(json.dumps(template))
    row['Name']=name
    row['Attribute']['AttributeName']=name
    row['Attribute']['Attribute']='/Script/EFProjectSystemsGameplay.ProjectDoctrineAttributeSet:'+name
    row['Attribute']['AttributeOwner']="/Script/CoreUObject.Class'/Script/EFProjectSystemsGameplay.ProjectDoctrineAttributeSet'"
    tag='RPG.PrimaryAttributes.'+name
    field=row['SetByCallerTag']
    if isinstance(field,dict) and 'TagName' in field:field['TagName']=tag
    elif isinstance(field,str):row['SetByCallerTag']=field.replace('RPG.PrimaryAttributes.Strength',tag)
    else:raise RuntimeError('Unexpected native tag JSON schema')
    if tag not in json.dumps(row['SetByCallerTag']):raise RuntimeError('Could not resolve native primary-attribute tag')
    row['UIName']=name
    tag_rows.append(row)
tag_table=unreal.load_asset(tag_destination)
if not tag_table:
    tag_table=unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(tag_destination.rsplit('/',1)[1],tag_destination.rsplit('/',1)[0],tag_native)
if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(tag_table,json.dumps(tag_rows)):
    raise RuntimeError('Tag mapping import failed')
actual=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(tag_table))
(out/'attribute_tag_round_trip.json').write_text(json.dumps({'expected':tag_rows,'actual':actual},indent=2),encoding='utf8')
actual_by_name={row['Name']:row for row in actual}
if len(actual_by_name)!=len(tag_rows):raise RuntimeError('Tag mapping row count changed')
for expected_row in tag_rows:
    actual_row=actual_by_name.get(expected_row['Name'],{})
    for key,value in expected_row.items():
        if key=='UIName' and expected_row['Name'] in names:
            # DataTable import gives new FText values stable localization keys.
            if value not in str(actual_row.get(key,'')):raise RuntimeError('New attribute display name changed')
        elif actual_row.get(key)!=value:raise RuntimeError('Tag mapping changed '+expected_row['Name']+'.'+key)
unreal.EditorAssetLibrary.save_loaded_asset(tag_table,only_if_is_dirty=False)
(out/'attribute_tag_table.json').write_text(json.dumps({'source':tag_source,'destination':tag_destination,'rows':actual},indent=2),encoding='utf8')
