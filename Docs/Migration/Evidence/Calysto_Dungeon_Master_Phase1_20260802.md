# Calysto Dungeon Master — Phase 1 evidence

Date: 2026-08-02  
Target: `D:/Projects UE5/NoShellForWinter` (UE 5.8)  
Source: `D:/Projects UE5/LustAsDeadlySin` (read-only)  
Master plugin: `Plugins/EFProcedural` 1.1.0

## Outcome

Phase 1 establishes a project-owned, DataTable-driven control boundary around Calysto without editing or saving `BP_MassiveDungeon`, its PCG graphs, or its source DataAssets. All dungeon policy, transient adaptation, floor state, readiness, and the generated ACF endpoint door live inside `EFProcedural`. `EFProjectSystems` contains only the thin existing `L`-menu command surface.

The final tested path is:

`Floor 1 (30x30) -> generated End_Point door -> loading -> Floor 2 (28x28) -> generated End_Point door -> loading -> Floor 3 (26x26)`

Floor 3 rejects Floor 4. Enemy difficulty remains tier 1 on every floor for Phase 1.

## Implemented boundary

- Typed floor, spawner, theme, debug, and snapshot contracts in `EFProceduralRuntime`.
- Persistent `UEFCalystoDungeonSubsystem` with deterministic run seed, Floor 1-3 state, URL travel options, external-entry reset to Floor 1, debug overrides, and travel-failure recovery.
- Fail-closed `EFCalystoPCGAdapter` in `EFProceduralPCGRuntime`.
- Transient clones of `DA_DungeonMesh`, `DA_DemoSpawner`, and `DA_RoomTheme`; no vendor asset is modified or saved.
- Exact graph requirement: `/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster`.
- Exactly one dungeon actor, exactly one non-editor PCG component, `GenerateOnDemand`, deterministic seed, and one explicit generation request.
- Project-owned `AEFCalystoFloorDoor` in `EFProceduralACFURuntime`, injected only into the transient `endBlueprint` field.
- Door remains disabled until PCG completion and runtime navigation readiness. Readiness uses a temporary 0.25-second timer, a 30-second fail-closed timeout, and no permanent tick.
- Normal UE travel failures clear both subsystem and door latches. A request which hangs without either a destination world or `OnTravelFailure` still needs the Phase 2 watchdog.
- Duplicate dungeon actors, unknown reflected schema, wrong graph/trigger, bad table topology, preload failure, or navigation failure abort controlled generation instead of falling back to legacy randomization.

The structural denylist includes floor, wall, roof, ramp, door-frame, material, tile-size, offset, collision, grammar, room-data, and structural mesh fields.

## Authored policy assets

All policy assets are under `/Game/_Game/Data/CalystoDungeon` and are included by `DirectoriesToAlwaysCook`:

- `DT_CalystoFloorProfiles`
- `DT_CalystoSpawnerWeights`
- `DT_CalystoThemeWeights`

Profiles:

| Floor | Seed | Size | Density | Side path | Room min/max | Difficulty |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 43 | 30x30x1 | 0.30 | 0.50 | 4/8 | 1 |
| 2 | 44 | 28x28x1 | 0.30 | 0.45 | 4/8 | 1 |
| 3 | 45 | 26x26x1 | 0.30 | 0.55 | 4/8 | 1 |

Runtime clamps remain conservative: X/Y 25-30 and never above the live 30x30 baseline, Z=1, density 0.20-0.50, side-path chance 0.30-0.70, spawner/theme weights 1-5, and room size 4-8. Phase 1 uses moderate spawner and Forge/Shrine theme-weight variation only.

## Debug surface

`L > Dungeon Harness` provides:

- live status;
- next floor and regenerate;
- select Floor 1/2/3;
- size 25/28/30;
- density 0.25/0.30/0.40;
- side paths 0.40/0.50/0.60;
- clear overrides.

Debug travel and overrides compile to no-ops in Shipping; the real generated floor door remains a production path. Runtime values are snapshot-driven and do not use per-frame polling.

## Final gates

| Gate | Status | Evidence |
|---|---|---|
| UE 5.8 cold Editor build and Daz receipt repair | PASS | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; final `Result: Succeeded`; Daz receipt guard PASS |
| Native policy/table/debug automation | PASS, 5/5 | `Saved/Migration/Automation/CalystoDungeonMaster_20260802_181525/index.json`; `Saved/Migration/Logs/CalystoDungeonMaster_20260802_181525.log` |
| Real ACF selection and interaction, F1 -> F2 -> F3 | PASS, 14/14 checks | `Saved/Migration/CalystoDungeonMaster/DoorFloorInteractionPIE58_20260802_181355.json`; paired log `Saved/Migration/Logs/CalystoDungeonMasterDoorPIE_20260802_181355.log` |
| Door readiness before ACF selection | PASS | All three floor samples report `door_runtime_enabled=true`; no early interaction |
| Floor 3 cap / Floor 4 rejection | PASS | Final ACF JSON, `floor_3_door_disabled=true`, `floor_4_rejected=true` |
| Exactly one dungeon and one door per floor | PASS | Final ACF JSON |
| PCG cancellation, timeout, Calysto/EFProcedural error, Blueprint error, ensure, fatal | PASS, zero scoped occurrences | Final ACF log |
| `BP_MassiveDungeon` compile and live dirty state | PASS | MCP compile with warnings-as-errors; `dirty=false` |
| `BP_MassiveDungeon` disk invariant | PASS | SHA-256 `1B3BE0902C810A1F1E665CB3815AB8C9FEBD22921D03C722C6E314E3A3456BEC`, 407681 bytes |
| 13 protected Calysto/map/legacy-door packages | PASS, 13/13 byte-identical | `Saved/Migration/CalystoDungeonMaster/ProtectedAssets_Before.json`; final `Test-CalystoProtectedAssets.ps1`, zero mismatches |
| ACFU and DazToUnreal plugin invariants | PASS | `Saved/Migration/CalystoDungeonMaster/ProtectedInvariants_Final.json`, 5043/5043 and 213/213 |
| Broader historical invariant set | PASS_NO_NEW_DELTA, helper remains FAIL | Before and final both contain the same 74 historical mismatches; canonical comparison is identical. Target Daz set already had 70 mismatches; Player/Female/Multiple/Male already differed from the Phase 0 manifest. |
| Frederick invariant | PENDING_RUNTIME_IDENTITY | The authoritative baseline still has no resolved Frederick package/hash. |
| Source-project global helper | PENDING_GLOBAL_BASELINE; no task write observed | HEAD, tracked modified-file hashes and LFS match. Status differs only by two TattooShop files timestamped 13:46-13:47, before this task's 16:45 preflight. `Saved/Migration/CalystoDungeonMaster/SourceReadOnly_Final.json` records the overall helper failure. |
| Functional `L` menu visual QA | PASS | `Saved/Migration/CalystoDungeonMaster/PIE_Floor1_DebugMenu_L.png`, `PIE_Floor1_DungeonHarness.png`, `PIE_Floor3_DebugMenu_L.png` |
| Loading-screen transition visual | PASS | `Saved/Migration/CalystoDungeonMaster/PIE_Floor1_to_Floor2_Loading.png` |
| Cook and `_Game` table inclusion | PASS | `Saved/Migration/Logs/CalystoDungeonCook_20260802_173339.log`: 6647 packages, result 0, 0 errors, 44 warnings; referenced set contains all three tables and the dungeon map |
| Packaged Development/Shipping executable and smoke | PENDING | Not run inside the two-hour Phase 1 budget; cook is not treated as packaging |
| Shipping `L` no-op plus production door smoke | PENDING | Static guards exist; packaged smoke not run |
| Comparative generation-time/actor/hitch benchmark | PENDING | Runtime is bounded and tick-free, but no controlled before/after benchmark was completed |
| Travel accepted but neither destination nor failure arrives | PENDING_PHASE2 | Normal travel failure is recovered; serial-based 30-second travel watchdog remains to be added/tested |

## Visual and log qualifications

- The existing root debug-menu text has cosmetic overlap in the screenshots. Dungeon Harness navigation and commands work; styling polish is pending.
- The final three-floor log contains three `LogTemp: Error: Can't Start the quest` messages, one per floor. They are outside the Calysto/EFProcedural categories and were not investigated here; attribution remains pending. The final scoped Calysto/EFProcedural/Blueprint/ensure/fatal counts are zero.
- An early automation attempt preloaded `BP_MassiveDungeon` before the map and crashed the editor. UE produced `Saved/Autosaves/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon_Auto1.uasset` and `Saved/Migration/CalystoDungeonMaster/RestorePackagesPrompt.png`. The autosave was not restored, and the real protected package remains byte-identical and clean.
- Later failed gates exposed readiness-driver races and an invalid inactive-component assumption. Both were removed/corrected before the final build, 14/14 ACF run, and 5/5 automation run above. Failed intermediate artifacts are diagnostic only and do not supersede the final timestamped evidence.

## Cross-agent entry points

- Codex/OpenAI skill: `.agents/skills/calysto-dungeon-master/SKILL.md`
- Claude wrapper: `.claude/skills/calysto-dungeon-master/SKILL.md`
- Protected-hash helper: `.agents/skills/calysto-dungeon-master/scripts/Test-CalystoProtectedAssets.ps1`
- Durable ACF PIE runner: `Tools/Migration/Run-CalystoDungeonMasterPIE58.ps1`

Both skill directories pass `skill-creator/scripts/quick_validate.py`. `AGENTS.md` now routes Calysto dungeon tasks to `$calysto-dungeon-master` in future chats.

## Phase 2 starting point

Phase 2 can concentrate on adversarial tuning and gameplay rather than rediscovery: difficulty progression, richer DataTable presets, run-seed/new-run API decisions, serial travel watchdog, packaged Shipping validation, performance baselines, and broader theme/spawner experiments. Structural generation remains outside the writable surface unless a later instruction explicitly changes that contract.
