# Director operator control cookbook

Use this reference for exact commands, runtime controls and their evidence limits.
The legacy inventory was read on 2026-09-05; the four project MCP operations below
were discovered and exercised on 2026-09-08. Reinspect changed files and the
loaded class before using them. V6 commands below remain V6 even when a V7
candidate exists. Never rewrite a version in a command and assume it works.

## Implemented V7 candidate interfaces — validation pending

The unversioned candidate now has source implementations. Its cold build,
three-floor traversal, complete gameplay bridges and release gates must be
checked against current receipts; API existence is not a passing gate.

`UEFCalystoDirectorSettings::IsEnabled()` selects the candidate from its
`bEnabled` configuration setting. Development/Editor also accepts
`-CalystoDirectorCandidate` or `-CalystoDirectorNativeParity`; these command-line
switches do not enable Shipping. Native parity uses an explicitly transient
fixture and is not the authored production asset or a release candidate.
Never enable the candidate in a running GameInstance: subsystem creation and
provider registration happen during GameInstance initialization.

The source-defined Blueprint-callable run controls on
`UEFCalystoDirectorSubsystem` are:

| Operation | Exact native API | Constraint |
| --- | --- | --- |
| New run | `RequestStartNewRun()` | Returns acceptance of the request, not floor success. |
| Seeded run | `RequestStartNewRunWithSeed(int64 Seed)` | Seed is a run seed, not the generated topology seed. |
| Advance | `RequestAdvanceFloor()` | Requires the current floor to be Ready. |
| Replay | `RequestReplayCurrentFloor()` | Retains current floor/reroll identity and selected Style. |
| Reroll | `RequestRerollCurrentFloor()` | Increments reroll identity, retains selected Style. |
| Explicit retry | `RequestRetry()` | Requires current snapshot `bCanRetry`; preserves failed floor/Style. |
| Explicit HUB return | `RequestReturnToHub()` | Requests owned cleanup before travel. |
| Cancel | `RequestCancel()` | Cancels unfinished work; never means successful generation. |
| Read state | `GetSnapshot()`, `IsTravelRequestPending()` | Observe the request under its one 30-second deadline. |

Use the live MCP discovery schema to access these reflected functions; there is
no assumed generic V7 console alias. `GetSnapshot()` exposes requested versus
last committed floor, run epoch, seed, reroll, Style ID, topology seed, attempts,
failure text, retry/HUB capabilities, and separate native/gameplay verification
flags. `Ready` in the native parity milestone does not imply
`bGameplayVerified=true` or full catalog/companion/outcome acceptance.

`AwaitingPlayerRelease` keeps the transaction open after native verification.
EFLevelFlow consumes `ResolvePlayerStartTransform` exactly, positions the pawn,
checks its view and calls the **native-only** `ConfirmPlayerRelease` handshake.
Only that owned handshake commits and publishes `FloorReady`. Entry/view failure
calls `RejectPlayerRelease`, rejecting the whole spatial attempt. Operators must
not forge these callbacks to satisfy a test. Failure preserves movement/input
and damage protection and presents Retry / Return to HUB. LevelFlow has a
35-second disconnected-owner watchdog; it does not extend the Director's
30-second generation deadline or release an invalid floor.

`AEFCalystoFloorDoor` implements `IEFCalystoDirectorPortal`. Its explicit approach
is local `(0,-90,0)` transformed by the owned door actor, and its selected mesh
comes from the Style's `Architecture.ProgressionDoorMesh`. Navigation verifies
that point's designated room, floor contact, capsule clearance and complete
route. Door interaction requires the Director's committed Ready state and
native verification. The enabled branch does not consult V6 Harness settings.

Source entrypoints are under:

- `Plugins/EFProcedural/Source/EFProceduralRuntime/{Public,Private}/Calysto/EFCalystoDirectorSubsystem.*`
- `Plugins/EFProcedural/Source/EFProceduralRuntime/{Public,Private}/Calysto/EFCalystoDirectorSettings.*`
- `Plugins/EFLevelFlow/Source/EFLevelFlowRuntime/Private/EFLevelFlowSubsystem.cpp`
- `Plugins/EFProcedural/Source/EFProceduralACFURuntime/Private/Calysto/EFCalystoFloorDoor.cpp`

The bounded native suite runner is now
`Tools/Migration/Run-CalystoDirectorNative58.ps1 -ExpectedTestsFile <reviewed-file> -TimeoutSeconds 180`.
Create the exact inventory from current registered tests and review it before
running; never reuse a pinned legacy count. The runner selects the exact reviewed
names using UE 5.8's `^Name$` anchors joined by `+` in one process. It compares every test name and
aggregate result, requires zero warnings/errors and successful process exit,
and rejects missing or malformed reports. Optional `-TestPrefix` accepts a
reviewed narrower test prefix; every expected test must belong to that prefix,
so a focused corrected test does not require rerunning unrelated passing tests.
`-TestPrefix` constrains the permitted inventory; it does not expand execution
to every matching test. This also avoids UE's complete-path-token prefix rule,
which selected zero tests for the former partial `Director.L` filter. Exact
selection passed in `Native_LightingLifecycleExact_20260908` and
`Native_RoundedBounds_20260908`, including clean process exits.
Native automation starts `/Engine/Maps/Entry`; this deliberately excludes cold
HUB loading from the native unit gate. Cold Editor/HUB and real traversal remain
separate required gates.

`Tools/Migration/calysto_native_supervisor.py` now owns subprocess waiting and
strict postprocessing. The protected PowerShell launcher remains the only code
that starts Unreal Editor. `SupervisorStatus.json` records driver PID/stage and
`StrictSummary.json` records the exact exit code and complete inventory result.
Timeout uses an exact project/log match to stop only the owned editor (four
seconds maximum for that query), then caps driver cleanup waits at seven
additional seconds. Log/report byte limits and a ten-second log-scan deadline
prevent unbounded postprocessing. Preserve unsaved editor work before using the
wrapper: it requires all editors closed.

The superseded PowerShell supervisor stalled after its child exited, including
after redirected parent pipes were removed. Its precise internal stall was not
established; the 415,950-byte failure log scan was independently ruled out at
0.107 seconds. Do not restore that supervisor based on passing isolated waits.
The Python replacement was tested with fake exit 0, exit 7, forced timeout and
malformed/incomplete reports without launching Unreal; see
`Saved/Migration/CalystoDungeonDirectorV7/SupervisorSelfTest_20260905/SelfTest.json`.

The structural fixture no longer calls `InitializeNewWorld` after `CreateWorld`:
UE 5.8 already initializes it. Reintroducing that call duplicates WorldSettings
and crashes the suite. A report emitted before an engine crash is a failed gate.

The candidate production decision test is
`NoShellForWinter.CalystoDungeon.Director.Probability.100000Trials` in
`EFProceduralRuntime/Private/Tests/EFCalystoDirectorFoundationTests.cpp`. It
performs 100,000 in-memory trials covering 37% opportunity presence,
conditional Forge/Shrine weights 5:3, five eligible rooms with
`1 + Binomial(4,0.25)`, fair guaranteed-room ranking, all five populated rarity
tiers including Winter, inclusive uniform amounts, rounded triangular amounts,
continuous triangular mean and count feasibility conditioning. The separate
`Director.Probability.BoundariesAndIdentity` covers exact boundaries and stable
identity behavior. Read actual assertions after changes. Neither suite proves
accepted-world probabilities or materialization; those need runtime receipts
including rejected attempts.

## Live discovery and JSON transport

Start with the read-only MCP preflight in `$noshellforwinter-unreal-mcp`.
The native endpoint is `http://127.0.0.1:8000/mcp`. Use discovered connector tools
when available. The local transport helper is
`scripts/Invoke-UnrealMcpMetaTool.ps1`; its actual parameters are `ToolName`
(`list_toolsets`, `describe_toolset`, `call_tool`), `ArgumentsJson`, `Uri`,
`TimeoutSec` (default 30) and `Raw`.

When using PowerShell, call the helper in the **same process**. An additional
`powershell -File` boundary can strip quotes from JSON arguments. Set execution
policy only for this process and serialize a hashtable instead of building JSON
or shell text by interpolation:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$calystoMcpHelper = Join-Path (Get-Location) '.agents/skills/calysto-dungeon-master/scripts/Invoke-UnrealMcpMetaTool.ps1'
& $calystoMcpHelper -ToolName list_toolsets -ArgumentsJson '{}' -TimeoutSec 10
```

After discovery, build the argument object using the **returned schema**, then
pass it directly:

```powershell
# $calystoMcpArguments is the hashtable matching the discovered meta-tool schema.
$calystoMcpJson = ConvertTo-Json -InputObject $calystoMcpArguments -Depth 20 -Compress
& $calystoMcpHelper -ToolName describe_toolset -ArgumentsJson $calystoMcpJson -TimeoutSec 10
```

Describe the relevant toolset before `call_tool`; use its exact returned tool
name and argument schema. Neither this reference nor the transport helper
defines an asset, Blueprint, Python or PIE tool schema. Avoid `-Raw` in a series
of same-process commands: the helper currently executes `exit 0` in that branch.
`TimeoutSec` is one monotonic end-to-end transport deadline covering initialize,
initialized notification, tool call, headers and JSON/SSE response bodies. It
does not extend the dungeon request deadline. Both JSON-RPC errors and MCP
`result.isError` fail, including with `-Raw`. Use at most two connection attempts;
record elapsed time and do not automatically replay a timed-out mutating call.

If MCP is unavailable, run the project connection probe once, inspect the exact
UnrealEditor executable and command line, and distinguish an absent server from
the wrong project. Mark live conclusions PENDING. File inspection does not prove
loaded state. Re-discover tools after Editor restart.

### Verified project-owned Calysto MCP operations

Describe `EFProceduralEditor.EFCalystoDirectorToolset` after each Editor restart.
The discovered fully qualified tool names use that prefix plus the operation
below. Status operations return an object whose required `returnValue` is a
**JSON string**. Read its `status` and `message`; transport success does not mean
operation success. `CaptureAuthoringDetails` instead returns the native image
object directly in `returnValue`, containing `mimeType` and base64 `data`.

Live authoring staging and the pending-mapping save guard were executed on
2026-09-08 in `AuthoringStage_20260908_G24567` and
`AuthoringSaveGuard_20260908_G24567`. Staging verified 2,912 records in 0.39 seconds,
with 31 unmapped leaves, zero authoring errors and unchanged V6. That historical
receipt proves the staging/refusal guard. The later `AuthoringMasterSave_20260908`
resolved every original mapping decision, verified 2,913 records and saved the
exact unversioned master. `AuthoringReloadIdempotence_20260908` verified the
saved master again in a fresh Editor in 0.407 seconds: zero saves, zero authoring
errors and unchanged V6. The original saving session crashed during shutdown;
the corrective fresh reload session `Resume_20260908/LiveMasterReload_2000`
closed normally with actual exit 0. These are authoring/operator gates only;
the full master runtime still has explicit capability gates. Authoring output uses
the fixed evidence directory with archived `ImportDirectorStaged.json`; its
operation does not arm PIE. Use a fresh evidence name and the 10-second MCP call
budget; if it times out, inspect its receipt before considering another call.

| Operation | Exact input object | Observed behavior and constraints |
| --- | --- | --- |
| `ReadEditorContext` | `{}` | Reports actual project/file, engine version, Editor/PIE worlds, active or queued PIE, dirty content/maps, Python availability and pending shutdown. `OK` confirms the target context only. |
| `MigrateAuthoring` | `{"evidenceName":"<fresh_name>","saveMaster":false}` | Executes the fixed importer, archives previous/current receipts, exports V6 read-only and roundtrips current native authored structs. `saveMaster:true` permits only the exact unversioned master after complete mappings; pending mappings return `AUTHORING_STAGED` with `save_permitted:false`. Requires stopped PIE and no dirty packages. Never switches runtime authority. |
| `ArmTraversal` | `{"evidenceName":"<fresh_name>","mode":"native_parity"}` or mode `full` | Requires the exact target, UE 5.8, loaded `/Game/_Game/Hub/HUB.HUB`, stopped PIE, no queued shutdown and matching native-parity launch flag. Runs only the fixed project traversal harness. Returns `ARMED` after verifying its fresh receipt. |
| `ReadDirectorDiagnostics` | `{}` | Reads the existing Director from the sole actual PIE GameInstance. Returns `pie_world`, `game_instance` and `diagnostics_json`; fails for absent or ambiguous PIE ownership. It creates no run. |
| `CaptureAuthoringDetails` | `{}` | Captures the already-open exact master asset window at its actual resolution, up to 16 megapixels. Requires stopped PIE. It neither opens/expands UI nor edits/saves assets. Verified as a readable 2560×1392 PNG in `LiveMasterReload_2000/MasterDetails.png`; inspect the image itself. |
| `RequestEditorShutdown` | `{}` | Requires no active/queued PIE and zero dirty content/map packages. Returns `SHUTDOWN_PENDING`; after two seconds, closes native asset editors while Editor services are alive, observes at least two frames and no remaining open asset editors, then requests graceful exit. Dirty state, PIE or a 10-second asset-window timeout cancels closure. Actual exit 0 with the master window open was verified in `LiveMasterReload_2000/Exit.json`. |

`evidenceName` accepts 1-80 ASCII letters, digits, `_` or `-`. For `ArmTraversal`, output is fixed to
`Saved/Migration/CalystoDungeonDirectorV7/<evidenceName>/traversal.json`; an existing
directory or file is rejected. Prepare the fresh name and discover StartPIE
**before** arming. After returned `ARMED` and the exact matching receipt, immediately
call `EditorToolset.EditorAppToolset.StartPIE` with the discovered options:

```json
{"options":{"bSimulate":false,"playMode":"PlayMode_InViewPort","warmupSeconds":0}}
```

The harness allows 30 seconds for external PIE startup and 150 seconds total.
After verified arming, its owned performance scope temporarily disables Editor
background CPU throttling in memory, including the separate rendering setting.
It saves no configuration. The scope restores the original setting and removes
only its own delegate on terminal receipt, EndPIE, cancelled startup, shutdown
or the 150-second deadline; at 145 seconds it requests owned PIE cleanup.
`ReadEditorContext` reports focus, actual throttling, frame cadence, active scope
and restoration reason. Do not disable throttling globally or extend floor
deadlines to compensate for background Editor cadence. Live restoration was
verified in `Resume_20260908/LiveBoundsObservation/Editor.log` and its context
receipts; lifecycle assertions passed in `Native_RoundedBounds_20260908`.
Do not insert discovery, unrelated inspection or a pause between arming and
StartPIE. The harness and Director deadlines continue independently of an MCP
call. A StartPIE acknowledgment supplies no floor acceptance. Observe the receipt
and actual Director, then `EditorToolset.EditorAppToolset.IsPIERunning` (`{}`);
use `StopPIE` (`{}`) only if the owned session still needs cleanup.

Preserve `diagnostics_json` verbatim in evidence. Decode that string with an
integer-preserving parser, such as Python `json.loads`, when inspecting int64
run/floor identities. Do not round-trip it through JavaScript `Number` or an
Unreal JSON numeric object: the run seeds exceed exact IEEE-754 integer range.
Distinguish `actual_root_generation_requests` from transaction authorization;
null remains unknown. A measured actual count of one does not prove accepted
geometry, navigation, gameplay or traversal.

Before shutdown, confirm `pie_active_or_queued=false`, empty `pie_worlds`, and
empty dirty package arrays. After `SHUTDOWN_PENDING`, retain the owned launch
handle, verify its actual child exit code and audit the final log. A pending
response or disappearance of MCP is not exit evidence. Preserve unsaved work if
the tool rejects closure; it never saves packages or forces process termination.

For a native authoring screenshot, first discover/use
`EditorToolset.EditorAppToolset.OpenEditorForAsset` with
`{"assetPath":"/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector"}`.
Then call `CaptureAuthoringDetails`, decode `returnValue.data` to a fresh PNG
under `Saved/Migration/CalystoDungeonDirectorV7`, and inspect it at original
resolution. The stock `CaptureEditorImage` composites all desktop windows and
shrinks to 1280 pixels; it did not produce readable master Details on this
multi-monitor setup. The scoped capture proves visible UI only, not runtime
effects, expanded nested controls, Undo/Redo or complete authoring acceptance.

This path worked when Computer Use reported `native pipe` missing. It requires
no generic Python/console MCP tool or UI command entry; the project tool owns
the fixed Editor Python invocation. Its success proves operator access only.
The cold and warm traversal receipts from this discovery run are **FAIL**.

Evidence under `Saved/Migration/CalystoDungeonDirectorV7/Resume_20260908/`:
`NativeFixToolset0.txt` (four exact schemas), `NativeFixToolset1.txt` (PIE schema),
`McpNativeContext.json`, `McpArmNativeCold.json`, `McpArmNativeWarm.json`,
`McpLiveNativeWarmObservation.json`, and `LiveMcpNative/Exit.json` with its
`Editor.log` (shutdown dispatch, `CloseEditor`, final exit). The
[durable checkpoint](../../../../Docs/Migration/Evidence/Calysto_Director_V7_Acceptance.json)
records their hashes and scope.

### V7 replay seeds for known native topology

For the transient native-parity Style `43414C59-5354-4F4E-4154-495645535459`,
Floor 1, reroll 0 and attempt 0, launch through the protected wrapper with
`-CalystoDirectorNativeParity -CalystoDirectorRunSeed=<signed_int64>`:

| V7 run seed | First topology seed | Evidence scope |
| --- | --- | --- |
| `1219803037753221267` | `1779679224` | Confirmed in actual diagnostics in `Traversal_20260908_LightObservationSeed177/traversal.json` and its `Warm` counterpart; both traversal receipts FAIL. |
| `-3003287693235273058` | `1190737158` | Mathematically derived; live topology confirmation and traversal PENDING. |

The derivation is `Resume_20260908/DeriveNativeReplaySeeds.py` under the evidence
root. These mappings depend on that Style/floor/reroll/attempt identity. The old
V6 run seed `2959332854660340481` instead produces V7 topology `2068023713`;
copying a historical run seed does not preserve topology across random domains.

## Establish the current authority

Inspect `Config/DefaultGame.ini`,
`Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Calysto/EFCalystoDungeonHarnessSettings.cpp`
and the loaded asset/class. The inventory baseline is:

| Scope | Current V6 | Required V7 |
|---|---|---|
| Asset | `/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy` | `/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector` |
| Asset class | `/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset` | `UEFCalystoDungeonDirectorAsset` |
| Runtime owner | `/Script/EFProceduralRuntime.EFCalystoDungeonSubsystem`, returning V6 types | Stable public APIs and independent unversioned V7 types |
| Acceptance | Existing receipts prove only their exact V6 gate | Current-tree traversal, probability, visual, cook and package receipts |

The target interface is a requirement, not evidence that it is compiled or
loaded. Check class, asset, Config and consumers together.

## Current reflected runtime controls

Source of truth:
`Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDungeonSubsystem.h`.

| Operation | Existing API | Use and limitation |
|---|---|---|
| Observe | `GetSnapshot`, `GetResolvedFloorIntent`, `GetResolvedFloorPlanV6`, `GetRoomManifestV6`, `GetRealizedFloorManifest`, `GetRunEcology` | Read the exact active game-instance owner and preserve the returned identity. |
| Observe identity | `HasActiveRun`, `GetCurrentFloor`, `GetRunSeed`, `GetRunEpoch`, `IsTravelRequestPending` | Check before issuing another interaction or request. |
| Queue next intent | `GetNextFloorDirectorIntent`, `SetNextFloorDirectorIntent`, `ClearNextFloorDirectorIntent` | V6 typed intent; establish current fields before editing. |
| New run | `RequestStartNewRun`, `RequestStartNewRunWithSeed(int64)` | Starts/restarts a run; does not advance the current floor. |
| Progress | `RequestAdvanceFloor` | Useful technical control; direct invocation does not prove real floor-door interaction. |
| Replay/reroll | `RequestReplayCurrentFloor`, `RequestRerollCurrentFloor` | Different identity contracts; record run/floor/serial/seeds before and after. |
| Debug floor | `RequestTravelToFloor(int64)` | Development control; three direct floor jumps are not consecutive traversal proof. |
| Gameplay bridges | `SubmitFloorOutcome`, `SubmitCompanionRunSnapshot` | Real transaction inputs, not test shortcuts for making an invalid floor pass. |
| Style preview | `SampleDirectorRolls(int32 SampleCount=1000)` | Development-only Style counts; see probability limits below. |

`NotifyFloorReady`, `NotifyGenerationFailed`, population-realization and
companion-ready methods are internal runtime callbacks. Never invoke them to
manufacture readiness or bypass verification. This header has no public
`RequestRetry`, `RequestReturnToHub` or cancellation operation in the inventory
baseline; discover an implemented replacement before using those names.

Python discovery is demonstrated in
`Tools/Migration/Validate-CalystoDungeonDirectorV6TraversalPIE58.py`, functions
`find_subsystem`, `director_for_world`, `reflected` and `audit_ready_floor`.
It gets the game instance from the world, resolves that exact subsystem class,
and invokes reflected names such as `get_snapshot` and
`request_start_new_run_with_seed`. This file is an executable smoke harness;
do not import it merely to reuse a utility while the user's editor is open.

Snapshot flags alone are insufficient evidence: current V6 `BuildSnapshot`
sets PCG/nav-ready from travel state `Ready`. Inspect actual structural
components, nav data and route postconditions separately.

## Development Harness and failure injection

`L > Dungeon Harness` delegates through
`Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Debug/ProjectGameplayDebugCommandExecutor.cpp`:

- `RefreshDungeonHarnessStatus`, `RequestAdvanceDungeonFloor`,
  `RequestReplayDungeonFloor`, `RequestRerollDungeonFloor` and direct floor travel.
- `RequestStartNewDungeonRun`; `RequestStartDungeonTestRun` always uses seed 42.
- `SetDungeonHarnessPreferredStyle`, `SetDungeonHarnessIntentBias`,
  `SetDungeonHarnessIntentVolatility`, `ClearDungeonHarnessDirectorIntent`.

These C++ executors are not general registered console commands. Read the
actual Harness option before activating it; the preferred Style is an intent
input, not permission to replace the selected Style during recovery.

One exact existing Calysto console hook is
`EF.Calysto.Automation.SuppressStartPointOnce`, registered in
`Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/EFProceduralPCGSubsystem.cpp`.
It accepts no arguments and is restricted to unattended PIE. Use only in an
explicit injected-failure gate. It is not a retry/repair command.

## Existing V6 test commands

Run from `D:/Projects UE5/NoShellForWinter`. Select timing and escalation from
[validation-gates.md](validation-gates.md). Preserve dirty editor work before
an authorized close/restart; none of these commands authorizes losing unsaved
packages. Use fresh stamps/tags. A receipt plus process exit 0 and clean logs
is required; a timeout is FAIL or PENDING according to the observed gate.

| Script under `Tools/Migration/` | Parameters and constraints |
|---|---|
| `Run-CalystoDungeonDirectorV6NativeAutomation.ps1` | `ProjectRoot`, `Stamp`, `ExpectedTestCount=38`. Exact pinned test inventory; no filter or timeout parameter. Requires zero pre-existing UnrealEditor processes. NullRHI. |
| `Run-CalystoDungeonDirectorV6BlueprintCompile58.ps1` | `ProjectRoot`, `Stamp`, `EditorTimeoutSeconds=720` (60-1200). Requires current cook-closure receipt. NullRHI; watchdog exists. |
| `Run-CalystoDungeonDirectorV6TraversalPIE58.ps1` | `RunSeed`, `Stamp`, required `BlueprintCompileSummary`, `EditorTimeoutSeconds=720` (60-1200). Requires current matching cook closure and Blueprint PASS. No floor-count parameter; NullRHI. |
| `Run-CalystoDungeonDirectorV6PackagedSmoke58.ps1` | Required `Configuration` and `ArchiveRoot`; `ValidationMode=FinalStrict`, `Scenario=Natural`, `RunSeed`, `MaxFloor=10`, `TimeoutSeconds=600`, `ForcedDungeonEdge=0`, `CaptureVisual`, `RunTag`, `ExpectShippingRejection`, output/log overrides. |
| `Run-CalystoDungeonDirectorV6PackagedDeterminism58.ps1` | Required configuration/archive; `RunSeed`, `MaxFloor=10`, `TimeoutSeconds=600`, `PairTag`, validation mode. Runs two sequential Natural smoke processes and compares canonical deterministic fields. |

The native runner calls protected launcher `-Wait` without a watchdog. Do not
start it without bounded supervision of the owned process. Its pinned count
cannot be increased or reduced to hide missing tests. Blueprint and PIE
watchdogs target the exact project, but timeout cleanup forcibly terminates
their owned validation editor; never attach them to an existing dirty editor.

The existing PIE test checks only `door_entry_probe`, then a separate
`seeded_floor_1`. Python limits are 600 s global and 180 s per phase. It cannot
prove three consecutive floors, and NullRHI cannot prove visual appearance.

### Existing three-floor packaged smoke

After building a matching V6 package, use an explicit short smoke rather than
the ten-floor defaults. `$calystoArchive` must be the exact fresh package
archive being validated:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
& .\Tools\Migration\Run-CalystoDungeonDirectorV6PackagedSmoke58.ps1 `
    -Configuration Development -ArchiveRoot $calystoArchive `
    -Scenario Natural -RunSeed 1779679224 -MaxFloor 3 `
    -TimeoutSeconds 180 -CaptureVisual
```

`MaxFloor` accepts 1-100; timeout accepts 60-1800. The runner's external timeout
adds 120 s to the supplied runtime timeout. Shipping permits only Natural and
ForcedDungeonEdge=0, except an explicit `ExpectShippingRejection` test. Other
scenarios require one floor. Never call this V7 proof.

The packaged subsystem first interacts with real HUB DoorToLevel, accepts an
entry probe, then starts a **separate** fixed-seed run. It checks three seeded
Ready floors and two actual ACF floor-door interactions for MaxFloor=3. It
positions the player near each door; this does not prove traversing the route
by locomotion. On the final floor it selects the door and exits without
interacting, so intended final HUB return is an additional gate.

The runner renders offscreen. `CaptureVisual` produces one final-floor image;
inspect the image, and collect additional Floor/Wall/Roof and boundary views
for the full material gate. A nonempty PNG alone is not visual QA.

Implementation reference:
`Plugins/EFProcedural/Source/EFProceduralACFURuntime/Private/Calysto/EFCalystoPackagedSmokeSubsystem.cpp`,
`HandleBootstrapTick`, `TrySelectDoor`, `HandleFloorReady`,
`HandleDoorSelectionTick`, `HandleTimeoutTick`, `WriteReceipt`.

## Historical V6 probability suite

The existing native automation prefix is
`NoShellForWinter.CalystoDungeon.V6`. The 38 pinned tests include
`Policy.RoomThemeStatistics100K`, `Policy.StableRoomManifest`,
`PCG.ProbabilityLanesAreIndependentAndOrderStable`,
`Runtime.Subsystem.ThemeWeightIsolation`, and population order, overlay,
budget and isolation fixtures. These tests are useful partial evidence.

`EFCalystoDungeonV6PolicyTests.cpp` under EFProceduralEditor runs 100,000
**in-memory room decisions**, not generated floors. `RoomThemeStatistics100K`
uses Standard with fixed seed, checks marginal 25% presence and conditional
Forge 62.5% / Shrine 37.5% with six-sigma bounds, then protected-room cases.
`StableRoomManifest` separately checks one ranked guaranteed room, independent
25% on remaining eligible rooms and reversed input order. It does not measure
the full guaranteed-floor count distribution over 100,000 trials.

`SampleDirectorRolls` in `EFCalystoDungeonSubsystem.cpp` samples only Style
selection over seeds 1..count. It caps work at 100,000, skips failed plans, prints
the requested count even when capped, and returns empty output in Shipping.
Treat it as a preview, not statistical acceptance.

Required V7 probability coverage includes conditional amounts, populated rarity
including Winter, exact curve endpoints, guaranteed-floor mathematics,
entry/profile reorder and rename, impossible feasibility/budgets, and zero
influence with adaptation disabled. Existing Theme or Style previews cannot
satisfy those gates. The V7 runner must report requested, feasible and accepted
distributions separately and exercise the production decision library.

## Protected build, launch and package wrappers

Use `Tools/Migration/Build-NoShellForWinterEditor58.ps1` for Editor, and
`Build-NoShellForWinterGame58.ps1 -Configuration Development|Shipping` for Game.
Use `Launch-NoShellForWinterEditor58.ps1` for launches. Both Daz plugins must be
enabled in restored descriptors, receipts and the running editor.

The Game build wrapper temporarily changes the project descriptor during UBT
and restores byte-exact in `finally`. Force-killing that wrapper can skip
restoration. Supervise progress, and verify descriptor plus Daz receipts before
launching after interruption. Never launch an editor during this UBT window.

Launcher `-PythonScript` accepts a target-local file and
`-PythonTimeoutSeconds` 30-300 (default 300); it uses NullRHI and refuses existing
UnrealEditor processes. Its current GUI-process branch tolerates an unavailable
exit code. Obtain independent concrete exit evidence or improve the wrapper
before accepting a gate requiring process exit 0. Plain `-Wait` is unbounded.
`Run-CalystoV6InlineAuthoring58.ps1` also uses unbounded launcher `-Wait`.

`Package-NoShellForWinter58.ps1` accepts `ProjectRoot`, `EngineRoot`,
`Configuration` and `ArchiveRoot`; it performs actual cook/stage/package work
and runs the clothing catalog compiler first. The inventory version explicitly
references V6 source, assets and authoring/cook-closure receipts. Migrate this
wrapper for V7 rather than bypassing its closure checks.

`Invoke-NoShellForWinterPackageValidation58.ps1` is a broad release orchestrator:
Natural=10, extra one-floor Development fixtures, and two more ten-floor
determinism runs with 600 s per smoke. It removes its own temporary archive
after all checks PASS. It is unsuitable as the routine three-floor command;
use explicit individual runners while building the concise V7 release binding.

## Required V7 operator capabilities

Current candidate inspection additions (authoring/diagnostics only):

- `UEFCalystoDungeonDirectorAsset::GetAuthoringErrors()` returns exact field
  errors directly, including in Python. `validate_authoring()` uses Unreal's
  bool-success/output-array convention; do not assume it returns a tuple.
- `Import-CalystoDungeonDirector58.py` loaded through `runpy.run_path` supports
  `main(stage_only=True)`. It validates a transient native candidate and every
  mapped value without saving. Read `ImportDirectorStaged.json`; this does not
  create the master asset, complete unsupported mappings or switch authority.
- To capture native generated instance evidence before rollback, set
  `builtins.CALYSTO_DIRECTOR_STRUCTURE_OUTPUT` to a new target Saved directory
  and execute `Tools/Migration/Observe-CalystoDirectorStructure58.py` before PIE.
  It is read-only, unregisters on a terminal first-floor state or after 60 seconds,
  bounds snapshots/actors/components/instances, and records its own overhead.
  Pair it with the real-door traversal harness; it cannot accept a floor.
- Director diagnostics distinguish `root_generation_requests` (transaction
  authorization) from retained `actual_root_generation_requests`. The September 8
  warm observation records actual calls of one; absent measurements remain null.
  Neither count alone proves an accepted floor.
- Shared async preparation and per-request visual loading now have separate
  measured durations and dependency counts. They do not measure PSO readiness,
  geometry, navigation, gameplay spawning, or total Door-to-FloorReady latency.
- Critical pending loads temporarily request a shared 5 ms async loading budget;
  owner release restores the original value and priority, and external writes
  retain precedence. `async_loading_budget_ms` reports the effective value.
  This scope does not save settings, enable async loading threads or synchronously
  flush loading. Its ownership test and live restoration have passed.
- Arm the traversal through the verified project MCP operation above. Its fresh
  `ARMED` receipt must precede immediate StartPIE. Stop an accidentally unarmed
  owned PIE session promptly; it supplies no traversal evidence.
- UE 5.8 Python exposes `CollisionChannel.ECC_PAWN`; capsule overlap returns an
  array or None. Native `BreakTaggedData` is not exposed. Use
  `PCGDataFunctionLibrary.get_typed_inputs(collection)` for matching data/tag arrays.
  For zero-height native ISM instances, `GetInstanceTransform` loses rotation
  during singular-matrix decomposition. Exact provenance must compare the stored
  instance matrix in component world space, not ignore its orientation.
- Python `HitResult.to_tuple()` executes UE's native break function and supplies
  the 18-value UE 5.8 contract; `GameplayStatics.break_hit_result` is not exported.
  Calling a NavigationSystem class-static Python method dispatches through its
  Within=World CDO and can trigger an ensure. The tested traversal inspects at most
  64 loaded NavigationSystemV1 objects, requires the exact PIE world outer, and
  invokes `FindPathToLocationSynchronously` through that instance's `call_method`.
  This is one independent QA query after runtime acceptance, never a rebuild.
- `Traversal_20260908_WorldNavTrilogySeed177` completed three real-door floors and
  short grounded entry walks in 17.985 seconds. `Traversal_20260908_RouteSeed119`
  completed full protected-route walks and the same three-floor flow in 77.594
  seconds, with first topology 1190737158 from run -3003287693235273058.
  The harness now requires each full walk within 35 seconds and its first 75 cm
  within five seconds, inside the existing 150-second total. It uses normal
  character input and verifies the actual movement-component floor contact.
  All three floor-door approach interactions remain explicitly recorded as
  automated positioning followed by the real ACF interaction contract.
  These native fixture receipts do not imply full gameplay, final floor-cap
  return or release acceptance. The fixture's unlit images were black; visual
  QA remains PENDING. Do not accept PNG existence or a receipt alone as visual QA.
- The Player-only project Recast subclass uses explicit config resolutions
  Low 20 / Default 10 / High 10 cm, retaining radius 50 / height 176 cm and the
  native Default agent's 19 cm resolution. Both failing topologies now have
  live complete routes. Runtime tile completion is observed through the owned
  NavigationSystem generation-finished delegate, including late data registration;
  `Recast.OnNavMeshUpdate` alone does not report ordinary tile completion.

Implement and validate these against the unversioned production interfaces
before adding executable V7 commands to this cookbook:

- Read authoring errors, compiled configuration, request/attempt identities,
  state, remaining shared deadline, reservation manifest and realized result.
- Request seeded new run, replay, reroll, advance, cancellation, explicit retry
  and explicit HUB return, without bypassing real-door acceptance gates.
- Inspect/simulate production probabilities on demand in Editor and Development
  Harness; export the current seed/configuration and complete receipt.
- Run three consecutive real-door floors in one run, capture actual structural,
  nav, material and placement evidence, and preserve a concise error on failure.
- Inject supported failures before commitment and verify whole-attempt rollback,
  delayed Pending, stale callback rejection and exhaustion without HUB bounce.
- Run bounded probability, Blueprint, cook/package and retention gates using
  actual registered tests and current manifest schemas.

Current V6 recovery is one frozen retry followed by source-world/HUB recovery;
it does not implement V7's four attempts, deterministic reseeding and one
30-second request deadline. Documentation or a new skill cannot change that
runtime behavior. Bind each V7 capability only after its behavior is proven.
