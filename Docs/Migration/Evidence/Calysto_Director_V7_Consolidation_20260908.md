# Calysto Director V7 consolidation — 2026-09-08

Status: **Scoped build and native tests verified; native traversal and V7 release PENDING.**

Target: `D:/Projects UE5/NoShellForWinter`, Unreal Engine 5.8. Historical source
project remains read-only. Initial HEAD: `5ec929d76546e95249cab4735f96176e3ca7911f`.
Evidence root: `Saved/Migration/CalystoDungeonDirectorV7/Resume_20260908/`.

## Preserved state

`Snapshot.json`, `SourceBefore.zip`, `git-status.txt` and `git-diff.patch` preserve
180 dirty/untracked project source, configuration and tooling files plus the
existing tracked diff. This is not a replacement for the unrelated dirty worktree.
No protected asset was saved or restored.

`CalystoProtectedBefore.json` matches all 13 Calysto assets in the September 5
receipt. `ProjectProtectedBefore.json` and `ProjectProtectedAfterConsolidation.json`
both contain the same 145 historical discrepancies and identical authoritative
asset records. The historical comparison remains FAIL; it was not rebaselined.
`TaskProtectedBefore.json` preserves 5,517 current file hashes, supplementing
historical length-mismatch records with actual hashes. Frederick's exact runtime
identity remains PENDING and is not included in a complete-coverage claim.

## Changes and results

- The initial protected Editor build failed on new dormant-controller C++ types:
  a const default object passed to `GetDefaultSubobjects`, raw-pointer deduction
  from `TObjectPtr` component templates, and narrowing a tick bitfield to bool.
  Corrected only those type uses. `BuildFixReceipt.json` records exit 0; the build
  took approximately 25 seconds and repaired the Daz receipt.
- The reviewed inventory contains 77 native tests: the previous 70 plus three
  dormant-controller and four social-staging tests. The full run reported all
  assertions successful, 75 clean tests and two social fixtures with world-context
  warnings. The actual process exited 0, but `Native_Consolidation_20260908/StrictSummary.json`
  correctly reports FAIL. Do not rewrite it as a clean suite PASS.
- Registered and released the exact social fixture world context. The four
  social tests then passed without warnings in
  `Native_SocialContextFix_20260908/StrictSummary.json`, process exit 0,
  48.43 seconds including startup. The earlier materializer warning is absent
  from its test in the 77-test run.
- `BuildSocialAndSeedReceipt.json` records the subsequent protected build,
  exit 0, approximately 11 seconds. Development candidate/parity launches now
  accept `-CalystoDirectorRunSeed=<int64>` through the real door's existing
  `RequestStartNewRun` path. Shipping ignores this hook. Exact known-seed
  gameplay verification remains PENDING.
- The traversal harness now records retained native diagnostics on phase/code
  transitions, including terminal failure, and requires actual native
  `GenerateLocal` counts instead of transaction authorization alone. Its seven
  receipt-validator tests passed in 0.005 seconds; they are not PIE evidence.

## Live operator status and next action

Owned Editor PID 25664 launched the target HUB using the protected script with
Daz enabled and seed 2959332854660340481. Native MCP discovery succeeded after
startup. No PIE was started. The Windows Computer Use helper was unavailable
(`native pipe` missing), so the existing UI-console arming procedure could not
execute. The Editor closed normally with an observed launcher exit 0; see
`LiveNative/Exit.json`.

That operator blocker was subsequently resolved through the four project-owned
`UEFCalystoDirectorToolset` MCP operations: `ReadEditorContext`, `ArmTraversal`,
`ReadDirectorDiagnostics` and `RequestEditorShutdown`. `NativeFixToolset0.txt`
records the discovered schemas; `McpNativeContext.json` confirms target UE
5.8.2/HUB with stopped PIE and no dirty packages. `McpArmNativeCold.json` and
`McpArmNativeWarm.json` each record a fresh ARMED receipt before external PIE.
`McpLiveNativeWarmObservation.json` reads the actual PIE GameInstance and preserves
its `diagnostics_json` string, including exact int64 identities. No arbitrary
Python MCP tool or functioning Computer Use pipe is needed for this fixed task.

The real-door cold run failed when shared preparation reached 30.021 seconds;
the floor's terminal elapsed time was 25.130 seconds and native generation had
not started. Warm shared preparation took 0.227 seconds; observed native attempts
each recorded an actual GenerateLocal call, but wall-light output rejection and
the floor deadline still prevented acceptance. Both traversal receipts are FAIL.
`LiveMcpNative/Editor.log` records the shutdown tool at 10:14:46 UTC, deferred
`CloseEditor` at 10:14:48, and final exit; `LiveMcpNative/Exit.json` confirms the
owned launch completed with exit 0 at 10:14:53. This is scoped operator/exit proof,
not a clean-log, traversal or final-tree release PASS.

`DeriveNativeReplaySeeds.py` maps V7 native-parity run `1219803037753221267` to
first topology `1779679224`, confirmed in actual `LightObservationSeed177` cold
and warm traversal diagnostics. Those runs also FAIL. Run `-3003287693235273058`
maps mathematically to topology `1190737158`; live confirmation remains PENDING.
These mappings require the parity Style, Floor 1, reroll 0 and attempt 0.

Next, the parent completes its protected build and focused lighting/lifecycle
tests, then arms a fresh cold run for topology `1779679224` and immediately starts
PIE. Preserve the 30-second floor deadline and 150-second harness budget while
diagnosing the first failed native/navigation postcondition. Runtime outcome of
the lighting/shared-load corrections remains PENDING. The parent retains sole
ownership of Editor, build, native tests, navigation work and process cleanup.

Skill workflow and the frozen 31-criterion final acceptance ledger are under
`.agents/skills/calysto-dungeon-master/` and `Calysto_Director_V7_Acceptance.*`.
The durable ledger retains all original IDs and prior checkpoint: **0 / 31 whole
criteria verified, zero final V7 floors accepted**. No final acceptance percentage
is inferred from native test counts, operator success or mathematical seeds.

## Continued checkpoint at 10:37 UTC

Work continues automatically at milestone 2. The protected build
`Resume_20260908/BuildEmptyLightsAndLoadReceipt.json` passed. The native runner now
selects the exact reviewed test names with UE's start/end anchors in a single
process; its former partial-namespace invocation selected no tests and remains
FAIL in `Native_LightingLifecycle_20260908`. The corrected invocation passed all
four lighting/lifecycle tests, zero test warnings, process exit 0, 46.66 seconds
including startup, in `Native_LightingLifecycleExact_20260908/StrictSummary.json`.
The production lighting resolver's 100,000-trial test took 0.066 seconds.

Shared preparation now retains its lease when joining a floor and uses that
floor's unchanged 30-second deadline. Critical load priority is 100. The cold
`Traversal_20260908_EmptyLightsLoadFixSeed177` progressed through loaded resources
and into actual generation before its deadline; it remains FAIL.

The explicitly empty native parity lighting catalog produces native surface
opportunities with default zero transforms and no Blueprint. These are now
distinguished from selected light requests. Any unexpected Blueprint or actual
actor reference still rejects the whole attempt. No requested light was dropped.

Live Editor performance settings showed background throttling enabled. A bounded
comparison temporarily disabled it in memory, then restored both original
performance booleans through MCP; see `PerformanceSettingsBefore.json`,
`PerformanceSettingsDuring.json` and `PerformanceSettingsRestored.json` in the
resume evidence root. No settings or Content asset was saved. With throttling
disabled, `Traversal_20260908_UnthrottledSeed177` completed native output in 0.963
seconds and observed 165 Floor, 120 Wall and 165 Roof instances. Floor contact
and capsule clearance reached the next postcondition. The first observed failure
is now `NAVIGATION_BOUNDS_REGISTRATION_FAILED`, before relevant tiles/route can
be accepted. This is progress in diagnosis, not an accepted floor or a V6
performance improvement claim. The owned Editor closed normally; see
`LiveEmptyLightsAndLoad/Exit.json`.

Next: compile the scoped MCP performance guard and detailed requested/actual
navigation bounds observation; execute the fractional-box regression and the
same real-door topology. Keep all acceptance checks intact. The master asset,
complete migration, full content and release gates remain PENDING. The importer
now has tested pure conversion for 16 of the 68 previously unresolved leaves;
52 still lack a complete conversion decision, and actual Editor migration is
still PENDING.

## Continued checkpoint at 10:59 UTC

The exact live bounds failure was a floating-point enclosure loss of
`5.6843418860808015e-14` cm on maximum X, after real floor contact and capsule
clearance passed. Evidence: `Traversal_20260908_BoundsCoordinatesSeed177`.
The registered bounds volume now encloses its requested geometry on an outward
centimetre grid, with less than one centimetre added per face; the original
strict enclosure postcondition remains intact. Cleanup retains the registration
identity even when applying geometry fails.

The fractional-coordinate regression first failed in
`Native_BoundsRegression_20260908` (retained RED evidence). After the fix,
`Native_RoundedBounds_20260908/StrictSummary.json` records all three focused tests
PASS, zero warnings and process exit 0 in 46.464 seconds including startup.
The other two tests cover scoped Editor performance restoration and strict
rejection of unexpected wall lights in the empty parity catalog.
`Resume_20260908/BuildRoundedBoundsReceipt.json` records the protected build.

The bounded MCP traversal performance scope restores the original background
throttle state after PIE without saving configuration. Its previous live run
restored `true` on EndPIE and observed mean Editor cadence 56.46 FPS; this is
operator evidence, not packaged gameplay performance. The owned
`LiveBoundsObservation` process closed normally with exit 0. Historical protected
drift remains unchanged in `ProtectedCheckpointComparison.json`: 145 existing
differences, no rebaseline, Calysto task hashes unchanged; Frederick is PENDING.

The recoverable source checkpoint is
`Resume_20260908/Checkpoint_1040/Snapshot.json` and `Source.zip` (captured 10:42 UTC).
The next owned Editor is `Resume_20260908/LiveRoundedBounds`, with topology replay
run `1219803037753221267`. Next: arm a fresh native-parity receipt and verify
registration, relevant tiles and the complete Start-End route. No V7 floor is
accepted yet; the final-scope ledger remains 0/31 verified. Continue directly
through the remaining milestones after native traversal succeeds.

## Live bounds verification and completion-event correction at 11:14 UTC

`Traversal_20260908_RoundedBoundsSeed177` confirmed the real runtime bounds fix:
165 Floor / 120 Wall / 165 Roof instances, geometry-derived bounds registered,
54 active Player tiles with radius 50 cm / height 176 cm, Start and End projected.
Its single actual root generation took 0.913 seconds. The floor still FAILed the
unchanged shared deadline: dependencies consumed 27.580 seconds and the observer
remained `NAVIGATION_UPDATE_PENDING`. Cleanup verified; no floor/run committed.
The scoped Editor setting restored on EndPIE; `LiveRoundedBounds/Exit.json`
records actual exit 0. This is no accepted-floor or cold-performance PASS.

UE 5.8 source establishes the next defect: `RecastNavMesh.cpp:4479` broadcasts
`OnNavMeshUpdate` when its internal nav object changes, called by
`PImplRecastNavMesh.cpp:1236`. Tile completion instead reaches
`UNavigationSystemV1::OnNavigationGenerationFinishedDelegate` through
`ARecastNavMesh::OnNavMeshGenerationFinished`. The existing owned bounds volume
now binds that exact dynamic completion event, filters the relevant nav data,
records a revision/time and unsubscribes on cancellation/destruction. No new
generator, expanding search or synchronous rebuild was added.

`BuildNavigationCompletionEnabledReceipt.json` records the protected build PASS.
The earlier `BuildNavigationCompletionReceipt.json` is a shell-policy failure
before build execution and is retained. `Native_NavigationCompletion_20260908`
FAIL exposed a fixture error: nav data self-destructed in its isolated game world
without a registered navigation system, and actor destruction lacked a world
context. The fixture correction registers those owners before spawning data;
its rerun and the live completion-event result remain PENDING.

The traversal harness additionally has 10 passing pure checks for its bounded
entry walk, SHA256 `1A38422E6BEBCB2A285E51704531E65447FF8D68244E5D41CE4EDDEF8E36BED0`.
Live walking and full-route walking remain PENDING. Next: consolidate the scoped
critical-loading budget, compile once, run the affected native tests, then launch
and exercise the real-door trilogy again. The root continues ownership of all
Editor, build and asset operations; one auxiliary owns the isolated load scope.

## Checkpoint through 11:48 UTC: native navigation and physical passage

`Native_NavigationFinal_20260908/StrictSummary.json` passed all six structural
navigation tests, zero warnings, actual process exit 0, 46.416 seconds including
startup. The isolated callback test uses UE's scoped script execution guard:
`AActor::ProcessEvent` suppresses native dynamic callbacks before fixture actors
initialize. It also covers relevant data arriving after the subscription and
before the next observer poll. The bounds owner resolves the explicit query
agent/origin inside completion, preserving captured revisions across polls.
The failed intermediate fixture reports remain historical FAIL evidence.

The critical loading scope's native ownership test passed in
`Native_NavigationLoading_20260908`; that complete batch remains FAIL because its
earlier navigation fixture failed. Production scope changes only positive
`s.AsyncLoadingTimeLimit` values below 5 ms while critical loads are pending,
shares ownership and restores the original value/priority. External writes win.
No saved settings, synchronous flushes or loading-thread switches were used.

`Traversal_20260908_NavigationCompletionSeed177` observed cold dependency
readiness at 7.050 seconds versus 27.580 in the preceding instrumented run.
This is a measured pair, not a general percentage or P95 claim. Four attempts
completed within 17.505 seconds: exactly one actual generation each, unchanged
run/floor/Style, all cleanup verified, no commit. All were rejected with a
two-point partial route. The corrected event now produces finite spatial
recovery instead of waiting for the deadline. Terminal effective load budget
was restored to 1 ms. Runtime acceptance remains FAIL.

The first diagnostic capture in `Traversal_20260908_NavGapGeometrySeed177`
was PARTIAL due to reflected field/enum errors. Its corrected capture in
`Traversal_20260908_PassagePhysicsSeed177/first_attempt_geometry.json` is
CAPTURED with zero errors: 29 components, 731 instances, native Start and End,
and 12 actual doorway capsule sweeps in 0.016 seconds. All 12 sweeps passed
with the measured radius 50 / half-height 88 cm, Pawn profile, real collision
and existing door transforms. This is physical passage evidence, not locomotion
or navigation acceptance. The read-only MCP mesh inspection records frame
collision geometry and native Recast resolutions in `Resume_20260908`.

The actual frame opening is about 136.6 cm before the nearby angled door leaf;
the 19 cm Recast raster quantizes a 50 cm radius to 57 cm. A project-owned
unversioned `AEFCalystoPlayerNavMesh` now supplies finer raster precision only
for the existing Player agent: Low 20 / Default 10 / High 10 cm. Radius 50,
height 176, native Default agent precision, geometry and door transforms remain
unchanged. Values are configured before generator initialization because UE
caches its raster settings. Metrics report the actual default cell size.

`BuildPlayerResolutionReceipt.json` passed. The first class fixture in
`Native_PlayerResolution_20260908` correctly failed: inherited BaseEngine.ini
configuration overrides constructor values after construction. The project now
has an explicit subclass config section. Its focused rerun is
`Native_PlayerResolutionConfig_20260908`; live route acceptance remains PENDING.
The owned `LiveNavigationCompletion` Editor closed normally with exit 0 and no
dirty packages; its performance scope restored after each PIE session.

Recoverable text snapshot: `Resume_20260908/Checkpoint_1125/Snapshot.json` and
`Source.zip` (260 files). The active milestone remains 2 and no V7 floor has
been accepted. Next: verify the Player class/config fixture, launch through the
protected wrapper, then run the same topology and real-door trilogy. If a route
passes, continue into authoring/migration and all remaining milestones without
an approval pause. The sole auxiliary's next-integration audit found production
surface proofs, gameplay snapshot/bridge, planner/materializer calls and
fallible pre-commit publication still unwired; deliberate capability guards
must remain until those consumers exist.

## 12:04 UTC: first accepted native trilogy

`Native_PlayerResolutionConfig_20260908` passed its exact native fixture with
exit 0. `Traversal_20260908_PlayerResolutionSeed177` then accepted the formerly
failing topology 1779679224 on the first attempt: 165 floor / 120 wall / 165 roof
instances, 54 relevant Player tiles, complete five-point route, validated entry,
and an 8.588-second request (8.609 from the actual door interaction). Its harness
failed after acceptance because Python exposes `HasNativeBreak` through the
HitResult struct rather than `GameplayStatics.break_hit_result`.

The independent QA navigation query also exposed a Python class-static dispatch
ensure on the Within=World NavigationSystem CDO. The harness now selects the
unique real system with the exact PIE world outer through a bounded object
iterator and calls its reflected method on that instance. `HitResult.to_tuple`
uses the native UE 5.8 break contract. The intermediate missing World property
binding is retained in `Traversal_20260908_GroundedTrilogySeed177` as FAIL.

`Traversal_20260908_WorldNavTrilogySeed177/traversal.json` records
`NATIVE_TRAVERSAL_ONLY`: HUB -> floors 1, 2, 3 of run 1219803037753221267 through
the actual doors, grounded normal-input entry movement on owned generated floor
components in every floor, and explicit Return to HUB. Total 17.985 seconds;
door-to-ready samples 1.906 / 4.532 / 2.344 seconds. Independent QA path queries
took 0.016 / 0.015 / 0.015 seconds. Grounded movement exceeded 75 cm each.
This does not establish full-route walking, the final floor-cap return, full
gameplay or release readiness. All three screenshot files were visually
inspected as black (identical hashes); visual QA is PENDING, not PASS.
The fixture has an intentionally empty optional wall-light catalog. Camera/fade
and actual lights will be observed before assigning the visual cause.

`LivePlayerResolution/Exit.json` records clean Editor exit at 12:00:44 UTC.
ReadEditorContext reported no dirty packages and restored background throttling.
The process log retains the earlier handled ensure and cannot be a clean release
log. New source-grounded binding calls will be checked in a fresh process.

G4 Theme notes and preview color migration source now preserves ten metadata
leaves. `BuildThemeMetadataReceipt.json` passed; exact native field export and
compilation/precedence tests passed with exit 0 and no warnings in
`Native_Native_ThemeMetadata_20260908/StrictSummary.json` (47.138 seconds).
Actual asset import remains PENDING. The active owned Editor is
`Resume_20260908/LiveSeed119Route`, launched with run -3003287693235273058 to
reproduce topology 1190737158. Next: complete bounded physical route walking and
camera observations, then continue remaining authoring/content integration.
No final-scope acceptance criterion is promoted solely by this partial trilogy.

## 12:33 UTC: full protected walks, native lighting and migration control

`Traversal_20260908_RouteSeed119` completed all three actual-door floors and
complete protected-route walks in 77.594 seconds, followed by explicit HUB
cleanup. First topology1190737158 was observed directly; following topologies
408699671 and35562811 also accepted first attempt. Routes measured86.85m,
96.38m and90.27m, walked in18.171/19.531/18.313seconds with normal character
input and actual owned movement-floor contact. The fresh
`LiveSeed119Route/Exit.json` records exit0; the earlier Python navigation CDO
ensure did not recur. Fade fields proved native-only, so their observation now
comes from read-only C++ `player_view` diagnostics rather than Python reflection.

The explicit additional `-CalystoDirectorNativeParityLighting` fixture option
authors one native BP_WallTorch alternative while retaining empty gameplay and
blocked decals. Its first live preflight failed because
`UBlueprintGeneratedClass::FindComponentTemplateByName` searches ComponentTemplates,
while this actual light lives in SCS. MCP readback of the exact native template
confirmed Movable, Intensity10000, Unitless, visible, attenuation1000cm. The
localized adapter now finds the unique PointLight SCS node and resolves its
actual class template. `Native_TorchScs_20260908` passed the extended existing
metadata test with exit0 (46.900seconds); vendor assets were only read.

`Traversal_20260908_ScsLitRouteSeed177` completed the lit trilogy and all full
walks in92.766seconds. Door-to-ready times8.515/4.641/2.422seconds; route walking
15.094/21.844/27.594seconds. Floor2 rejected topology301248845 for a partial route,
verified cleanup and accepted1405896220 without changing the run; floor3 accepted
1827044188 first attempt. Actual view diagnostics showed fade0/disabled,
viewport present and world rendering enabled. Screenshots floor2/floor3 visibly
show native floor, walls, roof, doors and torches; floor1 is dim. These images
do not prove final themed colors, every authored surface/zone or lighting quality.

`LitNativeGeometryComparison.json` passed an exact canonical comparison of the
six native structural ISM components and731 instances for first topology1779679224
with lights disabled/enabled: identical mesh, transform, bounds and collision/nav
properties. No numeric tolerance or truncated instances were used. This compares
two Director fixtures; a separate raw native-generator parity comparison remains
required before claiming that broader original criterion.

`BuildSafetyAndLitFixtureReceipt.json` and the four exact tests in
`Native_SafetyAndLitFixture_20260908` passed, including all six global ceilings,
shared capacity/initial usage and lit fixture compilation. Original mapper gaps
are now36 after32/68 source leaves were converted; actual current-schema Editor
staging remains next. `Checkpoint_1212/Snapshot.json` preserves265 source/text
files. `ProjectProtectedAfterTrilogies.json` reproduces the same145 historical
discrepancies and identical authoritative-asset records; no rebaseline occurred.

The pending `MigrateAuthoring(EvidenceName, SaveMaster)` MCP operation archives
previous/current reports, stages through the fixed importer and refuses saving
the exact master until every source mapping is resolved. It does not switch
runtime authority. Source is compiled together with the independently implemented
unversioned decal pool. Initial `BuildMigrationToolAndDecalPool` failed only for a
parameter hiding AActor::Owner; it restored Daz receipts. The parameter was renamed
and explicit material-domain includes added. Current retry is
`BuildDecalOwnerNames`; pool native tests and live migration remain PENDING.

## Decal component tests and current-schema Editor staging, 12:55 UTC

The subsequent protected builds `BuildDecalOwnerNames` and `BuildDecalSchema`
passed. First pool tests in `Native_DecalPool_20260908` failed because the fixture's
PCG component registered without nongenerated actor bounds; this is preserved as
FAIL. Corrected fixture ownership uses a noncolliding bounds root before PCG
registration. `Native_DecalSchema_20260908/StrictSummary.json` passed all three
exact tests in 48.249 seconds with launcher exit 0 and no failures: staged decal
culling/release, atomic rejection, and native complete-field export. Production
decal selection, texture/material closure and transaction integration remain pending.

Fresh protected Editor session `Resume_20260908/LiveAuthoringStage` confirmed
UE 5.8.2, target HUB, stopped PIE and no dirty packages. Discovered
`MigrateAuthoring` staged `AuthoringStage_20260908_G24567` in 0.39 seconds with
2,912 mapping records, 31 pending leaves, no authoring errors and unchanged V6.
An explicit save request in `AuthoringSaveGuard_20260908_G24567` verified refusal
while mappings remain pending. No master was saved and authority did not change.
The normal Editor closed cleanly through the project MCP operation and protected
launcher, whose `Exit.json` records exit zero.

G3 optional-architecture reservation code is now written but not compiled:
shared capacity across eight zones, Style default Chance with explicit zone
override, typed payload weights/Empty, pre-Chance compatibility/cooldowns/spacing,
independent deterministic opportunity rank and variation. Native world assembly
and materialization remain pending; this is not an architecture acceptance claim.
The sole auxiliary implements G1 explicit modifier inputs/bindings in parallel.

## Complete authoring migration and shutdown failure, 13:16 UTC

`BuildTraitsArchitecture` passed in 62.76 seconds. Its first exact six-test run
crashed in the new architecture fixture's self-container `TArray::Add` call;
`Native_TraitsArchitecture_20260908` remains FAIL. A temporary copy corrected that
test setup. `BuildArchitectureFixtureCopy` passed in 7.19 seconds and
`Native_TraitsArchitectureCopy_20260908/StrictSummary.json` passed all six exact
tests in 47.760798 seconds with launcher exit 0. These include 100,000 architecture
Chance/conditional Empty/mesh decisions, shared room caps, stable identities,
fixed variation, typed trait bindings and complete native field export. The pure
importer then passed 16 tests in 0.545 seconds, including exact malformed G3 fields.

`AuthoringCompleteStage_20260908` verified 2,913 mappings with zero pending fields,
zero authoring errors and unchanged V6. `AuthoringMasterSave_20260908` saved only
`/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector` in 0.516 seconds and
verified mapped native values. `AuthoringMasterIdempotence_20260908` repeated the
import in 0.391 seconds with zero saves. Source hashes and dirty-package checks
passed. The exact asset file is 304,976 bytes, SHA-256
`2860b6f14ad8397019da8123e66bfa41dc3ef44fa52b3de0451d1e15c299089b`.

However, `Resume_20260908/LiveMasterMigration` exited with code 3 during shutdown:
the floating master asset window was destroyed after HUB/world services had been
cleaned, and the UnrealEd/Slate stack reported an access violation. The master
save/idempotence receipts are retained facts, but this session is NOT a clean
Editor acceptance gate. The pending project-owned closure correction closes
asset editors while the engine is alive, waits for native window processing and
then requests engine exit. A fresh load/inspection/clean-exit run is required.

`ProtectedAfterMasterComparison.json` confirms the exact same 145 historical
mismatches and authoritative asset records as the previous phase; no baseline
changed. `Checkpoint_1305/Snapshot.json` (actual UTC 13:00:53) archives 275 text
sources before the fixture-copy and later Editor UI/closure edits. Native world
architecture assembly, full gameplay, final visual QA, legacy retirement and
packages remain pending. Current master creation is not runtime cutover.

## Fresh master reload and current integration, 20:47 UTC

There was no owned Editor/build running during the observed interruption between
13:35 and 19:52 UTC. This interval is not recorded as active implementation.

`Resume_20260908/LiveMasterReload_2000` now proves the saved master reloads,
validates all 2,913 migration records and repeats the native import with zero
saves. `MasterDetails.png` is the actual 2560x1392 asset window, captured by
`CaptureAuthoringDetails`; it was inspected at original resolution. The scoped
shutdown closes asset editors before engine teardown, and this fresh session's
`Exit.json` records actual exit 0. The earlier exit-3 evidence remains archived.
Nested authoring controls and Undo/Redo are still separate pending checks.

The real same-world ACF inventory/equipment/item-fragment, companion and outcome
snapshot passed identity/reordering/rollback tests. Coordinator publication is
reversible until exact player-release acceptance. Five subsequent native tests
in `Native_ActivationNativeContract_20260908_2021` verify accepted materializer
activation, cancellation and exact binding cleanup with actual exit 0. The full
project gameplay bridge and cross-world handoff are not yet connected.

`BuildBakedSurfaceLoading_2043` failed because UE 5.8's quaternion SIMD overload
does not accept the ambiguous integer scalar `-1`. The exact `-1.0` correction
passed in `BuildBakedSurfaceLoadingScalar_2045` (12.97 seconds, repaired Daz
receipt). `Native_BakedSurfaceLoading_20260908_2045/StrictSummary.json` passed all
eight exact tests in 65.5283125 seconds, actual process exit 0:

- Native baked decoding preserves the Forge table's nine children and eight
  dependencies: one mandatory table, eight explicitly optional 50% children,
  and native relative/world yaw behavior. Point reorder preserves identities.
- 100,000 trials per baked probability/rotation law calculated in 4.397 seconds;
  independent six-sigma and uniform-CDF checks passed without world generation.
- Registered collision fixtures prove dense floor candidates and actual wall,
  corner and roof support. Whole variation envelopes detect off-center blockers;
  protected routes, moved geometry and finite-capacity rejection are exercised.
- Real streamable handles retain both baked parents and children, deduplicate
  phase requests and release correctly on cancellation. Stale callbacks cannot
  revive the request. The selected Style/depth query excludes unreachable visuals
  and honors inherited decoration chance even with a zero inactive local curve.

The physical candidate fixture does not claim positive Player-Recast coverage.
That bounded fixture and progression-marker protection are the auxiliary's next
task. Architecture/content world materialization remains the parent's next
integration. `Checkpoint_2046` archives 289 dirty text/source files before these
follow-ups; its surface implementation/test hashes match the compiled batch.
No full final-tree criterion changed to PASS: 0/31 final criteria are complete.

## Checkpoint at 21:41 UTC: architecture integration and accepted activation

`Checkpoint_2135` archives 294 dirty source/text files. The architecture runtime now
uses the shared actual-geometry candidate assembler, native-slot planner, immutable
mesh/baked parent-child manifest, native ISM batch, current navigation revision,
actual material/instance verification and token-scoped activation/cleanup.
The production coordinator handles accepted activation failure explicitly: retain
accepted state, block player release, record accepted cleanup and forbid spatial
recovery of the accepted transaction. No full gameplay bridge is implied.

The combined build passed in 48.76 seconds; its exact dependency-list correction
passed in 11.85 seconds. Native_ArchitectureIntegration_20260908_2134 remains FAIL:
11/12 tests passed in a 59.416-second process, but Player-Recast fixture initialization
received EnhancedInput ensures before its first navigation tick. The eight-zone and
baked physical feasibility tests, actual original Forge payload-to-ISM realization,
100,000-trial baked probability/yaw suite, and eleven coordinator activation cases
passed individually. Do not promote the failed whole batch.

The corrected diagnosis is that UE5.8 TObjectIterator excludes Unreachable but not
Garbage. MarkObjectsPendingKill alone cannot remove deinitialized world-input
subsystems from that iterator; one real GC before constructing the latent fixture
is being validated. No engine patch, fabricated PlayerInput or ensure suppression.
Typed feasibility outcomes now distinguish actual pending registration, spatial
changes, unavailable resources, unsupported configuration and the shared deadline.

Protected comparison at 21:36 UTC: the same 145 historical discrepancies remain;
no new protected mismatch and no rebaseline. All 13 Calysto protected assets match
this run's original snapshot. The master SHA256 remains
2860b6f14ad8397019da8123e66bfa41dc3ef44fa52b3de0451d1e15c299089b.

Next: require a clean focused test exit, then use the explicit lit architecture
fixture for three actual floors; require nonzero reserved/verified architecture
on each floor before counting that test. Continue the full master/gameplay bridge,
remaining controls and final release gates. Whole final criteria remain 0/31.

## Checkpoint at 22:33 UTC: exact native contact, portable inventory and final observation

`Checkpoint_2230` archives294 source/text files. `Native_NativeContactPortable_20260908_2228`
passes7/7 with process exit0 in49.505 seconds. Its new portable fixture destroys and
collects the original world before reconstructing native inventories in a second
world; GUIDs, counts, classes, fragment cross-links, currency and roster aliases
survive without inventory/equipment/roster broadcasts. Equipped actor/effect
restoration remains an explicit unsupported contract, not full travel acceptance.

The original Forge table has a measured1.93752mm authored contact gap. Feasibility
now permits at most2.5mm of attachment gap while retaining0.5mm independent plane
and penetration checks. The actual native table passes real floor trace and swept
collision verification. A missing center tile and an excessive gap reject; no
mesh, transform or vendor collision was modified. The unnecessary render-bound
contact experiment was removed after actual collision and render minima matched.

`Traversal_20260908_NativeContactSeed177` remains FAIL: two architecture parents
and eight exact mesh instances were reserved/materialized, but final player release
timed out. The new Pending branch did not rearm EFLevelFlow's one-shot observer.
`BuildPendingObserver_2234` passes after scheduling its next owned observation at
0.1 seconds without resetting the30-second deadline. `LivePendingObserver_2234`
is the next actual three-floor gate; prior failed receipts remain immutable.

Protected comparison `Protected2218Comparison.json` confirms the same145 historical
mismatches with no new drift or rebaseline. All13 protected Calysto assets pass;
the master remains SHA2562860b6f14ad8397019da8123e66bfa41dc3ef44fa52b3de0451d1e15c299089b.
The sole auxiliary is drafting a direct enemy/FloorLocal NPC bridge and necessary
Social accepted-cleanup/Director companion initialization patches outside Source
until a coherent shared-build boundary. Full master, all controls, remaining
gameplay/advanced functions, legacy retirement and both release stages remain pending.
