# Calysto Dungeon Master — Phase 2 evidence

Date: 2026-08-02  
Target: `D:/Projects UE5/NoShellForWinter` (UE 5.8)  
Source: `D:/Projects UE5/LustAsDeadlySin` (read-only)  
Scope of this receipt: integrated Phase 2 implementation and the validation completed as of this receipt

## Outcome

The integrated Phase 2 implementation is built and its core seed, policy, authoring, protected-asset, live PIE, cook, Development package, and Shipping package gates are validated. Full release acceptance remains `PENDING`: the packaged Development dungeon reaches PCG/navigation readiness but the protected cooked Calysto subgraph emits 16 missing `Object Transform` accessor errors, and the requested baseline-relative performance gate plus native MCP/Blueprint live-state gates are still pending. No Calysto core asset was patched to hide that blocker.

Phase 2 removes the historical Floor 3 terminal rule and the floor-indexed policy model. A run is transient state in the current GameInstance only. It is not written to `SaveGame`, and closing the game or destroying the GameInstance ends the run.

The production operations are:

| Operation | Result |
|---|---|
| New run | positive run seed, Floor 1, GenerationSerial 1 |
| New run with explicit seed | supplied positive seed, Floor 1, GenerationSerial 1 |
| Exact replay | same floor, same generation serial, identical frozen plan |
| Reroll | same floor, generation serial +1, newly resolved plan |
| Advance | floor +1, generation serial +1 |
| Debug jump | positive target floor, generation serial +1; Development only |

`FloorNumber` is a positive `int64` with no configured Floor 3 maximum. `GenerationSerial` is supported in `1..2147483647`; exhaustion fails closed and requires a new run. `RequestRegenerateFloor` remains only a deprecated exact-replay alias, and `SetRunSeed` remains only a deprecated new-run alias.

## Deterministic seed and policy contract

Let `M=2147483647`:

```text
A = 1 + Mix(RunSeed xor PCGSequenceMultiplierDomain) mod (M - 1)
B = Mix(RunSeed xor PCGSequenceOffsetDomain) mod M
PCGSeed = 1 + ((A * (GenerationSerial - 1) + B) mod M)
```

Layout, spawner, and theme selection use separate hash domains. Row names are sorted before weighted selection. The subsystem freezes one resolved plan before travel and records both a canonical V2 `PolicyHash` and a resolved `PlanHash`.

Replay must preserve the complete plan and PCG seed. Reroll and advance must receive the next generation serial. A new run with the same seed reproduces Floor 1 / GenerationSerial 1 only while the policy hash is unchanged.

## Floorless V2 tables

The only active Phase 2 policy paths are:

- `/Game/_Game/Data/CalystoDungeon/V2/DT_CalystoGenerationOptions`
- `/Game/_Game/Data/CalystoDungeon/V2/DT_CalystoSpawnerPresets`
- `/Game/_Game/Data/CalystoDungeon/V2/DT_CalystoThemePresets`

Their project-owned row structs are `FEFCalystoGenerationOptionRow`, `FEFCalystoSpawnerPresetRow`, and `FEFCalystoThemePresetRow`. Every V2 row has a conservative `SelectionWeight`; no V2 row or schema contains a `Floor` property.

`Tools/Migration/Create-CalystoDungeonPolicyTablesV2.py` is the explicit authoring script. A successful authoring receipt at `Saved/Migration/CalystoDungeonMaster/CreatePolicyTablesV2.json` must report exactly the three paths above in both `asset_mutations` and `asset_saves`, and must report `calysto_asset_mutations=[]`. Those saves are expected only during the authorized authoring step. All normal PIE evidence must still report `asset_mutations=[]` and `asset_saves=[]`.

The authoring script completed successfully. `Saved/Migration/CalystoDungeonMaster/CreatePolicyTablesV2.json` reports `status=PASS`, exactly the three V2 paths in both `asset_mutations` and `asset_saves`, `calysto_asset_mutations=[]`, the expected project-owned row structs, the exact preset names, and `contains_floor_field=false` for every table.

## Durable Phase 2 PIE contract

`Tools/Migration/Run-CalystoDungeonMasterPIE58.ps1 -RunSeed <positive-int64>` launches the protected UE 5.8 editor wrapper, requires schema-version-2 evidence from `Validate-CalystoDungeonMasterPIE58.py`, and rejects targeted fatal/ensure/Blueprint/PCG failure text in the paired log. The validator is designed to:

1. load all three V2 tables read-only and reject a wrong row struct, missing/empty rows, or any `Floor` property;
2. start a fixed-seed run at Floor 1 / GenerationSerial 1;
3. exact-replay and compare the complete resolved plan;
4. reroll Floor 1 and require GenerationSerial 2 plus a changed plan/PCG seed;
5. use the real ACF selection/interaction path to advance from Floor 1 through Floor 10;
6. require every sampled next-floor door through Floor 10 to be enabled/selectable only after PCG/navigation readiness;
7. restart the same seed and reproduce the original Floor 1 / GenerationSerial 1 plan;
8. require one runtime dungeon, one project-owned door, one non-editor runtime PCG component, completed PCG, navigation, consistent snapshot/plan hashes, and empty asset mutation/save arrays.

This is an in-session determinism test. It deliberately does not claim cross-session persistence; a fresh process owns a fresh GameInstance run.

## Gate ledger

| Gate | Status | Evidence or blocker |
|---|---|---|
| Authoritative `.agents` skill validation | `PASS_STATIC` | `quick_validate.py` completed successfully for `.agents/skills/calysto-dungeon-master` during this pass. |
| Claude mirror skill validation | `PASS_STATIC` | `quick_validate.py` completed successfully for `.claude/skills/calysto-dungeon-master` during this pass. |
| Phase 2 Python/PowerShell runner syntax | `PASS_STATIC` | Python AST and PowerShell parser checks completed successfully during the runner update; this gate covers syntax only, while the separate PIE gate records runtime execution. |
| V2 authoring script source contract | `PASS_STATIC` | Read-only inspection found only the three declared V2 package saves and `calysto_asset_mutations=[]`; the separate authoring-receipt gate records its successful execution. |
| V2 asset creation and authoring receipt | `PASS` | `Saved/Migration/CalystoDungeonMaster/CreatePolicyTablesV2.json`: exact three-table mutation/save surface, expected schemas and rows, no `Floor` fields, and `calysto_asset_mutations=[]`. |
| Native Unreal MCP preflight | `PENDING` | Native MCP transport was unavailable during preflight; the filesystem/PIE evidence does not substitute for an MCP live-state inspection. |
| C++ integration and editor build | `PASS` | `Tools/Migration/Build-NoShellForWinterEditor58.ps1` completed for UE 5.8 `NoShellForWinterEditor Win64 Development`, including receipt repair. |
| Automation tests | `PASS` | `Saved/Automation/CalystoPhase2FinalRecheckNative/index.json`: final binaries, 9 succeeded, 0 warnings, 0 failed. Includes one million unique PCG seeds, independent domains/boundaries, authored tables, fail-closed validation, six-sigma Monte Carlo, floorless schemas, and Dungeon Harness tests. |
| Blueprint compile and protected live dirty-state audit | `PENDING` | Requires a live UE 5.8 editor/MCP session. |
| Fixed-seed replay/reroll/Floor 10 PIE | `PASS` | `Saved/Migration/CalystoDungeonMaster/SeedReplayRerollAdvancePIE58_20260802_194956.json`: fixed seed `2026080201`, exact replay, changed reroll, real ACF doors through Floor 10, unique PCG seeds, same-seed restart reproduction, all checks true, `asset_mutations=[]`, and `asset_saves=[]`. |
| Layout fingerprint replay/reroll corpus | `PASS` | `Saved/Migration/CalystoDungeonMaster/SeedReplayRerollAdvancePIE58_20260802_200015.json`: 64/64 checks true; all eleven generated contexts have distinct physical-layout fingerprints, exact replay and same-seed restart reproduce the Floor 1 fingerprint, and reroll changes it. Every fingerprint used actors tagged `PCG Generated Actor`, not the defensive fallback. |
| Protected Calysto/map/door assets | `PASS` | `Saved/Migration/CalystoDungeonMaster/ProtectedAssets_Phase2ReleaseFinal.json`: all 13 protected files remain byte-identical after final build/package. `BP_MassiveDungeon` remains 407,681 bytes with SHA-256 `1B3BE0902C810A1F1E665CB3815AB8C9FEBD22921D03C722C6E314E3A3456BEC`. |
| Protected plugin/target invariant re-hash | `PASS_NO_NEW_DELTA` | `Saved/Migration/Evidence/CalystoPhase2_ProtectedInvariantVerification_ReleaseFinal.json`: ACFU 5,043/5,043 and DazToUnreal 213/213 pass. Its 74 pre-existing target Daz/Player baseline mismatches exactly match `Saved/Migration/Evidence/CalystoPhase2_ProtectedInvariantVerification.json`; final build/package introduced zero new mismatches. |
| Cook and V2 soft-reference inclusion | `PASS` | Full Development cook processed 6,713 packages and IoStore consumed 6,701. `Saved/Migration/Logs/CalystoPhase2_Development_UFSManifest.txt` contains DungeonGeneration and all three V2 DataTables; the runtime spawner/theme dependencies are also present. |
| Development build/cook/package | `PASS` | `Saved/Migration/Logs/CalystoPhase2_Development_BuildCookPackage.log`: UAT `BUILD SUCCESSFUL`, ExitCode 0, build/cook/stage/pak/IoStore/archive completed. |
| Shipping build/package | `PASS_PACKAGE` | `Saved/Migration/Logs/CalystoPhase2_Shipping_BuildPackage.log`: native Shipping compile plus stage/pak/IoStore/archive completed with ExitCode 0, reusing the validated Development cook. The packaged executable remained alive for the 20-second offscreen smoke; Shipping logging was unavailable, so this is stability evidence only. |
| Packaged dungeon runtime | `FAIL_RELEASE_BLOCKER` | Development direct-map smoke used one controlled `GenerateLocal`, reached navigation/floor-door ready, and did not crash, but logged exactly 16 `GetAttributeFromPointIndex_0` errors because protected Calysto attribute `Object Transform` was absent in cooked runtime. The same failure reproduced with seed 42, the known PIE seed, NullRHI, D3D12, and a diagnostic async-load drain. Calysto core remains untouched. |
| Performance comparison | `PENDING` | No runtime samples were captured. |

## Validated commands and receipts

```powershell
python "A:\Skills Codex\.system\skill-creator\scripts\quick_validate.py" ".agents\skills\calysto-dungeon-master"
python "A:\Skills Codex\.system\skill-creator\scripts\quick_validate.py" ".claude\skills\calysto-dungeon-master"
```

Both returned `Skill is valid!` after this evidence update.

The UE 5.8 editor build completed through:

```powershell
Tools/Migration/Build-NoShellForWinterEditor58.ps1
```

The completed runtime, package, and native evidence is recorded in:

- `Saved/Migration/CalystoDungeonMaster/CreatePolicyTablesV2.json`
- `Saved/Automation/CalystoPhase2FinalRecheckNative/index.json`
- `Saved/Migration/Logs/CalystoPhase2FinalRecheckNativeAutomation.log`
- `Saved/Migration/CalystoDungeonMaster/SeedReplayRerollAdvancePIE58_20260802_194956.json`
- `Saved/Migration/Logs/CalystoDungeonMasterPhase2PIE_20260802_194956.log`
- `Saved/Migration/CalystoDungeonMaster/SeedReplayRerollAdvancePIE58_20260802_200015.json`
- `Saved/Migration/Logs/CalystoDungeonMasterPhase2PIE_20260802_200015.log`
- `Saved/Migration/CalystoDungeonMaster/ProtectedAssets_Phase2ReleaseFinal.json`
- `Saved/Migration/Evidence/CalystoPhase2_ProtectedInvariantVerification_ReleaseFinal.json`
- `Saved/Migration/Logs/CalystoPhase2_Development_BuildCookPackage.log`
- `Saved/Migration/Logs/CalystoPhase2_Development_UFSManifest.txt`
- `Saved/Migration/Logs/CalystoPhase2_Development_PackagedSmoke_KnownSeed.log`
- `Saved/Migration/Logs/CalystoPhase2_Shipping_BuildPackage.log`
- `Saved/Migration/Logs/CalystoPhase2_Shipping_UFSManifest.txt`

The completed PIE receipt, rather than syntax inspection alone, is the evidence for the live Floor 1 through Floor 10 behavior. Native Unreal MCP preflight and Blueprint dirty-state conclusions remain independently `PENDING` as recorded in the ledger.

## Historical boundary

`Docs/Migration/Evidence/Calysto_Dungeon_Master_Phase1_20260802.md` remains historical and was not edited by this pass. Its SHA-256 observed here was `3D285E976E1EF0E4B9B869A469E929108DF1485987543466930B46FE4F824C06`.

Its Floor 1–3 tables, terminal Floor 3 behavior, and seed-plus-floor model describe Phase 1 only. They are not Phase 2 acceptance evidence and must not be silently rewritten.

## Remaining acceptance sequence

1. Re-run native MCP preflight, Blueprint compile, and live schema inspection when native MCP transport is available.
2. Resolve the cooked-runtime `Object Transform` contract with a Calysto-compatible adapter or vendor-authorized asset update; do not patch or resave the protected Calysto graph implicitly.
3. After that blocker is resolved, repeat packaged Floor 1 through Floor 10, replay/reroll, same-seed restart, Shipping productive-door/API, and Shipping `L`-menu-inert validation.
4. Record the requested baseline-relative performance and actor/memory-growth evidence.

Phase 2 is not complete until every required live, build, PIE, cook, package, and invariant gate is `PASS`.
