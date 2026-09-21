# Calysto dungeon enemy patrol — 2026-09-12

Status: **Implemented and functionally verified in UE 5.8 PIE. Visual review,
actual cook, and packaged validation remain PENDING.**

Writable target: `D:/Projects UE5/NoShellForWinter`. The read-only source
project was not opened or modified. No Marketplace, Engine, ACF Ultimate,
DazToUnreal, map, or Blueprint asset was changed.

## Diagnosis

`/Game/Procedural/Maps/DungeonGeneration` already contains runtime
`NavMeshBoundsVolume` and `RecastNavMesh` actors. The patrol failure was not a
missing Nav Mesh Bounds Volume.

The generated ACF controllers use `UCrowdFollowingComponent`, while the active
dungeon has more than one Recast NavData agent. In this combination Crowd
rejects the route as `InvalidPath`; `AACFAIController::TryGoToNextWaypoint()`
only updates Blackboard patrol data and does not issue a movement request.

## Target-owned correction

`UProjectCalystoPatrolSubsystem` is a runtime adapter scoped to authority-side,
transient actors tagged both `EF.Calysto.Population` and
`EF.Calysto.Population.Category.Enemy` in `DungeonGeneration` only.

- It preserves ACF patrol destination selection and combat/command priority.
- When an affected transient controller is idle, it disables Crowd simulation
  for that controller only and submits a normal full-path `MoveToLocation`.
- It retries on valid projected navigation points and has bounded stall
  recovery.
- `UProjectCalystoPatrolSettings` exposes the behavior, enabled by default;
  `bDisableCrowdSimulationForDungeonPatrol` is the narrow compatibility switch.

The trade-off is deliberate: transient Calysto dungeon enemies do not use Crowd
avoidance while this adapter is active. Nothing global is changed in ACFU.

## Evidence

| Gate | Result | Evidence |
| --- | --- | --- |
| Protected Editor build of the patrol adapter | PASS | Build completed before the runtime validation; the final Game build below independently compiled `ProjectCalystoPatrolSettings.cpp` and `ProjectCalystoPatrolSubsystem.cpp`. |
| Cohort Blueprint compile | PASS, 30/30 | `Saved/Migration/CalystoDungeonDirectorV6/BlueprintCompile_PatrolFix_20260912_1615/StrictSummary.json` |
| Deterministic patrol PIE, seed `5738796534536664893` | PASS | `Saved/Migration/CalystoDungeonDirectorV6/PatrolPIE_20260912_164119/PatrolPIE.json` |
| Navigation | PASS | Same PIE receipt: NavMesh bounds and Recast were present. |
| Patrol movement | PASS | Same PIE receipt: the same 3 actors with active patrol loops moved 1110.86 cm, 1367.69 cm, and 827.83 cm; peak speeds were 350, 180, and 180 cm/s. |
| Runtime logs | PASS | `Saved/Logs/NoShellForWinter.log`: `CALYSTO_V6_PATROL_CROWD_DISABLED`, `CALYSTO_V6_PATROL_MOVE_REQUESTED`, and `CALYSTO_V6_PATROL_STARTED`. |
| Daz receipt | PASS | Checked before and after each protected PIE launch. |
| Package-script static gate | PASS | `Tools/Migration/Test-CalystoDungeonDirectorV6PackageValidationStatic58.ps1` |
| Visual review | PENDING | The two automated screenshots in `PatrolPIE_20260912_164119` were produced but frame modular geometry rather than the selected enemy. They are retained as failed visual evidence, not accepted as visual QA. |
| Actual cook/package/smoke | PENDING / BLOCKED | A fresh Game Development build is required. It fails in pre-existing untracked `ProjectCalystoDormantController.cpp:32`: `UClass::ClassGeneratedBy` is not available in UE 5.8. This file is outside the patrol change and was not modified. |

The final PIE receipt's GameplayTag Python reflection field remains `PENDING`;
UE 5.8 renders that tag as a struct shell. It is not used as the movement
authority. The C++ runtime markers and actual character displacement are the
authoritative evidence.

The final runner additionally rejects output paths outside `Saved/`, correlates
movement with the same actor that has an active patrol loop, requires no new
dirty packages, and closes PIE/editor even if report serialization fails.

## Required follow-up before release acceptance

1. Repair or remove the unrelated `ProjectCalystoDormantController.cpp` UE 5.8
   compile incompatibility with its owner’s approval.
2. Run a fresh Game Development build, cook/package, strict package validation,
   and packaged smoke from the current tree.
3. Capture a human-readable in-dungeon view of an enemy patrolling and mark
   visual QA PASS only after reviewing it.
