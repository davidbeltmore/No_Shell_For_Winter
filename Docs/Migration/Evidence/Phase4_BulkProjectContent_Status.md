# Phase 4 broad project-content status

Snapshot date: 2026-07-13

Status: `IMPORT_COMPLETE_AGGREGATE_340_OF_341_ONE_LEGACY_BP_PENDING`

This is an evidence index, not a migration-closeout PASS. Raw evidence remains under `Saved/Migration/Phase4/BulkProjectContent/BULKREST_20260713_2015` and `Saved/Migration/Phase4/EssentialMaps/Runs/ESSMAPS_20260713_2054`.

## Scope decisions

- SaveGame migration and compatibility with legacy slots are `OUT_OF_SCOPE_BY_USER`.
- All visual QA is `USER_OWNED_OUT_OF_SCOPE`; the user will execute it.
- The 660 source packages under `/Game/ExportedAnimations` are `OUT_OF_SCOPE_BY_USER` and were not selected for the broad import. This exclusion does not assert that the target directory is empty: the pre-existing `/Game/ExportedAnimations/Anim_KA_Idle53_Seiza_Loop1` was not part of the 660-package cohort and was left untouched.
- Kawaii Quinn packages are under `/Game/KawaiiAnimations`, not `/Game/ExportedAnimations`, and therefore remain inside the imported-content integrity scope.

## Import milestone

| Cohort | Evidence-backed result | Evidence |
|---|---|---|
| Broad source-only project content | 1,667/1,667 `.uasset` packages selected by the exact UE 5.7 AssetTools cohort; exact target delta reported; 660 `/Game/ExportedAnimations` packages selected count is zero | `BulkProjectContent57Migration.json`: `ASSETTOOLS_EXACT_BULK_PROJECT_CONTENT57_PASS` |
| Essential maps | Three maps are present in the target and covered by the exact map cohort: `/Game/_Game/Hub/HUB`, `/Game/_Game/Locations/StorySelection`, and `/Game/_Game/Locations/PCGLevel` | `EssentialMaps57Migration.json`: `ASSETTOOLS_EXACT_ESSENTIAL_MAPS57_MIGRATION_PASS` |

The map migration evidence records the three packages as already-present collisions in its final exact run. It therefore proves the target-presence contract and unchanged bytes for that run, not that the final invocation created new map files.

## UE 5.8 integrity evidence

| Gate | Result | Disposition |
|---|---|---|
| Broad package load | 1,667/1,667 loaded | `PASS` for load coverage only |
| Final integrated Blueprint sweep | 340/341 `UP_TO_DATE`; the only failure is `/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController` | `FAIL` for complete Blueprint integrity; all 1,667 packages loaded, package bytes were unchanged, no asset-save operation occurred, and `/Game/ExportedAnimations` selected count remained zero |
| Food pickup parent repair | 81/81 reparented, compiled, saved, and independently validated read-only | `PASS` |
| Kawaii Quinn skeleton/pose repair | 14 animation assets, 14 pose assets, and one post-process AnimBlueprint repaired; independent validation reports the post-process Blueprint `UP_TO_DATE` and tracked files unchanged during validation | `PASS` |
| Calysto editor Blueprint cohort | 21/21 exact-remigrated; 21/21 load/compile/parent validation | `PASS` |
| QuangPhan combine master | XRBase function resolves and Blueprint is `UP_TO_DATE`; no asset write during validation | `PASS` |
| Calysto targeted repairs | `BP_MassiveDungeon` is `UP_TO_DATE`; `ST_SmartScatter` and `PDA_VegetationCalysto` have repair plus fresh read-only PASS receipts; `BP_CalystoController` remains `BS_ERROR` because `/Game/Calysto/World/Blueprint/Legacy/BP_Biome3_0` is absent | `PASS` for MassiveDungeon and SmartScatter/Vegetation; `PENDING` for the single inherited controller |
| Null-parent gameplay Blueprints | `/Game/Procedural/Blueprints/Altar` and `/Game/_Game/Lockpicking/Locked` were reparented to project-owned native classes, compile `UP_TO_DATE`, preserve their required override-event links, and pass independent fresh read-only validation with unchanged hashes | `PASS` |
| Essential maps in UE 5.8 | Read-only inspection found four unresolved `/Game` dependencies in HUB | `UE58_ESSENTIAL_MAPS_DEPENDENCY_GAP` |

The four currently recorded HUB dependency gaps are:

- `/Game/ExportedAnimations/Together/0001Scene` — deliberately excluded by user scope; no import is required by Codex, but the unresolved reference must not be represented as a technical PASS.
- `/Game/FullSample/Blueprints/Characters/Enemies/DummyMale` — `PENDING` target-authoritative resolution.
- `/Game/FullSample/Blueprints/Characters/Enemies/MeleeMale` — `PENDING` target-authoritative resolution.
- `/Game/FullSample/Integrations/Ultimate/Blueprint/Game/ACFBaseInteractableBP` — `PENDING` target-authoritative adapter or reference resolution.

## Focused evidence files

- `BulkProjectContent57Migration.json`
- `PostRepairBulkProjectContent58Validation_Final.json`
- `PostRepairBulkProjectContent58Validation_FoodKawaiiCalysto.json`
- `FoodPickupParentRepair/FoodPickupParentValidation58.json`
- `KawaiiQuinnRepair/KawaiiQuinnValidation58.json`
- `CalystoEditorBlueprintRepair/CalystoEditorBlueprintValidation58.json`
- `CalystoCore3Repair/CalystoCore3Validation58_Redirects.json`
- `CalystoSmartScatterRepair/CalystoSmartScatterRepair58.json`
- `CalystoSmartScatterRepair/CalystoSmartScatterValidation58.json`
- `QuangPhanXR/QuangPhanCombineMasterXR58.json`
- `NullParentGameplayRepair/NullParentGameplayRepair58.json`
- `NullParentGameplayRepair/NullParentGameplayValidation58.json`
- `EssentialMaps57Migration.json`
- `EssentialMaps58Validation.json`

## Closeout still required

- Resolve or explicitly disposition the single remaining inherited Blueprint failure, `/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController`; its serialized dependency `/Game/Calysto/World/Blueprint/Legacy/BP_Biome3_0` is absent.
- Revalidate the three maps after resolving all in-scope target-authoritative dependencies.
- The final source-read-only and protected-invariant gates are complete and `PASS` (`FinalGates/SourceReadOnlyVerification.json` and `FinalGates/ProtectedInvariantVerification.json`).
- Complete nonvisual PIE, cook/cooked-manifest, package, and packaged-runtime gates. Visual QA and legacy SaveGame/slot compatibility remain outside Codex scope by user direction.
