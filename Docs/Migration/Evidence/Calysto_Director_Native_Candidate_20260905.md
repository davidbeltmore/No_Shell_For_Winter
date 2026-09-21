# Director native candidate verification — 2026-09-05

V7 completion remains **PENDING**. The native fixture has no population, optional
architecture, lighting or decals. It cannot establish the full candidate gate.
All paths below belong to `D:/Projects UE5/NoShellForWinter`, UE 5.8.

## Authoring staging

`Tools/Migration/Import-CalystoDungeonDirector58.py` now supports `main(stage_only=True)`.
Staging creates a transient native asset, compares every mapped authored value
with the complete native export, and returns without creating or saving a package.
It writes `Saved/Migration/CalystoDungeonDirectorV7/ImportDirectorStaged.json`;
the actual importer receipt remains separate.

The 11:58 UTC invocation passed native authoring and complete mapped-value
roundtrip in 0.422 seconds. Saved packages and dirty packages were empty; immutable
V6 package SHA-256 remained
`F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323`.
That receipt still contained 73 PENDING source leaves. Empty-catalog reporting
was subsequently corrected; rerun staging before quoting the final gap count.
The master asset has not yet been saved and migration is not complete.

## Native build and short tests

The cold Editor build with shared dependency loading, canonical structural mesh
rules and the pure content reservation planner passed in 64.20 seconds:
`Saved/Migration/CalystoDungeonDirectorV7/BuildSharedMeshAndReservations_20260905.log`.
The protected build restored the enabled Daz receipt.

`Native_20260905_SharedMesh64` ran the exact 64-name inventory and exited after
68.90 seconds. Its overall gate is FAIL: one new test incorrectly checked the
flat-Floor counter for Ramp instances. The other 63 tests reported Success with
zero test errors or warnings. The supervisor correctly retained the nonzero
Editor exit (255), returned failure, and did not stall after report generation.

Within that run, the new seven-test content planner suite passed. It checks
feasible conditional amounts, capacity-preserving choices, whole-manifest failure,
shared Style/Theme limits, Winter, spacing, per-container slots, existing usage,
finite input bounds and transformed placement envelopes. The 100,000-trial
probability test passed in 0.365 seconds. These are in-memory tests and are not
realized gameplay evidence.

The corrected test checks two total Ramp instances and zero flat Floor instances.
Rebuild passed in 7.05 seconds (`BuildSharedMeshFixture_20260905.log`). The fresh
live Editor then passed all five StructuralNavigation tests with zero test
errors or warnings (`LiveNative_20260905_SharedMesh/StructuralTests/index.json`).
That live process's exit and actual traversal are separate pending gates.

## Protected baseline

`CalystoProtectedAfterSelector.json` matches all 13 task-start Calysto/map/door
hashes. `ProjectProtectedAfterSelector.json` retains exactly the same 145
historical discrepancy records as the task-start snapshot, including the four
authoritative asset discrepancies. This is unchanged task-local state, not a
historical baseline PASS or a rebaseline. Frederick's exact authoritative
invariant remains unresolved in prior migration evidence.

The skill and workflow snapshot is commit `973981f`. The direct authoring schema,
complete immutable compilation and production probability core are checkpointed
in `0d9910e`. Neither checkpoint switches runtime authority or completes gameplay.
Integration changes remain in the preserved dirty worktree.

Staging was repeated after the empty-array report correction: native roundtrip
passed again in 0.422 seconds, with no saves and unchanged source hash. There are
68 actual PENDING source leaves; all five falsely unreported empty arrays are now
explicit EMPTY_SOURCE records.

## Native zero-height wall investigation

The subsequent real-door runs rejected finite zero-Z wall transforms. The bounded
read-only observer captured them before rollback in
`Structure_20260905_WallTransform2/structure.json` and
`Structure_20260905_WallHeights/structure.json`. The first successful capture took
0.235 seconds of aggregate observer work across approximately three seconds.

The latter capture records actual runtime Wall Height = Initial Wall Height =
300 cm. The native upper-extension graph computes
`Scale.Z = Wall Height / Initial Wall Height - 1`, so zero is an intentional output
of this branch at equal heights. Native component tags do not preserve per-point
NoSocket provenance; base and upper walls merge into one ISM component. A wall
role or mesh path alone is therefore insufficient to permit a zero transform.

The repair under construction captures native upper-extension point provenance
through a transient diagnostic output and matches mesh/transform multiplicities
to exact owned instance indices. Only proven zero-height upper extensions may be
recorded without contributing structural volume. Required Floor/Wall/Roof counts
must come from nondegenerate geometry. Build and real traversal of that repair
remain PENDING.

`LiveNative_20260905_SharedMesh/Exit.json` records clean Editor exit at 12:31 UTC.
Its real-door traversal receipts remain FAIL; no accepted floor was manufactured.

The pure reservation planner is checkpointed in `5ec929d`. It remains separate
from world materialization until the native traversal gate passes.

`BuildWallProvenance_20260905.log` passed in 17.16 seconds. The next live Editor
executed the exact 66-test inventory; 64 tests succeeded without warnings, and
two new graph-contract tests failed. The report is
`LiveNative_20260905_WallProvenance/NativeTests/index.json`. Native automation
execution totaled 4.907 seconds. The failure was localized to the named reroute's
hidden `InvisiblePin` output, which the first check incorrectly called `Out`.
The test gate remains FAIL until the exact pin correction is compiled and tested.

The correction then passed the protected build in 14.50 seconds
(`BuildWallHiddenPin_20260905.log`). All six affected native integration tests
passed with zero test errors/warnings in 1.659 seconds
(`LiveNative_20260905_HiddenPin/NativeTests/index.json`). The prior 66-test
process exited cleanly (`LiveNative_20260905_WallProvenance/Exit.json`).

The next actual-door receipt, `Traversal_20260905_WallProvenance/traversal.json`,
remains FAIL. It stopped after 21.187 seconds, with four attempts under one
request (16.464 seconds at exhaustion), unchanged Style/run/floor, no accepted
floor and no dirty packages. Each attempt rejected an unmatched native
zero-height wall instance. Capture: `Structure_20260905_WallProvenance/structure.json`.
This proves that the new graph contract is accepted; it does not prove the
captured points agree with realized instances. The next diagnostic reports
proposal count and exact actual/nearest transforms without weakening matching.

The exhaustion path exposed a separate Slate error: UIOnly focused the
non-focusable enclosing SBorder. The project-owned failure overlay now targets
its actual Return to HUB button. Runtime verification of that fix is pending.

`BuildWallComparison_20260905.log` passed in 15.77 seconds. The next real-door
run (`Traversal_20260905_WallComparison`) hit the shared 30-second deadline and
remains FAIL. Its mismatch diagnostics show identical position and scale but
identity rotation from the public instance getter versus 90/180 degrees in the
native point output. UE 5.8 `InstancedStaticMesh.cpp:4218` reconstructs FTransform
from the stored matrix; the singular zero-Z matrix loses its quaternion during
decomposition. The comparison now uses all three actual stored matrix axes and
translation in component world space, preserving orientation checks and exact
multiset ownership. Focused tests cover this native behavior and wrong rotation.
`BuildWallMatrix_20260905.log` passed in 11.63 seconds; the new tests and traversal
are pending. The comparison process exited cleanly and no UIOnly focus error
occurred on its real failure dialog. No successful floor was accepted.

The read-only Python observer's first PCG-data extension hit an unavailable
`BreakTaggedData` binding; that observer receipt is FAIL and is not used to
infer point data. It now uses the exported typed collection API instead. Its
runtime binding and bounded capture must be verified independently.

The corrected matrix comparison passed all six affected native tests without
errors/warnings in 1.658 seconds (`LiveNative_20260905_WallMatrix/NativeTests`).
Actual generation then accepted the structural instance provenance and reached
the capsule checks. The typed PCG observer successfully recorded actual outputs
in `Structure_20260905_WallMatrix/structure.json`; its 3.529-second observation
overhead is diagnostic cost, so this capture is not a performance baseline.

`BuildCapsuleBlocker_20260905.log` passed in 16.74 seconds. Exact failure-query
diagnostics preserve the clearance decision and identify its blocking component.
The cold run `Traversal_20260905_CapsuleBlocker` failed shared resource preparation
at the 30-second deadline before native generation. This remains a cold loading
failure, not a spatial success. The subsequent correctly armed warm run
`Traversal_20260905_CapsuleBlockerWarm/traversal.json` stopped after 10.813 seconds:
all four attempts had blocking floor at Z=0 and an End-approach capsule obstruction
from the owned `EFCalystoFloorDoor.StaticMesh`; Start was not the blocker.

Live native bounds for `SM_SquaredArchedWoodenDoors` are approximately
(-2.732,-14.033,0) to (115.627,14.033,205.431) cm. The legacy appearance transform
(0,60,-110), yaw -90, buries half the mesh and crosses the approach capsule.
The V7 appearance setter now centers the selected mesh horizontally, keeps its
authored size, aligns it upright, and places its base on the native End marker.
Collision remains enabled. A native physics regression checks the real player
capsule and explicitly reproduces the old obstruction before reapplying the fix.
The preceding live Editor exited zero at 14:01 UTC with no dirty packages.

The first combined door/materializer build failed on a class/struct forward
declaration mismatch; incremental GC also diagnosed a raw retained actor pointer.
Both were corrected (`struct FStreamableHandle`, `TObjectPtr<AActor>`).
`BuildDoorMaterializerFix_20260905.log` passed in 11.13 seconds with Daz receipt
repair. The three new materializer tests and door test bring the reviewed native
inventory to 70. Their result and real traversal remain PENDING at this entry.
