# Calysto V6 enemy movement and floor travel repair — 2026-09-13

Scope: writable `D:/Projects UE5/NoShellForWinter`, UE 5.8.2. Active gameplay
authority remains V6; this is not a V7 cutover. Source
`D:/Projects UE5/LustAsDeadlySin` was not modified.
Starting HEAD: `5ec929d76546e95249cab4735f96176e3ca7911f`; substantial existing
tracked/untracked work is preserved. No commit is implied.
Final whole-file source snapshot: evidence subdirectory `SourceSnapshot_Final/`,
with seven SHA-256 source identities in `SourceHashes.json`.

Evidence directory: `Saved/Migration/CalystoDungeonDirectorV7/EnemyTravel_20260913/`.

## Findings and changes

- `UserSession.log`: floor 2 failed with `V6_POPULATION_FAILED` (zero Floor
  candidates for two frozen decisions) and, in a separate run,
  `V6_ROOM_MANIFEST_INVALID` (zero rooms, topology seed 475408055, run seed
  3250503565319212091). The identical frozen retry failed again and invoked
  the existing HUB recovery. This was not a wrong floor-door destination.
- `EFCalystoPopulationMaterializerV6.cpp`: retain native Floor output from the
  existing PCG room-point producer as supplemental candidates. Require NavMesh
  projection, ownership against all rooms, deterministic deduplication and the
  existing per-actor collision/spacing checks. Do not omit frozen decisions.
- `EFCalystoDungeonSubsystem.cpp`: up to three bounded recovery attempts.
  Empty topology/exhausted placement on an **unaccepted** floor can advance the
  generation serial while preserving run, floor, Style and committed ecology.
  Accepted replay manifests stay frozen across retries. Other failures still
  fail closed; the terminal safe-HUB fallback is not removed.
- `ProjectCalystoPatrolSubsystem.cpp`: configure base path following while idle
  before checking combat/command state. Previously, an enemy that immediately
  acquired a target could permanently skip the per-pawn multi-NavData Crowd
  workaround. Native ACF perception, target selection and combat remain owners
  of pursuit; no fabricated player-target assignment was added.
- Added `NativeFloorCandidates` regression test (retain floor point, collapse
  duplicate, exclude protected neighbor under rotated/translated dungeon).
  Updated pinned native Automation inventory from 38 to 39.
- `EFCalystoPCGRuntimeGraphV6.cpp`: the exact native `$Index < count * 0.1`
  endpoint filters cast the threshold to integer, rejecting every candidate
  when the candidate count is below ten. Clone only the native endpoint
  subgraph below the already-transient Shape; compose `max(count * 0.1, 1)`
  before both filters. Validate exact source node/edge/operator signatures,
  invalidate copied cooked compilation data and version the runtime graph
  fingerprint. No vendor graph or Style size is edited. Existing native
  cooked/topology signature tests passed with three transient clones.

## Validation

| Gate | State | Evidence |
| --- | --- | --- |
| Editor Development cold build | PASS | `EditorBuild.log`, `EndpointEditorBuild2.log`, `FinalTestsEditorBuild.log`; protected Daz receipt repaired |
| PIE patrol and natural perception/pursuit | OBSERVED | `AfterFirstFix.json` and `FinalMeleeRealDoors.json` / `.log`: both Melee move; Melee 0 selects Player naturally, enters battle and closes to ~101 cm. Target acquisition already occurs during patrol observation, before test encounter staging |
| PIE 1 → 2 → 3, same run | PASS (API diagnostic) | `AfterFirstFix.json`: seed 5738796534536664893, epoch 1, three Ready floors; this first run uses advance API, not real doors |
| Reported failing seed before endpoint fix | FAIL reproduced | `FailedSeedRealDoors.json` / `.log`: actual floor-1 door; Compact floor 2 has zero rooms on original seed and all three recovery seeds |
| Reported failing seed after endpoint fix / real doors | PASS | `EndpointFixFailedSeed.json` / `.log`: floors 1, 2 and 3 Ready in epoch 1; real interaction component selects and overlaps each exact floor door for three samples. Compact floor 2 now has 3 rooms at unchanged seed 475408055, size 19x19x1; no recovery required |
| Second seed / real doors | PASS | `FinalMeleeRealDoors.json` / `.log`: seed 5738796534536664893, floors 1, 2, 3 Ready; both actual floor-door interactions recorded |
| Native tests | PASS | `Saved/Migration/CalystoDungeonDirectorV6/NativeAutomation_EnemyTravel_20260913_Final/StrictSummary.json`: exact 39 tests, zero warnings/errors, no Content mutations |
| Blueprint compile | PASS | `Saved/Migration/CalystoDungeonDirectorV6/BlueprintCompile_EnemyTravel_20260913_Final/StrictSummary.json`: exact 30 Blueprints, no saves/protected package changes |
| Visual pursuit | PENDING | Inspected `FinalMeleeRealDoors_Encounter.png`; essentially black, not valid visual evidence |
| Game build | FAIL, pre-existing blocker | `GameBuild.log`: `ProjectCalystoDormantController.cpp:32`, `UClass::ClassGeneratedBy` is editor-only. Changed runtime sources compile, but target cannot link. This separate V7 identity-audit code was not weakened or changed |
| Cook / packaged validation | PENDING | Blocked by Game build; no packaged acceptance inferred from Editor/PIE |
| Protected invariant comparison | PASS unchanged vs task start | `InvariantsBefore.json` / `InvariantsAfter.json`: all sets, authoritative asset rows and all 145 historical mismatches identical; historical-baseline validation itself remains FAIL |
| Protected Calysto/map comparison | PASS unchanged | All 13 SHA-256 identities from `ProtectedBefore.json` rechecked after the final test gates; zero changes |
| Editor handoff | PASS | Protected launcher reopened target; native `ReadEditorContext` confirms UE 5.8.2, `/Game/_Game/Hub/HUB.HUB`, PIE off, zero dirty map/content packages |

The diagnostic harness first required corrections for startup reentrancy and
waiting for the actual PIE session preload. The first completed pursuit probe
also emitted a handled NavigationSystem CDO ensure from the **test's** Python
projection call. This persists through the instance-bound Python call in the
final Melee probe; it is not counted as an error-free gameplay gate. The
natural target acquisition/patrol samples preceding that call remain useful
observations. Projection binding and visual capture require separate QA.
These probe issues are not counted as game passes or as game root causes.
The first native test run also exposed a self-aliasing `TArray::Add` in the new
test fixture (not gameplay). Corrected by copying the point before appending;
the aborted run is retained as `NativeAutomation_EnemyTravel_20260913_Endpoint1`.
The failing-seed run has no Melee enemy on floor 1, so its scenery capture is
not visual proof of pursuit. Its logs retain the separate pre-existing
`LogTemp: Error: Can't Start the quest` on map startup; it is not a clean whole-
game acceptance run and is not presented as one.
