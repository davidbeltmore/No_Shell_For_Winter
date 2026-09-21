# Continuous Calysto V7 execution

Use this mode when the user authorizes full implementation or overnight work.
Read the [master contract](../../../../Docs/Migration/Calysto_Dungeon_Director_V7.md)
and use [validation-gates.md](validation-gates.md) without changing its scope,
counts or deadlines. This reference adds continuity and receipt discipline; it
does not add release gates or certify any implementation.

## Execution ownership and continuity

Keep the authorized task active through implementation, real PIE, visual review,
builds, cook, packages, audited retirement and final-tree verification. Continue
from the first incomplete milestone after each checkpoint. A skill delivery,
passing native suite or successful build is an intermediate result. Do not end
the task to ask whether to continue already authorized work.

When the user explicitly requests a persistent goal, use the available goal tool
and retain that goal across checkpoints and context compaction. Do not infer a
new goal or token budget from an ordinary task. Follow the platform's actual
goal lifecycle; only demonstrated completion can mark it complete. Record an
external blocker precisely, continue independent authorized work, and use the
platform's blocking rules when no meaningful progress remains. User stop/cancel
instructions take effect immediately.

The user's 2026-09-12 instruction selects GPT-5.6 Terra Ultra at Standard speed
for the remaining V7 closure. Follow the
[Terra closure plan](../../../../Docs/Migration/Calysto_Director_V7_Terra_Closure_Plan.md),
starting with the ordinary-launch HUB return and actual active authority. This
supersedes the earlier strongest-model preference. Verify the client selection;
a document edit does not switch the running lead model. Never purchase extra
usage, switch to paid overage or assume that a model
name proves its billing tier. Use only exposed session/settings capabilities for
a verified model choice; do not invent model-switching APIs. Keep the current
authorized session when no verified alternative is available and state any
unresolved model/quota limitation without claiming a change occurred.

One lead agent owns the Editor, PIE, builds, packages, protected baselines and
runtime mutation. At most one auxiliary agent may run at a time, limited to a
bounded independent source/documentation task with explicit file ownership.
It must not launch an Editor, run builds, manipulate PIE or spawn further agents.
Before an ownership handoff, record owned PIDs, project paths, active request,
dirty packages and cleanup state. Never overlap editor owners or build against
a changing dependency set. Preserve all unrelated dirty work.

## Automatic checkpoints

Start a monotonic checkpoint timer and write a checkpoint every 45-60 minutes.
Do not cancel a healthy bounded operation merely to checkpoint; first record its
live PID, log position and deadline, then finalize the receipt after it exits.
Checkpoints do not reset step or floor-request deadlines, suspend execution, or
require the user to approve the next authorized step. Continue immediately.
Frequent progress communication still follows the 60-second maximum in the
timing reference; the checkpoint interval does not replace it.

Store unique evidence under `Saved/Migration/CalystoDungeonDirectorV7/` and a
durable summary under `Docs/Migration/Evidence/`, outside runtime/cook. Preserve
the prior checkpoint. Each checkpoint contains:

- UTC timestamp, elapsed time, milestone and exact candidate/final scope.
- Git HEAD/status and a non-destructive snapshot of dirty files, with SHA-256
  manifest including paths and sizes. A Git commit alone does not identify a
  dirty compiled tree. Keep source, Config, assets and tool hashes explicit.
- Frozen acceptance IDs changed, their PASS/FAIL/PENDING result, evidence paths,
  remaining gaps and verified/total count.
- Current process ownership, launch command/arguments, deadlines, dirty packages,
  active request/attempt identities and cleanup status.
- Protected comparison before/after the phase and historical discrepancies
  separately; never replace a baseline to make a comparison pass.
- One explicit next action, its owner, prerequisite and intended receipt path.
  Record the observed blocker and the correction being tested when applicable.

Resume by checking that the process and source hashes still match the checkpoint,
reading the last log/receipt and rediscovering MCP after any Editor restart.
Observe a still-running owned job within its existing deadline; do not launch a
duplicate. Reuse evidence only when its complete dependency scope is unchanged.
A changed dependency invalidates affected claims until tested again.

## Source-bound receipts and phase cleanup

Before execution capture the exact source/configuration/asset/tool manifest and
its snapshot hash. After execution compare that scope again; source edited while
a binary was running is not proof for the edited tree. Record engine version,
binary/build receipt hashes, configuration, actual arguments, timestamps,
elapsed time, owned PIDs and actual process exits. Keep wrapper and child exits
separate. Missing/null exit, timeout, crash or relevant log warning/error cannot
be replaced by a PASS field in a test report.

Attach test inventory and parameters; source/target symbols and asset paths;
run/floor/request/attempt and topology seeds; requested/feasible/selected/rejected/
accepted counts; inspected screenshot paths; and protected comparisons when
relevant. Mark unexecuted scope PENDING, an observed failure FAIL, and full-scope
verified evidence PASS. A previous source revision can supply partial historical
evidence, never an unqualified current-tree PASS.

At phase completion or failure, finish supported cleanup of owned callbacks,
PIE, attempts, processes and temporary test state. Preserve unsaved user packages,
reports and logs. Verify cancellation/exit and rejected-attempt isolation. Use
the protected build/launch wrappers and verify both Daz plugins remain enabled
in descriptor and receipts after any interrupted UBT operation. Never terminate
an unrelated process or delete Unreal packages through filesystem operations.
If cleanup cannot establish isolation, record the failure and diagnose it before
starting another editor or attempt.

## Verified overnight helper bindings

These source bindings were inspected on 2026-09-08. Reinspect their signatures
and hashes before running changed versions. Binding existence is separate from
successful live execution; the acceptance ledger records measured results.

| Capability | Verified source binding | Evidence limit |
| --- | --- | --- |
| Read-only MCP context | `scripts/Invoke-UnrealMcpMetaTool.ps1`: `ToolName`, `ArgumentsJson`, `Uri`, `TimeoutSec`, `Raw`; exact schemas come from discovery | Follow [operator-control.md](operator-control.md); two bounded connection attempts, no blind replay of timed-out mutations. |
| Project MCP operator | `EFProceduralEditor.EFCalystoDirectorToolset`: `ReadEditorContext`, `ArmTraversal`, `ReadDirectorDiagnostics`, `MigrateAuthoring`, `CaptureAuthoringDetails`, `RequestEditorShutdown` | Six operations discovered/exercised September 8. Fresh master reload and zero-save import passed with clean Editor exit0. See [exact schemas and order](operator-control.md#verified-project-owned-calysto-mcp-operations); preserve string-vs-image-object return types and int64 identities. Operator access is not traversal acceptance. |
| Quick/release schedule | `scripts/calysto_test_plan.py plan --mode quick|release [--output <json>]` | Generates PLANNED JSON only; it runs no tests and enforces no executor deadline. Its missing runner bindings stay explicit. |
| Exact native automation | `Tools/Migration/Run-CalystoDirectorNative58.ps1 -ExpectedTestsFile <reviewed-file> -TimeoutSeconds 180 [-TestPrefix <registered-prefix>] [-Stamp <unique>]` | Requires editors closed; Python supervisor writes strict inventory/log/exit summary. A test subset is not complete Section 11 probability acceptance. |
| Real-door PIE observer | Project MCP `ArmTraversal` owns the fixed `Tools/Migration/Validate-CalystoDirectorTraversal58.py` invocation with `evidenceName` and `mode` (`native_parity` or `full`) | Discover/preflight before arming, require fresh ARMED, then StartPIE within the 30-second startup limit. Overall harness budget remains 150 seconds. Native parity needs the matching launch flag. No arbitrary Python tool is required. |
| Graceful Editor closure | Project MCP `RequestEditorShutdown` after no active/queued PIE and zero dirty content/maps | Rechecks after two seconds. `SHUTDOWN_PENDING` is not exit evidence; verify the owned process's actual exit and final log. |
| Traversal receipt states | `NATIVE_TRAVERSAL_ONLY`, `TRAVERSAL_OBSERVED`, or `FAIL` from the traversal script | It approaches doors by teleporting into interaction range. Capture actual movement separately; explicit cleanup HUB return does not prove the intended final-floor return. Script SHA-256 is not a complete compiled-source manifest. |
| Protected Editor launch/build | `Tools/Migration/Build-NoShellForWinterEditor58.ps1` and `Launch-NoShellForWinterEditor58.ps1` | Inspect actual parameters; never invent Game/Shipping/package switches for an Editor wrapper. |

Do not synthesize V7 commands by renaming V6 runners, assume console aliases or
invoke readiness callbacks. Missing package, soak, field-coverage or failure
bindings are bounded implementation work before execution. Keep source-defined
API inventory and demonstrated runtime behavior separate.

Use the existing short sequence: targeted native/probability and affected
Blueprint checks, then three consecutive real-door floors; correct the first
observed failure before rerunning that gate. After the rapid gate passes, run
the one planned 25-consecutive-floor retention soak and finite release matrix.
The 100,000 trials are in-memory sampling. Do not expand to 1,000 world floors,
repeated unchanged failures or broad test sweeps. For skill-only edits validate
the skill; run helper behavioral tests only for changed helper code.

## Frozen acceptance accounting

The [JSON ledger](../../../../Docs/Migration/Evidence/Calysto_Director_V7_Acceptance.json)
freezes every original bullet in Sections 11.1-11.4 once, using IDs
`V7-11.<section>-<ordinal>`. Section 11.5 supplies evidence/stopping rules, not
additional counted bullets. Keep original text and denominator immutable unless
the user explicitly changes product scope; retain a prior ledger and record any
such change. Do not split easy subchecks into new completion units.

`completion_percent = 100 * fully_verified_original_criteria / 31`.

A criterion earns one only when every part is proven for its required current
scope. Partial tests earn zero, while their receipts remain linked. Unit-test
counts, trials, changed files, milestones, hours and estimated percentages cannot
inflate this calculation. Retain distribution entitlement as its own original
criterion; technical package success cannot confirm it. Report candidate and
final-tree limitations explicitly, and continue from the ledger's next action.

## Focused native selection verified September 8

Reuse the runner above with reviewed exact names and a fresh evidence stamp.
`Native_BakedSurfaceLoading_20260908_2045` passed eight tests with process exit0
in65.528 seconds. Its baked simulation calculated in4.397 seconds. Relevant
registered suffixes under `NoShellForWinter.CalystoDungeon.Director.` are:

- `Architecture.BakedProbabilityAndReservation`:100,000 trials per native child
  Chance/yaw law, independent six-sigma/CDF checks, no world generation.
- `Architecture.NativeBakedPayloadContract`: actual native Forge payload and
  read-only graph contract; nine children/eight dependencies, point-order identity.
- `Loading.ReachableVisualSelection` and `Loading.BakedPhaseOwnership`: selected
  Style/depth reachability, real parent/child leases, deduplication and cancellation.
- `ContentSurfaces.DenseGeometryAndCompatibility` and
  `ContentSurfaces.JitterZonesAndAtomicBounds`: actual registered physics fixtures.

The subsequent `Native_NativeSurfaceNav_20260908_2052` batch is FAIL, exit1:
positive Player-Recast coverage passed in0.088 seconds, but
`ContentSurfaces.NativeCollisionCompatibility` exposed native meshes using
complex-as-simple triangle collision. Stored simple boxes are inactive for those
queries. Subsequent exact native collision and mesh realization tests passed, but
their combined batches still failed the isolated Player-Recast fixture. The
`Native_ArchitectureIntegration_20260908_2134` batch is FAIL, exit1,59.416 seconds:
11/12 tests passed. Preserve that overall failure until a fresh clean process gate.

Additional registered and executed tests are `Architecture.NativeManifestToInstances`,
`Architecture.ExactMeshRealizationAndRollback`, `ContentSurfaces.NativeArchitectureGeometryAndVariation`
and `ContentSurfaces.BakedGeometryBeforeConditionalChildren`. They verify native Forge
child ISMs/material slots, exact rollback, eight-zone physical compatibility and
bounded whole variation. Original Forge placement in a generated room remains PENDING.

The fixture failure is lifecycle-specific: UE5.8 EnhancedInput's default TObjectIterator
still visits deinitialized subsystems marked Garbage until real GC. All owned test
worlds mark their objects pending kill; a single real GC before constructing the
latent Recast fixture is pending validation. Never fabricate PlayerInput, manually
mark objects Unreachable, suppress engine ensures or increase the nav deadline.

The clean follow-up `Native_TypedSurfaceLifecycle_20260908_2146` passed12/12 with
actual exit0 in52.472 seconds, including the real GC/Recast lifecycle correction.
It supersedes the unresolved fixture observation above; prior failed receipts stay intact.

The explicit `-CalystoDirectorNativeParityArchitecture` fixture and stricter
traversal assertion were exercised in `Traversal_20260908_ArchitectureSeed177`.
The operation is FAIL: first-floor feasibility rejected `UnsupportedSupportFace`
before content selection or acceptance. The protected Editor closed with exit0
at21:54:34UTC. Identity-only native mesh fixtures did not cover all generated
wall rotations/scales. Diagnose those exact transforms in a small native fixture
before another PIE run. Three nonempty architecture floors remain PENDING.
Empty architecture is not a successful realization test. Full master gameplay,
full rotations/actor payloads, legacy retirement and packages remain separate gates.

The doorway-plane defect was reproduced with the actual `SM_WallDoor`, then
corrected by a robust plane normal and right-handed polygon winding.
`Native_PlanarBoundary_20260908_2206` passed6/6 with exit0. Its PIE follow-up still
failed the nonempty architecture requirement and exposed a final navigation
observation incorrectly converted from Pending into spatial recovery.

`Native_NativeContactPortable_20260908_2228` passed7/7 with actual exit0 in49.505s.
It adds `ContentSurfaces.OriginalForgeOnNativeFloor` and
`GameplaySnapshot.PortableWorldReconstruction`. The real Forge feet have a
1.93752mm authored gap. The finite attachment allowance is2.5mm; independent plane
and penetration tolerance remains0.5mm. Real missing-tile and larger-gap tests
reject compatibility. No native transform or collision asset was modified.

The native release handshake now returns `Pending`, `Released` or `Rejected`.
EFLevelFlow keeps protection and observes again for Pending under the original
shared deadline. It must not call rejection or authorize another attempt for that
result. Inspector reads remain observations only.

Portable inventory preparation and explicit travel detachment preserve the
logical item/fragment graph without retaining the destroyed source world.
Reconstruction requires exact inactive destination owners and sufficient native
capacity. Equipped-item effects/actor restoration and full bridge integration
remain unsupported/PENDING; passing the unequipped transport fixture is not full
inventory-travel acceptance. `LiveNativeContact_2230` is the next actual PIE gate.
