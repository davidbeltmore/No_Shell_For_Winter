# Calysto Dungeon Director V7 — Terra Ultra closure plan

Date: 2026-09-12. Status: EXECUTION PLAN; no new gameplay or release PASS.

This is the execution order for finishing [the complete V7 contract](Calysto_Dungeon_Director_V7.md).
It supersedes earlier model preferences and work ordering, not product scope,
protected boundaries, the two release gates, or acceptance requirements.
The immediate user-reported defect is an unexpected return to HUB through the
normal dungeon entrance. Fixing that experience takes priority over additional
isolated geometry experiments or documentation polish.

## 1. Model, ownership, and continuation

- Requested lead: **GPT-5.6 Terra, Ultra, Standard speed**, using included
  subscription access only. No API key, paid overage, purchases, or model upgrades.
  Select and verify the actual model in the client. Writing this plan or changing
  a project document does not switch an already running lead model.
- One lead owns the Editor, builds, assets, PIE, packages, and acceptance ledger.
  At most one auxiliary, also Terra, handles one independent bounded task.
  No recursive delegation, duplicate investigation, or simultaneous source edits
  to the same files. Use the auxiliary only when its result unblocks integration.
- Retain the existing persistent goal. Do not create competing goals, an external
  paid agent loop, or repeated fresh conversations that discard working context.
  Checkpoints continue directly into the next task without approval requests.
- Read the Unreal MCP skill, Calysto skill, this plan, current checkpoint, and
  the exact relevant source first. Do not repeatedly reread the complete archive.
- Each work cycle has one observable failure, one change set, and one closing
  test. Start with a 10-minute targeted investigation window, then implement or
  explicitly change the diagnostic hypothesis. A 45–60-minute checkpoint is an
  accountability point, not a claim that the subsystem must be solved in an hour.
- After two identical failures without new evidence, stop that experiment,
  inspect the actual failed component/caller, and choose a different diagnostic.
  Continue useful authorized work; do not keep relaunching the same world.
- Keep output compact: current user-visible result, first blocker, next action.
  Store full logs on disk. During long tools, observe every 10–30 seconds and
  communicate at least every 60 seconds.

OpenAI documents Terra as a balanced model, Ultra as supporting delegated work,
and higher reasoning as potentially increasing time and token use. This plan
therefore constrains work size and ownership; it does not promise Terra will
finish faster or within a particular night. See [Models](https://learn.chatgpt.com/docs/models?surface=app).
The checkpoint loop follows [Follow a goal](https://learn.chatgpt.com/use-cases/follow-goals).

## 2. Grounded starting point

Read-only inspection on 2026-09-12 established:

- `UEFCalystoDirectorSettings::bEnabled` defaults to false. `IsEnabled()` also
  accepts `CalystoDirectorCandidate` and `CalystoDirectorNativeParity` only
  outside Shipping.
- `Config/DefaultGame.ini` still points at the V6 Director policy and always-cook
  registration. The running target Editor command line has no candidate/parity flag.
- The legacy `UEFCalystoDungeonSubsystem::NotifyGenerationFailed()` calls
  `ReturnFromFailedDungeon()` after its bounded recovery fails.
- These facts make active-authority mismatch a strong first hypothesis for the
  reported HUB return. They do not establish the actual current call stack,
  effective loaded settings, or exclude saved configuration overrides.
- A target UE 5.8 Editor was running. The initial MCP connection failed; live
  authority and the current failure remain PENDING. Do not claim a reproduction.
- Earlier real-door three-floor traversal used a reduced native fixture.
  Architecture traversal later walked Floor 1 and reached Floor 2, where the
  fixture rejected empty architecture. These are not complete master-asset passes.
- `Native_NativeSeams_20260908_2248/StrictSummary.json` records three focused
  architecture tests passing in 49.047 seconds with actual launcher exit 0.
  Original Forge support across adjacent native floor tiles is tested, including
  a real 1 mm gap, missing tile, obstacle, and reordered instance storage.
  The changed coverage still needs actual three-floor gameplay validation.
- The unversioned master asset exists. Earlier migration recorded 2,913 field
  decisions and resolved the original 68 gaps. Reconcile that evidence; do not
  redo migration or assume all migrated controls have runtime consumers.
- Native gameplay snapshot work exists, but equipped-item travel and complete
  gameplay publication remain unfinished. Enemy/FloorLocal NPC bridge drafts
  remain under `Saved/.../Resume_20260908/GameplayBridgeDraft`; a draft is not a
  compiled or registered runtime service.
- The original ledger has 31 criteria and zero fully closed final-scope criteria.
  Historical protected discrepancies are 145; preserve their identities and
  baseline separately from any new discrepancy. Recheck rather than rebaseline.

Starting files:

- `Plugins/EFProcedural/Source/EFProceduralRuntime/{Public,Private}/Calysto/EFCalystoDirectorSettings.*`
- `Plugins/EFLevelFlow/Source/EFLevelFlowRuntime/Private/EFLevelFlowSubsystem.cpp`
- `Plugins/EFProcedural/Source/EFProceduralRuntime/Private/Calysto/EFCalystoDungeonSubsystem.cpp`
- `Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/Calysto/EFCalystoDirectorPCGSubsystem.cpp`
- `Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/Calysto/EFCalystoSurfaceCandidates.cpp`
- `Tools/Migration/Validate-CalystoDirectorTraversal58.py`
- `Docs/Migration/Evidence/Calysto_Director_V7_Acceptance.json`

## 3. Ordered closure tickets

Every ticket has states PENDING, ACTIVE, FAIL, PASS, its exact source/build/asset
scope, and linked evidence. Integrate and close a ticket before expanding its
implementation. A later dependency change reopens affected evidence only.

### C0 — Establish the actual normal launch and preserve work

1. Record current Git HEAD/dirty paths and take a recoverable source/config/tool
   snapshot. Preserve all existing user edits; do not reset, bulk stage, or clean.
2. Identify the live Editor PID, project, binary receipt, launch arguments, map,
   dirty packages, effective Director setting and loaded master/policy object.
   Reconnect MCP with at most two 10-second calls; diagnose the server if absent.
   Preserve unsaved work before an authorized protected restart.
3. Compare protected hashes against both historical baseline and last task snapshot.
4. Reconcile outstanding source/drafts once. Compile coherent source using the
   protected wrapper if it differs from the loaded binary; retain Daz receipt repair.

**Exit:** source/build/asset/launch identity is recorded, process ownership is
current, and the exact authority used by an ordinary entrance is known.
Time budget for initial reconciliation: 15 minutes before reporting concrete gaps.

### C1 — Close the unexpected HUB return at the real entrance

1. Reproduce once through the actual HUB `DoorToLevel`, with ordinary launch
   arguments and authored configuration. Record the first generation failure and
   the exact caller/reason that requests HUB travel. Inspect the Blueprint travel
   contract and project-owned travel consumers; do not infer it from old V7 tests.
2. Distinguish four paths: requested HUB return, intended final-floor completion,
   generation-failure recovery, and unrelated gameplay travel. Only the third
   must lose automatic HUB travel. Do not globally block legitimate travel.
3. Correct the project-owned active failure integration: keep the player protected,
   end the loading wait within its shared deadline, and offer English **Retry**
   and **Return to HUB**. The latter requires the actual user action. Retry must
   preserve run/floor/rules/state and use the coordinator's bounded ownership.
4. Establish a single explicit candidate route through the same real entrance.
   Do not merely flip `bEnabled` against an unsupported master, return to V6 after
   a V7 failure, or label error containment as a playable dungeon.
5. Run one injected exhaustion case and one explicit return action; verify no
   hidden map travel, infinite loading, duplicate retry, or player exposure.

**Exit:** the reported failure is explained by current runtime evidence, its
automatic HUB return is removed, and failure UI/actions work through the same
entry path the user uses. A successfully contained failure is still FAIL for
floor generation. Avoid developing another V6 Director or modifying its source asset.

### C2 — Close native traversal with architecture

1. Continue from the already compiled floor-seam correction; do not restart the
   geometry algorithm. Run the three-floor architecture fixture once.
2. On failure, inspect its captured exact structural geometry and first failed
   postcondition. Check native Start/End ownership, floor collision/capsule,
   geometry-derived registered bounds, actual Player nav data, current complete
   route, final Pending observer, and exactly one root GenerateLocal per attempt.
3. Preserve original slot transforms, full supported variation, room/door/route
   protection, real surface coverage, and verified materialized instance counts.
   Never satisfy the test by disabling architecture or removing its nonempty check.
4. Cover both known topology seeds 1779679224 and 1190737158, using the recorded
   run-to-topology mapping rather than assuming these are run seeds.
5. Compare a grounded native Calysto reference with the adapter's structural
   output with extensions disabled. Two Director fixtures are not native parity.

**Exit:** HUB → Floor 1 → Floor 2 → Floor 3 in one run through real doors,
full route walking and actual reserved architecture on every floor, plus native
structural parity. Label this reduced-fixture gate explicitly; it cannot close C7.

### C3 — Close real gameplay ownership and travel

1. Review and integrate existing bridge drafts with the real module/service
   registration. Replace placeholder pre-floor snapshot hashes. Capture actual
   state before `BeforeTravel`, teardown, or any persistent outcome mutation.
2. Wire existing eligibility/reservation planner → frozen manifest → materializer
   → verification → commit → player release. Unsupported roles must be implemented
   before enabling their authored entries; preflight rejection is not completion.
3. First close native enemies and support NPCs with typed levels/archetypes/genders,
   dormant controllers and exact accepted/rejected cleanup. Then close recruited
   companions, inventory handoff, death/corpse state, graveyard and Winter's Recall.
4. Complete cross-world item/fragment identity and equipped-item/actor/effect
   reconstruction through the owning inventory systems. Snapshot flags alone are
   insufficient. Verify player, companion and corpse inventories without duplicate events.
5. Complete armor, loose loot, food, drinks, chest variants, lockpicking and contents.
   Close the actual actor lifecycle and native owner contract for each typed role.
6. Publish persistent outcomes once; rejection restores pre-floor state. Accepted
   cleanup cannot run rejected rollback or replay inventory/companion events.

**Exit:** one real master-content floor is fully realized and playable; fixtures
prove whole-attempt rollback and actual travel persistence. Each supported role
has an owner, consumer and observable effect. No unregistered draft bridge remains.

### C4 — Close authoring, probability, Themes, and native features

1. Reuse the saved master and migration report. Resolve only remaining behavioral
   gaps. For each editable setting record property → runtime consumer → observable
   effect → test. Remove misleading no-effect controls without removing required functionality.
2. Complete native Details categories, percentages, named entries, conditional
   fields, hidden persistent IDs, exact errors, pickers/search, Undo/Redo, duplicate
   identity regeneration and reset. Keep diagnostics in the on-demand inspector.
3. Complete supported structural variants, ramps/door frames/doorways, eight native
   zones, mesh/actor/baked PCG/level-instance payloads, transforms, rotations,
   variation, lighting and intentional optional Empty outcomes. Current explicit
   capability rejections are unfinished work, not permission to drop these features.
4. Verify one Style and the guaranteed Theme law, protected-room exclusion and
   rejection of no-eligible-room topology. Preserve independent identities/domains.
5. Close Inherit/Extend/Replace/Block, explicit overrides, all global budgets,
   feasible conditional counts, populated rarity including Winter, cooldowns,
   repetition, drought, quotas, run outcomes, ecology and bounded advanced modifiers.
   Adaptation initially disabled must have exactly zero mathematical influence.
6. Run the existing production C++ probability suite with at least 100,000 trials
   per relevant law, independent oracle, predetermined tolerances and exact edge
   cases. Add only missing assertions; never simulate 100,000 worlds or rerun a
   statistical failure until it happens to pass.
7. Inspect actual Floor/Wall/Roof assignments and shared boundaries: Style grey,
   Forge orange compatible with instancing/Nanite, Shrine blue. No silent material
   fallback, empty explicit override or per-room MID. Inspect usable lighting,
   native torches and the 2 cm Wall Middle variation.

**Exit:** every required authored control has its actual runtime effect and test;
probability and visual evidence are tied to the current compiled master candidate.

### C5 — Close recovery, decals, PCG and loading

1. Finish the transaction's Pending/spatial/configuration/resource/cancelled
   classifications, four total attempts and single 30-second request deadline.
   Verify deterministic reseeds and full cleanup before another attempt starts.
2. Finish the decal producer and exact selected realization: one pool owner, 24
   capacity, 8 active, one per room, Floor/Wall/Roof limits 3/4/1, Style 10%, Forge
   replacement 25%, Shrine blocked, protected route/door/progression exclusion.
   Close real distance culling and honestly named fading; no per-stain actor/MID.
3. Close custom PCG documented inputs/outputs, deterministic proposals, footprints,
   dependencies and supported surfaces. Preflight rejects recursive generation,
   topology/progression mutation or untracked spawning before executing them.
4. Close phased reachable/selected dependency loads, deduplication, idempotence,
   leases, stale callbacks, cancellation and PSO/native torch preparation.
5. Record actual loading, geometry, navigation, shader and realization timings,
   including rejected attempts and cleanup in Door-to-FloorReady latency.

**Exit:** all required advanced features are connected, the recovery UI is backed
by strict transactional behavior, and selected content cannot silently disappear.

### C6 — Make the complete candidate work in an ordinary launch

1. Complete C3–C5 before removing temporary capability exclusions. Resolve actual
   configuration/resource failures instead of substituting the reduced fixture.
2. Set the intended candidate authority and master path through reviewed project
   integration configuration. Launch the target through the protected launcher
   **without candidate/parity/fixture flags** and verify effective settings.
3. Run HUB → Floor 1 → Floor 2 → Floor 3 through real doors with all initial authored
   functionality enabled. Verify actual movement, themes/materials, placement,
   inventory, companions, containers and normal player control.
4. Verify the intended final-floor return naturally. An explicit harness return
   command is a separate action test and does not prove progression completion.
5. Relaunch once and repeat the ordinary entrance; a transient working session
   cannot establish persistent configuration correctness.

**Exit:** the experience the user opens is the complete V7 candidate and actually
works. No flags, alternate asset or harness acceptance bypass are required.

### C7 — Close the complete candidate release gate

Use one compact coverage matrix to combine relevant cases instead of a Cartesian
product of seeds, sizes, depths and Styles. Record exact cases and omissions.

- Both historical failing seeds, every initial Style, cold/warm runs and supported
  size/depth extremes; replay, reroll, same-seed restart and normal final HUB return.
- Gameplay persistence and failure fixtures, initially at most 12 targeted floor
  failure requests; delayed geometry/nav remains Pending, cancellation, repeated
  interaction, late callbacks, missing resources, realization failure and exhaustion.
- Actual visual review of all surfaces, boundaries, placement zones, lighting,
  decals, protected routes and readable native authoring.
- One 25-consecutive-floor retention process after stabilization. Count actors,
  components, references, instances and leases, including rejected attempts.
  Separate cache warmup from persistent growth; do not run a 1,000-floor soak.
- Record hardware, cold/warm shader/nav/hitch/memory measurements and measured
  P95 with method/sample count. Target P95 <30 seconds; do not claim robust P95
  from three floors or a percentage improvement over broken V6.
- Cold Editor, Development and Shipping builds; affected Blueprint compilation;
  actual fresh cook and packages for both game configurations; packaged traversal
  and visual QA with successful process exits and pertinent clean logs.

**Exit:** complete candidate acceptance, with V6 assets still immutable migration
sources and no active runtime fallback. A report followed by a crash fails.

### C8 — Retire exact legacy authorities

1. Audit Asset Registry referencers, code/config/tools, generated manifests,
   cook lists and package dependencies for Calysto V3–V6 authorities.
2. Migrate all remaining consumers; remove exact obsolete classes/wrappers/tools
   and temporary redirects. Delete exact obsolete assets only through Unreal Editor.
3. Keep useful unversioned internal resources, compatibility PCG and native torches.
   Do not match unrelated version numbers such as EFClothingMorph V4 for deletion.
4. Preserve historical evidence outside runtime/cook and compare protected hashes.

**Exit:** V7 is the sole active authoring/runtime/package authority and no required
or protected resource was removed. No bulk content or generated-directory deletion.

### C9 — Close final V7-only release and operational skill

1. From the final tree, repeat required Editor/Development/Shipping builds,
   affected Blueprints, fresh cook/packages and actual traversal/visual gates.
   Reuse other evidence only when its complete dependency scope is unchanged.
2. Finish the existing skill and Harness/MCP commands from executed operations:
   validate/edit/inspect, simulate probability, start/observe/reproduce/reroll,
   cancel/retry/return/export, timeouts, artifacts and Editor recovery. Preserve
   Undo/Redo and keep administrative controls out of Shipping.
3. Close all 31 original criteria with source-bound receipts and package paths.
   Recheck protected invariants and document the historical discrepancies separately.
4. Provide exact launch instructions and tested package locations. Mark the goal
   complete only when the technical definition of done is actually demonstrated.
   RealisticBlood entitlement remains a separate external distribution gate until
   confirmed; it does not excuse unfinished technical work or authorize distribution.

**Exit:** complete V7-only delivery with ordinary real entrance, three consecutive
floors, all functions, probability, visuals, recovery and both final packages proven.

## 4. Test selection and time limits

Run only affected tests during C0–C6; keep both C7 and C9 release validations.
Reuse the existing protected scripts and native supervisor instead of writing
another test framework. Healthy builds/cooks may use their full bounded budget;
after 180 seconds without genuine progress diagnose the process and preserve Daz
receipt restoration. A budget expiry is evidence, never a PASS or scope reduction.

| Operation | Hard bound / policy |
|---|---|
| MCP | 10 seconds per call; at most 2 connection attempts |
| Snapshot / protected hashes | 180 seconds; compare exact historical discrepancies |
| Editor startup | 180 seconds; establish loaded target/context |
| Focused native probability/geometry/gameplay batch | 180 seconds; combine affected tests in one process |
| Blueprint/focused subsystem checks | 300 seconds |
| One floor request, all retries and cleanup | 30 seconds shared |
| Three real-door PIE floors | 150 seconds harness |
| Screenshot | 15 seconds per meaningful view; actually inspect it |
| 25-floor retention | 900 seconds, once stable; repeat only if invalidated |
| Each Editor/Development/Shipping build | 900 seconds |
| Each fresh cook/package | 2,400 seconds |
| Three packaged floors | 180 seconds plus bounded startup |
| Owned process cleanup | 60 seconds grace; preserve unsaved work/receipt repair |

Grounded entry points:

- `Tools/Migration/Build-NoShellForWinterEditor58.ps1`
- `Tools/Migration/Launch-NoShellForWinterEditor58.ps1`
- `Tools/Migration/Build-NoShellForWinterGame58.ps1`
- `Tools/Migration/Run-CalystoDirectorNative58.ps1` with an exact reviewed test inventory
- `.agents/skills/calysto-dungeon-master/scripts/Invoke-UnrealMcpMetaTool.ps1`
- MCP `EFProceduralEditor.EFCalystoDirectorToolset`: discover then describe before
  calling current `ReadEditorContext`, `ReadDirectorDiagnostics`, `ArmTraversal`,
  and supported authoring/export/shutdown operations. Do not assume stale sessions.

At most one normal reproduction per diagnostic hypothesis and one verification
after its identified correction. Do not repeat unrelated passing tests after
every edit, rebuild all configurations before integration, or produce a new
framework to measure work already covered by current runners.

## 5. Progress and evidence

Keep the original 31-criterion denominator unchanged. Report two separately named
measurements so intermediate work is visible without claiming final acceptance:

- **Operational closure:** closed C0–C9 tickets / 10 × 100. This is an unweighted
  checklist, not estimated coding effort, remaining time, or final acceptance.
  Freeze these ticket boundaries before execution and give no credit for partial tickets.
- **Final contract acceptance:** complete verified original criteria / 31 × 100.
  Keep the existing immutable IDs/text and required candidate/final scope.

For each criterion, attach the actual existing partial evidence and concrete
remaining gap rather than leaving generic "not verified" text. Maintain candidate
and final evidence scopes separately; do not reset valid foundational knowledge
whenever an unrelated file changes. A regression reopens the affected ticket.

Checkpoint format: model actually selected; closed tickets and original criteria;
current ordinary-entry result; latest demonstrated behavior; first blocker;
next exact action; owned process/deadline; source/build/asset hashes and artifacts.
Update at ticket closure or 45–60 minutes. Do not spend another run estimating
percentages, rewriting this plan, or archiving the same unchanged files.

## 6. Ready-to-use execution instruction

> Execute `Docs/Migration/Calysto_Director_V7_Terra_Closure_Plan.md` through C9 using
> GPT-5.6 Terra Ultra at Standard speed and included quota. Keep the existing goal.
> Start with the real ordinary-launch HUB bounce and establish its actual authority
> and travel caller. Preserve the complete original V7 contract. Integrate existing
> work, close each ticket with its stated test, and continue automatically. One lead
> owns Editor/build/assets; at most one bounded Terra auxiliary. Do not substitute
> native fixtures for the master asset, suppress failed tests, weaken requirements,
> switch to Astra, or stop at a successful compile. Report operational closure and
> final acceptance separately. Finish only after the ordinary V7-only game and both
> final packages satisfy the complete contract.
