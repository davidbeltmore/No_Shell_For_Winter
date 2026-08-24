# Calysto V2 spawner preset expansion — 2026-08-13

## Scope

- Target project: `D:/Projects UE5/NoShellForWinter`
- Target DataTable: `/Game/_Game/Data/CalystoDungeon/V2/DT_CalystoSpawnerPresets`
- Row struct: `/Script/EFProceduralRuntime.EFCalystoSpawnerPresetRow`
- Vendor Calysto assets and `BP_MassiveDungeon` were not edited or saved.

## Inventory and change

The UE 5.8 Unreal MCP `ObjectTools.search_subclasses` query for `Character` + `Enemy`, correlated with the target `/Game/_Game/Characters` asset inventory, found these 16 target Blueprint classes:

```text
/Game/_Game/Characters/Female/ACFDummyAmbushEnemyBPFemale.ACFDummyAmbushEnemyBPFemale_C
/Game/_Game/Characters/Female/ACFDummyEnemyBPFemale.ACFDummyEnemyBPFemale_C
/Game/_Game/Characters/Female/ACFDefenderEnemyBPFemale.ACFDefenderEnemyBPFemale_C
/Game/_Game/Characters/Female/ACFGunEnemyBPFemale.ACFGunEnemyBPFemale_C
/Game/_Game/Characters/Female/ACFMageEnemyBPFemale.ACFMageEnemyBPFemale_C
/Game/_Game/Characters/Female/ACFMeleeEnemyBPFemale.ACFMeleeEnemyBPFemale_C
/Game/_Game/Characters/Female/ACFMMEnemyBPFemale.ACFMMEnemyBPFemale_C
/Game/_Game/Characters/Female/ACFRangedEnemyBPFemale.ACFRangedEnemyBPFemale_C
/Game/_Game/Characters/Male/ACFDummyAmbushEnemyBPMale.ACFDummyAmbushEnemyBPMale_C
/Game/_Game/Characters/Male/ACFDummyEnemyBPMale.ACFDummyEnemyBPMale_C
/Game/_Game/Characters/Male/ACFDefenderEnemyBPMale.ACFDefenderEnemyBPMale_C
/Game/_Game/Characters/Male/ACFGunEnemyBPMale.ACFGunEnemyBPMale_C
/Game/_Game/Characters/Male/ACFMageEnemyBPMale.ACFMageEnemyBPMale_C
/Game/_Game/Characters/Male/ACFMeleeEnemyBPMale.ACFMeleeEnemyBPMale_C
/Game/_Game/Characters/Male/ACFMMEnemyBPMale.ACFMMEnemyBPMale_C
/Game/_Game/Characters/Male/ACFRangedEnemyBPMale.ACFRangedEnemyBPMale_C
```

All three rows (`Balanced`, `SoftShift`, `RangedShift`) now contain the same 16 unique canonical entries. `ACFMageEnemyBPMale` is included. The 12 combat variants have non-zero policy weights; the four `Dummy`/`DummyAmbush` variants are catalogued with weight `0` so they cannot be selected as normal dungeon enemies.

The five legacy class/weight fields remain intact for compatibility, while `bUseLegacyFiveSlotEntries=false` keeps `SpawnerEntries` authoritative.

## Evidence

| Gate | Result | Evidence |
|---|---|---|
| Live UE 5.8 MCP preflight | PASS | `http://127.0.0.1:8000/mcp`; DataTable schema, rows, dirty state, class inventory, and parent-class inspection performed read-only before mutation. |
| DataTable edit/save | PASS | Table saved through `DataTableTools.set_rows` and `AssetTools.save_assets`; post-save `is_dirty=false`. |
| Table package delta | PASS | Before: 5,444 bytes, SHA-256 `A3A777E0D43A5108CBF3D8EC50319DEA6D175E8BC5F581ACC21B71E0DE417023`. After: 8,932 bytes, SHA-256 `19CCC672C4DA065C40DBEBE847A3C5E7B6F0A68C93D505CB29E7B52ED7C7DFC4`. |
| Cold editor build | PASS | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; `EFCalystoDungeonPolicyTests.cpp` compiled; Daz receipt repair PASS. |
| Protected Calysto assets | PASS | `Saved/Migration/CalystoDungeonMaster/ProtectedAssetsAfterSpawnerExpansion.json`; zero mismatches against the preflight baseline. |
| PIE V2 table read-only gate | PASS | `Saved/Migration/CalystoDungeonMaster/SpawnerExpansionPIE58.json`: all V2 tables loaded read-only, floorless schema, `asset_mutations=[]`, `asset_saves=[]`. |
| Runtime policy application | PASS | `Saved/Migration/Logs/CalystoDungeonMasterSpawnerExpansionPIE58.log`: V2 policy compiled, 16 enemy classes preloaded, adapter applied `spawnerEntries=12`, and requested one controlled `GenerateLocal`. |
| Full Floor 1–10 PIE harness | PENDING | Harness ended with `bootstrap did not reach Floor 1 / GenerationSerial 1 runtime readiness`; door/replay/advance checks were therefore not promoted to PASS. |
| Full protected invariant manifest | PENDING | `Target_Daz_Assets` already fails the repository's broader pre-existing invariant baseline; this is unrelated to the Calysto protected-asset hash gate. |

## Durable authoring/test updates

- `Tools/Migration/Create-CalystoDungeonPolicyTablesV2.py` now authors the 16-entry canonical arrays instead of reverting to five entries.
- `Plugins/EFProcedural/Source/EFProceduralEditor/Private/Tests/EFCalystoDungeonPolicyTests.cpp` now asserts the 16 target paths and per-preset weights.
