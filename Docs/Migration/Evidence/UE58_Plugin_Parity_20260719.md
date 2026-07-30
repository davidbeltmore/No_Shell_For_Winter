# UE 5.8 plugin parity and Calysto runtime evidence — 2026-07-19

## Scope

- Read-only reference descriptor: legacy private build (UE 5.7; identity omitted).
- Writable target: `NoShellForWinter.uproject` (UE 5.8).
- Source descriptor: 22 enabled plugin entries.
- Target descriptor after the change: 38 enabled plugin entries, zero duplicate names, and zero source-enabled plugins missing.

## UE 5.8 native enablement

The target now explicitly enables the installed UE 5.8 equivalents that were enabled only in the source descriptor:

- `MLDeformerFramework`
- `NiagaraFluids`
- `PCGExternalDataInterop`
- `PCGFastGeoInterop`
- `PCGGeometryScriptInterop`
- `PCGInstancedActorsInterop`
- `PCGPythonInterop`
- `Volumetrics`
- `WaterAdvanced`

`PCG` is also explicitly enabled even though its UE 5.8 descriptor uses
`EnabledByDefault=true`. This removes reliance on implicit engine defaults for
Calysto and `EFProcedural`. `PCGExtendedToolkit` remains the target-authoritative
0.76.2 installation and was not replaced by the UE 5.7 0.75.8 build.

## Validation

| Gate | Result | Evidence |
|---|---|---|
| Descriptor JSON and source/target set comparison | PASS | 22/22 source-enabled entries represented; target has 38 unique enabled entries |
| Cold UE 5.8 Editor build | PASS | `Saved/Migration/PluginParity_20260719/ColdBuildReopen.stdout.log`; 8.21 s |
| Live plugin mount after editor restart | PASS | `Saved/Logs/NoShellForWinter.log`; all ten explicit parity plugins mounted, with no plugin/module load error |
| Calysto/Procedural Blueprint compile | PASS | `Saved/Migration/PluginParity_20260719/CalystoProceduralBlueprintCompile58.json`; 74/74 compiled, zero failures, zero saves |
| UE 5.8 Game build | PASS | UBT `NoShellForWinter Win64 Development`; 26.29 s |
| DungeonGeneration PIE | FAIL_RUNTIME | `Saved/Migration/PluginParity_20260719/CalystoProceduralPIE58.json`; dungeon actor 1, PCG components 2, nav bounds 2, RecastNavMesh 1, player pawn present, StartPoint 0 |
| Cook | FAIL_GLOBAL_AFTER_COOK | `Saved/Migration/PluginParity_20260719/Cook_DungeonGeneration_Windows.log`; 7,017/7,017 processed, then global failure summary from two pre-existing `GameFeatureData` Asset Manager errors |
| Packaged runtime | PENDING | Not promoted because PIE and global cook gates are not clean |

The focused PIE run proves that the enabled PCG stack loads and executes. It
does not prove complete Calysto generation: `BP_MassiveDungeonRuntime -
PathfindingElement_2` reports that its search cannot be completed, and no
`BP_StartPoint` is generated before the 120-second timeout.

Cook also reports a stale Calysto dependency from
`/Game/Calysto/Shared/PCG/PCG_LoopPOI` to
`/Game/Massive/MassiveDungeon/Demo/LevelInstance/PCGDA_Table`. That package is
absent from both source and target, so it is not a valid source migration
candidate and was not fabricated or copied.

## Safety

- No Marketplace or Engine plugin file was modified.
- No `.uasset` or `.umap` was saved by this plugin-parity phase.
- Source read-only verification passes before and after:
  `PRE_SourceReadOnly.json` and `POST_SourceReadOnly.json`.
- Protected ACFU and DazToUnreal plugin sets remain PASS.
- The target began with 69 pre-existing protected baseline mismatches in current
  user-owned Player/Male/Daz work and ended with the exact same 69 canonical
  mismatch records; pre/post mismatch delta is zero.
- Protected receipts:
  `PRE_ProtectedInvariants.json` and `POST_ProtectedInvariants.json`.

The project-owned PIE harness was updated only for UE 5.8 Python API
compatibility: the removed keep-alive API is now optional, and world time is
queried through `GameplayStatics`.
