# Director operator control cookbook

Use this reference for exact commands, runtime controls and their evidence limits.
The inventory was read from the target on 2026-09-05. Reinspect changed files and
the loaded class before using it. V6 commands below remain V6 even when a V7
candidate exists. Never rewrite a version in a command and assume it works.

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

## Probability suite: what actually runs

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
