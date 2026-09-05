# Calysto Dungeon Director V7 — Implementation Contract

Status: **DESIGN CONTRACT; V7 implementation, live gameplay, and release gates PENDING.**

This document records the authorized V7 product and engineering requirements. The
authorized delivery completes the operating skill and timed implementation
and test workflow first, then continues directly through V7 implementation and
all acceptance gates in the same run. This design document establishes no PASS
result by itself; measured evidence must demonstrate each completed milestone.
The existing V6 configuration remains the current migration input and is to be
preserved immutably while V7 is developed. V7 must never use V6 as a runtime
fallback or compile its configuration through V6 structures.

Use this contract with the project
[Calysto operating skill](../../.agents/skills/calysto-dungeon-master/SKILL.md).
The [validation gates and timing policy](../../.agents/skills/calysto-dungeon-master/references/validation-gates.md)
define practical step budgets, observation intervals, timeouts, evidence, and
the minimum useful test sequence. Those timings are operational budgets, not
evidence that any build or runtime stage already meets them. A timeout is a
recorded result; it never becomes permission to silently weaken a gate.

## 1. Objective and boundaries

Build a clean, probability-driven Director that enhances native Calysto rather
than replacing it. V7 becomes the sole authoring and runtime authority only
after successful migration and complete validation. Native Calysto continues
to own supported room, corridor, ramp, mesh, construction-slot, and placement
generation. `Plugins/EFProcedural` is the single project-owned master
integration.

All work occurs in `D:\Projects UE5\NoShellForWinter` using UE 5.8. The previous
project, `D:\Projects UE5\LustAsDeadlySin`, is read-only, including Git LFS
hydration and editor saves. Never launch or resave that source in UE 5.8. Use a
detached target-side copy if source hydration is required.

Preserve the target's ACF Ultimate 4.3.5, current DazToUnreal, Frederick,
`/Game/FullSample/Player.Player`, current Female assignment on Player, and
current Female, Male, and Multiple meshes. Never modify Marketplace or Engine
plugins, `BP_MassiveDungeon`, or Calysto vendor Data Assets. Never bulk-copy
Content, Config, Plugins, Saved, Intermediate, or Binaries; never overwrite
`/Game/FullSample` or Player wholesale. Use adapters, bridges, composition,
interfaces, subclasses, and project-owned compatibility code. Preserve public
asset paths and Blueprint contracts; use narrow Core Redirects only for
unavoidable compatible class/module renames.

Required project-owned plugins remain EFCharacterCreation,
EFCharacterCreationDazBridge, EFClothingMorph, EFProcedural, EFLevelFlow,
EFCharacterCreationACFUBridge, and EFProjectSystems. Preserve the input contract:
O free camera, Period character creator, L debug menu, Comma full Needs & Status
HUD, N custom walk, C crawl, Y actions/emotes/interactions, J Chronicle,
H conditional status debug, and exact existing T/Plus/Minus interactions.

Build the Editor through
`Tools/Migration/Build-NoShellForWinterEditor58.ps1` and launch through
`Tools/Migration/Launch-NoShellForWinterEditor58.ps1`. Never launch or leave a
receipt with DazToUnreal or EFCharacterCreationDazBridge disabled. Any temporary
UBT-only exclusion belongs to the protected build wrapper, which must repair
the receipt immediately. Verify the actual wrapper parameters before use; do
not invent Development, Shipping, or packaging switches for an Editor wrapper.

The enforceable runtime contract is:

- No silent return to HUB after failed generation.
- No endless loading or unlimited retries.
- No hidden substitutions, truncated selections, or changed rules to rescue an
  invalid attempt.
- No acceptance of an invalid dungeon or release of the player before commit.

“Never fail under any circumstances” is not an engineering guarantee. Finite,
observable failure with a protected player and explicit recovery controls is.

## 2. Existing evidence and initial investigation

The user-provided audit establishes the following V6 observations. They are
reported evidence to preserve and investigate, not fresh V7 validation:

- Travel reaches DungeonGeneration, PCG runs, and manifests contain themed rooms.
- Navigation preparation immediately reports `TOPOLOGY_REPAIR_START_INVALID`.
- Current readiness confuses an idle navigation build queue with usable nav data.
- Runtime navigation bounds do not correctly derive their extent from generated
  structural geometry.
- Logs report missing blocking floor near Start. Whether that persists after
  geometry settles needs actual runtime observation.
- Repeated failed generation eventually returns the player to HUB.

Freeze failing seeds `1779679224` and `1190737158`, the dirty worktree, current
V6 configuration, relevant logs/reports, and protected hashes before changes.
Inspect floor instances, instance transforms, collision, navigation relevance,
bounds registration, nav tiles, Start/End room ownership, and capsule clearance
after native geometry settles. Do not label a changed DoorToLevel destination,
an idle nav queue, or a passing JSON verifier as the root-cause fix.

The audit also found exposed settings that only affect stored data/hashes,
unsampled counts, an implicit rarity Nothing result, omitted Winter selection,
hidden lighting/category/NPC constants or identifier parsing, and diagnostic
clutter in Details. Async asset loads did not eliminate observed navigation and
shader/PSO hitches. Passing V6 native tests and Blueprint compilation did not
prove playable traversal; a PASS JSON followed by process crash is a failed
execution gate.

## 3. Stable authority and data boundaries

Create exactly one master `UEFCalystoDungeonDirectorAsset`, internally schema
version 7, at:

`/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector`

Use stable, unversioned public APIs and runtime class names. Schema version 7 is
internal metadata, not an invitation to create another version-suffixed family
of public classes or assets. The named class and path here are requirements;
their existence and authority remain PENDING until verified.

Keep the following types and ownership boundaries separate:

1. Authored configuration: editable asset data with persistent identities.
2. Compiled configuration: immutable, validated runtime inputs.
3. Floor request and generation attempt: separate identities, lifetimes, retry
   state, cancellation, and one shared request deadline.
4. Room/surface context: native topology ownership and valid opportunities.
5. Reserved generation manifest: immutable selections and placements after
   feasibility and random decisions.
6. Realized floor result and diagnostics: verified world outcomes, timings,
   acceptance/rejection, and evidence.

V7 must not compile through V6 structures, read V6 assets at runtime, or delegate
authoritative random decisions to migration wrappers. Profile and entry
identities are generated and persisted in the editor. Display names are
editable labels: renaming must not alter gender, archetype, lifecycle, budget
membership, or random identity. Canonical identities govern serialization,
lookup, random domains, diagnostics, and reorder stability. Duplicate entries
receive validated identity handling without colliding with originals.

## 4. Clean native Unreal authoring

Use native Unreal Details with this visible structure:

```text
Dungeon
Dungeon Styles
  [Style Name]
    Selection
    Layout
    Style Materials
    Architecture
    Lighting
    Content
    Decals
    Advanced
Room Themes
  [Theme Name]
    Selection
    Room Materials
    Architecture
    Content
    Decals
    Advanced
Advanced
```

All editor text, APIs, tooltips, logs, and documentation are English. Keep
technical IDs, hashes, schema/version fields, migration labels, authority
banners, and duplicated summaries out of ordinary editing. Use named array
entries, conditional visibility, sensible defaults, and short tooltips.
Percentages display as percentages rather than unexplained fractional values.
Preserve native asset pickers, search, Undo/Redo, duplication, reset controls,
and validation.

Diagnostics and probability simulation belong in an on-demand inspector
available from the editor and the Development Harness. The shipping player
flow presents only information needed for player decisions, including concise
recovery errors. Validation errors name the exact field. “Authoring Valid” must
never imply “Gameplay Verified.” No new control may be exposed on the strength
of merely being stored, hashed, or displayed: every editable gameplay setting
needs a behavioral test demonstrating the advertised effect. Implement its
effect or remove/hide the misleading control before delivery.

## 5. Styles, Themes, native architecture, and lighting

Exactly one Style is selected per floor. Multiple Themes may coexist across
eligible rooms. Each Style owns inline layout distributions, materials,
architecture, lighting, content, decals, and floor-wide budgets. Each Theme
owns inline room materials, native decoration/architecture settings, local
content overrides, and decals. Do not introduce secondary Style/Theme
configuration Data Assets. Meshes, materials, actor Blueprints, graphs, and
baked PCG/level-instance payloads remain ordinary referenced content resources.

Preserve native Calysto capabilities:

- Floor, Wall, Roof, door frames, doorways, ramps, and supported structural
  variants.
- Wall Bottom/Middle/Top, Floor, Corner Bottom/Middle/Top, and Roof placement.
- Mesh, actor, and baked PCG/level-instance payloads.
- Native transform offsets, scale, rotation, variation, weights, and explicitly
  intentional Empty decoration outcomes.

Required structural pieces cannot randomly disappear. Supported variants and
parameters may vary procedurally. Exposed lighting controls and category
behavior must have explicit data and tests, without hidden constants or
display-name parsing. Travel integration owns progression actor identity.
Expose supported door appearance settings; do not expose an End Blueprint that
runtime then silently replaces.

## 6. Shared probability contract

One shared decision library governs Styles, Themes, architecture, enemies,
support NPCs, props, containers and their contents, custom PCG extensions, and
decals. Explicit probabilities are the default. Adaptation is supported but
disabled in the initial asset.

| Control | Meaning |
| --- | --- |
| Chance | Whether an eligible opportunity produces content. |
| Amount | Conditional count distribution when content is selected. |
| Weight | Relative selection among eligible alternatives. |
| Placement | Surface/zone and spatial requirements for realization. |
| Limits | Capacity constraints applied to feasibility; not hidden probability rolls. |
| Empty | Explicitly authored absence where native optional decoration supports it. |

Support fixed, uniform, and triangular distributions with accurately named
mathematics. Inline depth curves use exact authored endpoints and linear
interpolation by default. Tests must include endpoints, clamping behavior,
conditional counts, degenerate ranges, and supported integer rounding rules.
Document any deliberate mathematical correction during migration instead of
silently reproducing misleading V6 behavior.

Default trial scopes are once per floor for Style selection, per eligible room
for Theme presence/type and content groups, per native construction slot for
architecture variants, per valid native opportunity for optional surface
decoration, and per container for container contents.

### 6.1 Eligibility, reservation, and commitment order

For each content decision:

1. Build finite candidate/entry compatibility sets.
2. Apply depth, gameplay role, cooldown, surface, footprint, spacing, and
   remaining floor/room budgets.
3. Establish feasible placement-and-budget reservations for supported amounts.
4. Roll Chance, conditional Amount, optional rarity, and eligible entry weights.
5. Freeze selected entries, counts, random outcomes, and reservations before
   materialization.

The feasibility stage determines supported outcomes without committing an
impossible count. Do not roll an impossible amount and truncate it afterward.
Do not roll repeatedly until an answer fits or use a failed placement as a
hidden Nothing outcome. Report how feasibility conditions requested
distributions. Once selected and reserved, every requested element must
materialize successfully; otherwise reject the whole attempt.

Rarity normalizes over eligible populated tiers, including **Winter**. It must
not introduce a second implicit Nothing roll. Zero-weight alternatives cannot
win. Exhausted budgets, absent surfaces, and incompatible entries affect
eligibility before random commitment. Explicit Empty remains supported only
where intentional native decoration absence is authored.

Use canonical identities, canonical candidate ordering, and independent random
domains. Material changes must not reroll topology. Theme-weight changes must
not change the Theme-presence set for unchanged topology. Array reorder and
display-name edits must preserve stable decisions. Request IDs that route
callbacks cannot contaminate reproducible seeds. Static tables may use alias
sampling; capacity-sensitive selection needs a suitable bounded sampler.
Never claim universal O(1) performance.

### 6.2 Guaranteed Theme mathematics

Start, End, Critical, and Progression rooms are ineligible for Themes. For
`N >= 1` eligible rooms:

1. Choose one guaranteed room by an independent deterministic rank.
2. Independently Theme each remaining eligible room with probability `0.25`.
3. Choose each themed room's Theme from normalized eligible Theme weights.

```text
ThemedRooms = 1 + Binomial(N - 1, 0.25)
Expected themed fraction = [1 + 0.25 * (N - 1)] / N
```

Label the probability **Additional Room Theme Chance**. Show the explicit
guaranteed-room setting while preserving the required minimum of one eligible
themed room. Do not label the result “25% of all rooms.” With one eligible room,
that room is always themed. With no eligible room, or no eligible Theme for a
required themed opportunity, the topology/configuration cannot meet the
contract and must be rejected before content commitment, with an accurate
spatial or configuration failure classification.

### 6.3 Catalogs, inheritance, and advanced behavior

Use typed gameplay roles and explicit budget membership. Never infer behavior
from `Forge`, `NPC`, dot-separated entry IDs, or any other label.

Retain and test:

- Enemies, archetypes, genders, rarity, threat costs, level offsets, and depth
  eligibility.
- Support NPCs, recruitment, companions, lifecycle, inventory handoff,
  graveyard eligibility, and Winter's Recall.
- Armor, loose loot, food, drinks, chest variants, lockpicking, and chest
  contents.
- Cooldowns, repetition controls, drought protection, quotas, run outcomes,
  and ecology.
- Danger, safety, abundance, mystery, clothing influence, volatility, and
  player adaptation.

Advanced modifiers have explicit inputs, bounded effects, and behavioral tests.
With adaptation disabled, adaptation has mathematically zero influence, not an
approximately small effect. Tests compare disabled-adaptation decisions under
different player/adaptation inputs and require identical applicable outputs.

Theme content mode semantics:

| Mode | Required behavior |
| --- | --- |
| Inherit | Use the selected Style's category settings. |
| Extend | Add/remove entries without silently changing inherited probability or limits. |
| Replace | Own the category locally through Theme settings. |
| Block | Disable that category locally. |

Probability and Amount overrides are separate explicit choices. Floor-wide
limits apply across all rooms and cannot be bypassed by Extend or Replace.

## 7. Native adapter and attempt state machine

Replace overlapping readiness booleans and duplicated geometry searches with
one transaction-owned state machine:

```text
Preflight -> Native Generation -> Structural Verification
          -> Navigation/Reservations -> Realization Verification
          -> Commit -> Player Release
```

Independent preparation may overlap, but commitment waits for every selected
result. A single attempt owns geometry, callback routing, reservations,
navigation registrations, spawned results, retained transient schemas, and
cleanup. Never more than one generation attempt may be active.

The Calysto adapter localizes vendor classes, properties, and graph pins in one
compatibility layer. Validate required graph connections and capabilities.
Compose transient graphs/data without modifying vendor assets. Issue exactly
one root `GenerateLocal` per attempt. Retain transient room-schema objects
strongly until the attempt is released. Preserve the supported UE 5.8
SoftObjectPath metadata approach. This adapter is neither a replacement dungeon
generator nor a generic arbitrary graph-rewriting framework.

### 7.1 Entry, structural, and navigation postconditions

Before accepting navigation, inspect actual generated components:

- Nonzero applicable Floor instance counts and their actual transforms.
- Floor collision, navigation relevance, and structural bounds.
- Exactly one Start and one End, each belonging to its designated native room.
- Blocking floor and player capsule clearance at the entry transform.
- Registered navigation bounds covering generated walkable geometry.
- Usable relevant nav data, relevant tiles, and a valid Start-to-End route.

An idle navigation build queue is not a readiness predicate. Derive navigation
bounds from generated structural geometry; observe generation and registration
settling before classifying spatial failure. Use asynchronous/event-driven
navigation preparation and bounded observation. Do not repeatedly invoke
blocking full navigation rebuilds or expand projection searches indefinitely.

The Director publishes exactly one validated entry transform. EFLevelFlow
consumes that transform; it must not independently search arbitrary nearby
rooms or silently replace the spawn with another location. Protect the player
until all realization and travel handoff conditions have passed.

### 7.2 Placement and supported PCG extensions

Every world-spawn entry exposes a placement zone. Existing pickups/chests stay
Floor; wall torches stay Wall Middle, with current 2 cm variation as the initial
default. Use native surface opportunities and room ownership. Valid Floor
space must be covered beyond the previous sparse anchor set.

Precompute and reserve placements using bounds, surface normals, footprint,
clearance, doorway protection, spacing, and navigation requirements appropriate
to that content. Keep candidate work finite/bounded and avoid array-order
favoritism. Verify actual realized components and actors against reservations.

Custom PCG subgraphs must:

- Accept validated room/surface inputs and deterministic seeds through
  documented input/output pins.
- Produce bounded content proposals.
- Declare footprints, supported surfaces, dependencies, and supported contract.
- Preserve topology and progression markers.
- Never recursively regenerate the dungeon or perform untracked spawning.
- Fail preflight for unsupported contracts before uncontrolled generation.

The master generator remains native Calysto. Extension output is subject to
the same reservation, exact realization, rollback, and verification contract
as native content.

## 8. Materials, decals, loading, and performance

### 8.1 Materials

Per-surface precedence is always:

`Room Theme override > selected Style material`

An explicit enabled override with an empty reference is invalid. Absence of an
override uses the selected Style material. There is no silent native-material
fallback and no MID per room.

Preserve the initial appearance: general Style grey, Forge orange with
instancing/Nanite compatibility, and Shrine display blue. Verify actual
material assignments and visible rendering on generated Floor, Wall, and Roof
components, including shared room boundaries. Loaded references and manifest
entries alone do not establish material correctness.

### 8.2 Pooled blood decals

Retain one pool owner, with no actor or MID per stain. Initial values are:

| Setting | Initial value |
| --- | --- |
| Pool capacity | 24 |
| Active floor budget | 8 |
| Maximum per room | 1 |
| Floor surface limit | 3 |
| Wall surface limit | 4 |
| Roof surface limit | 1 |
| Style Chance | 10% |
| Forge replacement Chance | 25% |
| Shrine | Blocked |

Protect Start, End, progression markers/rooms, doors, and the main route. Use
only the intended RealisticBlood texture dependency closure; exclude unintended
Demo, Blueprint, and Niagara content from that closure while preserving native
Calysto torch effects. Retain useful project-owned unversioned internal decal
materials where appropriate.

Implement real distance culling and accurately named fading. Screen-size
fading must not be described as exact world-distance fading. An unselected
decal may legitimately be absent. A selected and reserved decal that cannot be
realized invalidates the attempt like any other selected content.

RealisticBlood entitlement remains an unresolved distribution gate until
confirmed. Skill/documentation delivery does not confirm entitlement or grant
distribution rights.

### 8.3 Phased loading and measurement

Retain and improve these asynchronous phases:

1. Shared immutable/session dependencies.
2. Reachable structural and Theme visual dependencies.
3. Frozen selected gameplay payloads.
4. Selected decal and extension dependencies.

Deduplicate paths across phases, make identical requests idempotent, reject
stale callbacks, and retain leases for their request/attempt lifetime. Release
attempt-owned leases on rejection or cancellation without releasing shared
dependencies still legitimately needed.

Prewarm supported material PSOs and native torch effects during loading.
Measure geometry, navigation, shader/PSO preparation, and gameplay spawning
separately. Record hardware, Editor/package configuration, cold/warm state,
total Door-to-FloorReady latency, failed-attempt time, cleanup time, hitches,
and retained memory/objects. Async loading alone is not proof of hitch-free
generation. The initial target is P95 below 30 seconds on recorded target
hardware. Do not invent percentage improvement against broken V6 or infer a
credible P95 from only the three-floor smoke test. Report sample size and the
scope/limitations of the empirical release measurement.

## 9. Strict bounded recovery

Initial defaults are **four total attempts** (one initial plus three retries)
inside **one shared 30-second floor-request deadline**. Retries do not reset
the deadline. Use elapsed monotonic request time. One active attempt means
cleanup completes before a replacement starts. No additional retry may start
after the deadline or while rejected-attempt ownership is still live.

Across attempts preserve run identity, floor number, selected Style, authored
rules, budgets, and pre-floor companion/outcome state. Each attempt receives
its own identity and deterministically derived topology seed. Callback request
IDs remain routing metadata only. Do not advance progression, switch Style,
change weights, reduce selected content, or commit inventory/companion changes
to rescue a failed attempt.

| Outcome | Required handling |
| --- | --- |
| Pending | Work is genuinely unfinished; observe within the shared deadline without consuming another attempt. |
| Recoverable spatial failure | Settled topology, entry/path, or spatial realization is invalid; reject, fully clean up, and deterministically reseed if time/attempt budget remains. |
| Configuration/resource failure | Invalid/missing assets, unsupported graphs/contracts, or unavailable infrastructure; fail explicitly without pretending another seed repairs the cause. |
| Cancelled | Release ownership, do not commit outcomes, and do not start recovery. |

Before a new attempt, remove the rejected attempt's actors, instances, decals,
reservations, navigation registrations, callbacks, transient references, and
other attempt-owned resources. Delayed callbacks must be harmless and cannot
commit, respawn, or mutate a later attempt. Cleanup is bounded and observable;
if it cannot establish isolation, terminate recovery in the protected error
state instead of overlapping attempts or waiting indefinitely.

When time or attempt budget is exhausted, keep the player protected and show a
concise English error with **Retry** and **Return to HUB**. Return to HUB is an
explicit player action, not an automatic bounce. A player Retry starts an
explicit new request under the documented identity/progression rules; it must
not secretly extend the exhausted request. Intended successful final-floor
return to HUB remains normal travel behavior and is tested separately.

Report requested, feasible, selected, rejected-attempt, and accepted outcomes.
Feasibility and recovery condition the observed distribution; statistics must
include rejected and accepted attempts rather than reporting only successes.

## 10. Gated delivery sequence

The timing reference linked above governs step deadlines and concise waits.
Each milestone records entry criteria, exact changes, validation, remaining
PENDING gates, and a small commit or non-destructive snapshot. Preserve dirty
work and pre-existing protected discrepancies; never reset/stash/clean user
changes or silently rebaseline protected hashes. Re-hash ACFU, DazToUnreal,
Player, Female, Frederick, Multiple, and Male after each migration phase.

### Milestone 1 — Freeze and reproduce

Capture current source/target identities, dirty Git state, V6 asset/config,
protected baseline, live editor identity, dirty packages, logs, and seeds
`1779679224` and `1190737158`. Start with the Unreal MCP and Calysto skills and
read-only editor context. Inspect real settled structural collision/bounds,
navigation registration/tiles, entry clearance, and route status.

Advance only with a reproducible failure receipt or a precise PENDING record
explaining what prevented reproduction. A hypothesis alone is not evidence
that the root cause has been established or fixed. Independent design/source
work may proceed, but blocked runtime gates remain blocked for promotion.

### Milestone 2 — Native parity and playable traversal

Build a minimal V7 integration using native Calysto geometry, with population,
decals, and custom extensions disabled. Compare native and Director-enabled
structural output for matching supported topology inputs and seeds. Validate
one root generation call per attempt, structural postconditions, navigation,
entry transform publication, and EFLevelFlow consumption.

Prove real DoorToLevel traversal at least HUB -> Floor 1 -> Floor 2, then use
the required rapid gate **HUB -> Floor 1 -> Floor 2 -> Floor 3** before enabling
later content layers. Capture a unique accepted attempt/floor receipt per
transition, actual player presence and movement, door behavior, and screenshots.
Direct harness spawning or teleporting cannot substitute for this door gate.
This intermediate fixture proves native traversal only; it is not final V7
acceptance and does not waive the final Theme guarantee.

Stop adding content while structural/navigation/travel failures persist. Fix
the observed blocker and repeat only the affected concise gate.

### Milestone 3 — Authoring and read-only V6 migration

Implement the new native Details model and immutable V7 compiler. Import V6
read-only into the unversioned V7 master asset. Produce a field-by-field report
mapping each source property and value to the target field, transformation,
behavioral meaning, validation, and disposition.

Preserve user-authored materials, catalogs, weights, transforms, budgets, and
retained gameplay functions. Explicitly record mathematical corrections,
unsupported data, and changes to prior misleading behavior. Do not silently
drop fields, normalize away user values, or call a changed default preserved.
Advance when migration and authoring behavior pass, while V6 remains immutable
and any live V7 acceptance not yet executed remains PENDING.

### Milestone 4 — Themes, materials, architecture, and placement

Implement the guaranteed Theme formula, surface material precedence, inline
native architecture, supported door appearance, lighting, and all placement
zones. Verify grey general rooms, orange Forge, blue Shrine, Floor/Wall/Roof,
shared boundaries, pickups/chests on Floor, torches on Wall Middle, and
wall/corner/roof placements. Protect progression and main routes. Advance only
after actual component/visual evidence and the relevant behavioral tests.

### Milestone 5 — Content, bridges, and strict realization

Migrate EFLevelFlow, EFProcedural, EFProjectSystems, enemy levels, outcomes,
companions, recruitment, chests, inventory travel, and Harness consumers to
direct unversioned V7 interfaces. Complete the shared probability system,
finite feasibility reservations, strict materialization, rollback, and recovery.
Verify lifecycle, death/recall, lockpicking, contents, floor-wide budgets, and
catalog isolation. Inject failures and prove no partial commits or leaks.

### Milestone 6 — Decals, custom PCG, adaptation, and performance

Add pooled decals, validated extension contracts, and supported advanced
modifiers with adaptation initially disabled. Verify disabled zero influence,
bounded enabled effects, phased loading, lease/callback ownership, native torch
prewarming, and honest culling/fading controls. Measure cold/warm stages and
total request latency. Unsupported extension contracts fail preflight.

### Milestone 7 — Complete candidate validation

Run all acceptance gates in Section 11 with V6 retained as immutable migration
source, never an active V7 fallback. Start with probability/behavior tests and
three consecutive real-door floors. Only after those pass, run the single
planned 25-consecutive-floor ownership soak and the targeted seed/style/size/
depth/package matrix. Reuse overlapping evidence instead of multiplying runs.

Do not run 1,000 floors or broad exploratory floor sweeps. The 100,000-trial
requirement is fast in-memory probability sampling, not world generation.
Unresolved blocker, timeout, crash, protected delta, or missing package evidence
prevents promotion; it does not justify deleting V6 first.

### Milestone 8 — Switch authority and retire V6

Audit Asset Registry referencers, configuration, code, tools, generated
manifests, redirects, and packages. Switch authority only after the V7 candidate
has passed required gates and all active consumers are migrated. Delete exact
obsolete `.uasset`/`.umap` packages only through Unreal Editor operations with
reviewable ownership/referencer evidence. Remove V6 classes, wrappers, obsolete
tools, and temporary redirects only when migrated consumers no longer need
them. Audit V3/V4/V5 residues too.

Retain useful unversioned internal materials, runtime Blueprints, and cooked
PCG compatibility assets; their existence is not legacy contamination. Archive
historical evidence outside runtime/cook. Never bulk-delete vendor content or
generated directories. Do not raw-delete, move, or rename Unreal packages.

### Milestone 9 — Final V7-only rebuild and packages

Cold-rebuild Editor, Development, and Shipping from the final tree; compile
affected Blueprints without protected saves; actually cook and create fresh
Development/Shipping packages. Repeat real traversal and visual smoke gates
against that exact final tree/package, including three consecutive floor
transitions. Confirm no active V3/V4/V5/V6 Director authority, classes,
redirects, or package dependencies. Update project skills and migration docs
to the demonstrated final authority, with evidence and remaining distribution
gates explicitly separated.

## 11. Acceptance and evidence

All rows below initially remain **PENDING**. Use PASS only when the exact
recorded artifact, process exit, logs, and applicable live/package observation
support it. Authoring validity, unit test success, runtime success, packaged
success, and distribution eligibility are separate results.

### 11.1 Authoring and probability

- A new user can locate Style/Theme materials, placement, Chance, Amount, and
  Weight without diagnostic banners.
- Every exposed functional setting has a behavioral test showing its runtime
  effect; editor Undo/Redo, duplication, reset, pickers, search, named entries,
  conditional fields, and exact-field errors work.
- At least 100,000 in-memory trials cover Theme presence, conditional Theme
  weights, guaranteed-room counts, quantity distributions, rarity including
  Winter, and exact curve endpoints. Use deterministic, predeclared statistical
  tolerances appropriate to each distribution and include exact assertions
  where applicable; do not retry failed statistics until they pass.
- Reordering arrays and renaming display names preserve decisions. Material
  changes preserve topology; Theme-weight changes preserve Theme-presence sets.
- Empty catalogs, zero weights, exhausted cooldowns/budgets, incompatible
  surfaces, no eligible rooms, and unpopulated rarity tiers have tested results.
- Fixed/uniform/triangular Amounts, supported endpoints, inheritance modes,
  explicit probability/count overrides, floor limits, and disabled-adaptation
  zero influence are tested.
- Requested unconstrained, feasible, and accepted probability statistics are
  reported separately, including rejected attempts.

### 11.2 Native integration and strict realization

- Native layout/architecture parity with extensions disabled; exactly one
  Style, unique Start/End progression markers, one root generation per attempt.
- Start/End/Critical/Progression rooms remain unthemed; at least one eligible
  themed room exists in every accepted final V7 floor.
- Floor collision/bounds, entry clearance, relevant nav data/tiles, and a
  Start-to-End route pass before the player is released.
- Every committed actor, material assignment, PCG result, and decal verifies.
  Failure injection proves whole-attempt rollback without leaked ownership or
  partial companion/inventory/outcome state.
- Delayed geometry/nav registration stays Pending while genuinely unfinished.
  Reseeding preserves Style, progression, budgets, and pre-floor state and
  cannot double-commit outcomes.
- Custom PCG contract violations fail before uncontrolled generation.
- Shared deadline, four-attempt limit, explicit error Retry/Return to HUB,
  cancellation, stale callbacks, idempotent loads, and cleanup isolation work.

### 11.3 Gameplay and visual QA performed by the agent

- Real DoorToLevel HUB -> Floor 1 -> Floor 2 -> Floor 3, subsequent floors as
  needed, and intended final HUB return. Three consecutive floors are the
  mandatory rapid traversal gate, not three independent harness generations.
- Both original failing seeds, every initial Style, cold/warm runs, and
  supported size/depth extremes are covered by a targeted finite matrix.
- Grey general, orange Forge, and blue Shrine are independently verified on
  Floor/Wall/Roof and boundaries by assignments and visual evidence.
- Floor pickups/chests, Wall Middle torches, wall/corner/roof placements, native
  torch effects, decal budgets/protected routes, and native architecture work.
- Theme catalog isolation, recruitment, companion death/recall, inventory
  persistence, chest variants, lockpicking, and container contents work.
- Replay, reroll, same-seed restart, cancellation, repeated interaction,
  stale callbacks, and recovery exhaustion are covered.
- At least 25 consecutive accepted floor generations show no monotonic
  retained-object, actor, component, or lease growth after the short gate
  passes. Record rejected attempts and cleanup as part of the same soak; do
  not hide failures by restarting the counter or discarding reports. Separate
  legitimate shared-cache warmup from leaked attempt ownership.

### 11.4 Build, performance, release, and protected invariants

- Successful cold Editor, Development, and Shipping builds.
- Affected Blueprint compilation with zero errors and no protected saves.
- Actual cook and fresh Development/Shipping packages, each linked to the
  exact candidate/final tree and process/log evidence.
- Packaged traversal and visual smoke tests, repeated after V6 retirement.
- No active V3/V4/V5/V6 Director authority, classes, redirects, or runtime/cook
  package dependencies in the final project.
- Intended RealisticBlood texture dependency closure only for decals, with no
  unintended Demo/Blueprint/Niagara assets and preserved native Calysto torches.
- Recorded target hardware and total Door-to-FloorReady latency including
  failures/cleanup; initial P95 target below 30 seconds. Report cold/warm
  PSO/nav/hitch/memory measurements and sample counts without invented baseline
  improvement or excessive stress runs.
- Every required process exits successfully with clean relevant logs. PASS
  JSON followed by a crash cannot satisfy a gate.
- Protected hashes are compared before/after every phase. Preserve and report
  pre-existing discrepancies separately; never silently accept a new baseline.
- RealisticBlood entitlement confirmed before distribution; until then that
  gate stays PENDING even if technical packaging passes.

### 11.5 Evidence record and honest stopping

For every gate record source/target path, exact symbol/asset, test and parameters,
seed/run/floor/request/attempt identities where relevant, timestamps and elapsed
time, process exit, log/report/screenshot paths, protected hash comparisons,
and commit or non-destructive snapshot. Record failure classification, cleanup,
requested/feasible/accepted counts, and runtime/package environment when those
claims depend on them. Keep historical artifacts outside runtime/cook.

Use bounded observations and targeted reruns. At a timeout, gather the last
observable state/log, stop only owned test activity through supported cleanup,
and mark the affected gate failed or PENDING with a reason. Never repeatedly
extend a deadline, leave the player loading indefinitely, or perform long test
sweeps to manufacture success. Do not terminate an unrelated editor/process.

The implementation is complete only when clean authoring, real traversal,
materials, strict probabilistic generation, automatic recovery, and a V7-only
packaged build are demonstrated together. A new skill, renamed asset, cleaner
screenshot, successful compile, or passing unit suite alone is not V7 completion.
