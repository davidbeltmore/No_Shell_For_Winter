---
name: ef-npc-outfit-workbench
description: Author and validate NoShellForWinter DAZ NPC outfit presets, shared EFEquipment admission, appearance-linked inventory restoration and bounded clothing crowd measurements. Use for NPC clothing selection and persistence; body onboarding and clothing binding compilation remain in their existing workbenches.
---

# NPC outfit workbench

Delivery decision (2026-10-12): the user accepts current performance and defers
optimization. Keep original 10/25-NPC numerical budget FAILs; do not reopen
optimization just to clear them. This acceptance does not waive functional,
visual or packaged checks. Use `Docs/Migration/NPCOutfitPerformanceFollowup.md`
for the latest baseline and recommended future investigation when requested.

Start with `noshellforwinter-unreal-mcp` and read-only target context. Read
`Docs/Migration/NPCOutfits.md` for current measured gates; this skill does not
certify the outstanding release gates. Source game, vendor plugins, Player and
protected body assets remain read-only.

Current user direction (2026-10-12): prior LOD changes caused broken animations
and gameplay. The independent garment LOD experiment has been withdrawn from
this task, including generated FullRag LOD1 data. Keep the original character
and garment LOD behavior. Do not enable independent NPC LOD or restart reduction
experiments without a new explicit user request. The diagnostic commands below
remain historical reproduction tools, not the current optimization workflow.
`WithdrawGarmentLOD` is the bounded, hash-guarded editor rollback tool for the
specific three experimental meshes; inspect first, then `--apply`. It removes
only generated LOD1, preserves LOD0 vertex counts, morph names and skeleton,
and keeps scoped package backups. Publish fresh bindings afterward.

## Authoring and ownership

- `Tools/NPC/Outfits/Definitions.json` maps stable outfit IDs to owned DataAssets
  under `/Game/_Game/Characters/Data/Outfits`. Each item references an existing
  armor class and exact leaf destination. `FullRag` uses RagShirt/Chest,
  RagPants/Legs and RagFootWrap/Feet. No gender restriction or parent Armor slot.
- `Tools/NPC/Roster.json` is the assignment entry point. An archetype's `outfits`
  array contains `{ "id": "FullRag", "weight": 1 }` rows. Empty opts out; one
  choice is fixed; multiple nonnegative weights select deterministically using
  the existing appearance identity seed. At least one weight must be positive.
- `ProjectNPCOutfitComponent.OverrideChoices` overrides selection per instance;
  `bEnabled=false` opts that instance out. Existing saved equipment still wins
  over changed selection data. Adding an outfit does not require a character BP.
- ACF owns item instances. `UEFEquipmentCoordinatorComponent::CreateForNPC`
  explicitly enables shared slot/reservation operations, without player polling,
  carry projection, UI notifications or death routing. Do not make automatic
  `FindOrCreate` admit every Pawn. Native slot arrays can contain repeated Ammo:
  deduplicate before checking ancestry, rather than removing weapon slots.
- EFClothingMorph Director remains the only fitting/layer/coverage authority.
  The NPC integration attaches its existing runtime/bridge only to eligible
  owners; global non-player enable stays false. Use `ef-equipment-workbench`
  for new item rules and `ef-clothing-unisex-workbench` for garment registration.
  Use `ef-body-preset-workbench` for new bodies; an outfit cannot certify a fit.

## Ordering, persistence and cancellation

The existing appearance subsystem queues outfits after body/morph application.
Native equipment readiness and ALS idle precede mutation. The world queue defaults
to one owner/frame and four initializing outfits. This is an initial admission bound,
not proof of a global binding-load budget or supported crowd size.

`Project.NPCOutfits.MaxActiveInitializations` is clamped to 1..16 and defaults to
four. `RunNativePIE`/`Sweep.py --max-active 4|8|16` records the effective limit and
restores the previous value. Comparisons reject different limits. Inspect
`fixture_initializations`: spawn-to-admission includes body initialization plus
queue wait; per-owner initialization includes all later scheduling/render work.
Prepared 25-NPC diagnostics showed diminishing benefit above eight; do not infer
that a higher cap alone meets the fixed two-second group budget.

Fully loaded resident definitions and item classes can proceed without async
completion scheduling. Definition ownership plus transient `ResidentItemClasses`
pins protect them until Ready, failure, restart or EndPlay. Unloaded or pending
post-load objects retain the asynchronous path; no synchronous load is added.
`Project.NPCOutfits.UseResidentPrerequisites=0` preserves that path for diagnostics.
The lifecycle fixture explicitly tests destruction both before resident equip
and while an async callback is pending, restoring the CVar afterward. Do not
replace async cancellation coverage with a generic Pending/Loading assertion.
The resident prerequisite path has also passed a fresh cooked-process check on
the isolated cold-item map: the initial NPC begins with a nonresident definition
and three nonresident classes, then reaches Ready. Three representative bodies
pass packaged native equip/unequip, reinitialization, GC and normal exit. These
default-LOD functional runs do not certify the opt-in LOD1 quality/performance
candidate; see the migration document for the retained package and exact scope.

`FProjectNPCAppearanceRecord` retains outfit ID/version and the shared native
inventory serialization envelope under the existing identity. A versioned record
is restored, never regenerated—even an emptied inventory. `RestoreSerial`
distinguishes an actual load from repeating appearance initialization. Do not
replace GUIDs with classes/indices or serialize reservations as inventory items.

Initialization and presentation have a 20-second deadline. Initial grant failures
attempt native inventory rollback and report `Failed`; invalid saves do not grant
replacement clothing. Load handles cancel on component teardown and use weak
callbacks. The character itself is not hidden by this integration. `Ready` is
render-submission evidence and must be supplemented by image review.

Inventory mutations must be captured before same-frame destruction or saving.
Deferring the only inventory snapshot to a later tick resurrected a removed shirt
on identity-based recreation; use the lifecycle fixture to verify this boundary.

## Commands and evidence

All paths below are relative to the target project. `Run.py` uses the maintained
localhost editor Python bridge and accepts remaining arguments for its action.

```powershell
python Tools/NPC/Outfits/Protection.py before
python Tools/NPC/Outfits/Run.py Inspect
python Tools/NPC/Outfits/Run.py Author
python Tools/NPC/Outfits/Run.py Audit --compile
python Tools/NPC/Outfits/Run.py RunPIE --count 4 --seed 731
python Tools/NPC/Outfits/Run.py RunNativePIE --body Male.02 --count 1 --capture 2 --functional 1 --name unique_pilot
python Tools/NPC/Outfits/Run.py SaveQA
python Tools/NPC/Outfits/Run.py LifecycleQA
python Tools/NPC/Outfits/Run.py RunNativePIE --selection --count 16 --seed 731 --name unique_selection
python Tools/NPC/Outfits/Run.py ClothingRegression --bodies Female.01,Male.01,Male.02
python Tools/NPC/Outfits/Run.py LogicQA --mode ai
python Tools/NPC/Outfits/Sweep.py --counts 1 10 25 50 --prefix unique_measurement
python Tools/NPC/Outfits/Protection.py after
```

The protection baseline is immutable; reuse `after`, never overwrite `before`.
Before PIE, use `SaveGuard.ps1 -Mode Backup` in a fresh session directory; after
closing that editor, use `-Mode Restore` and verify every prior save hash. This
covers independent subsystems that write during EndPIE. Keep backups on failures.

Author saves only the definitions and roster, with scoped backups. Audit compiles
owned variants without saving them. `--require-all` is the rollout gate. The
current FullRag rollout passes all 40 variants; keep explicit configured,
unconfigured and failed-variant counts when adding or excluding variants.
Runtime receipts live in `Saved/NPCOutfits`, compact published evidence in
`Docs/Migration/Evidence/NPCOutfits`. A successful invocation is not runtime PASS:
require the fresh `runtime.json` and `host.json` where provided.

Native command `Project.NPCOutfits.QA` accepts count, body, archetype, dressed,
seed, capture, timeout, name, functional, loading and optional `quit`. It freezes
fixture navigation for controlled fit/performance work. It does not certify AI;
use the existing NPC logic fixture for actual interaction and pursuit.
`gc` forces collection after fixture destruction; the PIE host exposes `--gc`.
For a cooked build use `Tools/NPC/Outfits/ValidatePackaged.ps1 -StagedRoot ...`
with optional `-Body`, `-Archetype`, `-Count` and `-Seed`. It requires native
functional assertions, GC completion, zero pending work, image capture, feature
logs and a normal process exit. Review the actual image separately. A fresh
process can preload clothes through HUB; do not call it an isolated cold-resource
fixture. Use the existing body-preset HUB package wrapper after protected builds
and clothing freshness checks; its cook includes the owned Characters/Outfits
and V4/V5 generated directories through DefaultGame.ini.

Measure matched dressed/undressed groups 1, 10, 25; 50 is diagnostic. Capture
30–45 seconds per group, separate cold from warmed loads, retain raw CPU/GPU/frame
samples and teardown counts. `Measure.py` compares paired receipts. The user-frozen
25-NPC incremental budget is CPU 2 ms, GPU 3 ms, frame p95 4 ms and warmed readiness
2 s, with 60 FPS only the provisional whole-frame reference. Never loosen it after
measurement. Missing telemetry is PENDING, not zero cost. See the migration doc
for actual capacity and current limitations.

`--prewarm` prepares the same group in the same PIE world before measurement.
Preparation is not a cold packaged-load benchmark. `Measure.py` requires actual
quality CVars, matching provider configuration and successful preparation receipts
for warmed acceptance. Never overlap measurements or run heavy builds/hashing
while a capture is active. Traces include buffered history: filter the measured
interval before interpreting Insights totals.

Initial optimizations reuse immutable GPU binding buffers and stop witness cycles
only at an exact fixed point. They preserve per-frame animated surface evaluation.
The prepared 1-NPC comparison passes; 10/25 still fail the frozen budget, and 50
remains diagnostic. See `Docs/Migration/Evidence/NPCOutfits/performance-initial.json`.
Fixture version 2 requests the final body before clothing admission to avoid
timing an unintended double fit; comparisons require matching fixture versions.
The version-2 25-NPC pair still fails. The shared clothing regression validates
coverage/movement mechanically but requires separate screenshot review; the
first Female.01 extreme-morph capture after body transitions failed visually.
Later matched stock/cache and fresh-editor runs did not reproduce it; retain the
failed image without claiming its cause is established. The shared regression
temporarily disables background editor throttling and restores it on completion.
Use `--immutable-buffers 0` for a matched stock-provider diagnostic, keeping the
same body sequence. A single-body pass cannot certify the body-transition case.

`--selection` uses transient outfit definitions for repeated weighted generations,
zero-weight exclusion, fixed instance choices and saved precedence. It does not
change authored definitions or the roster. The sixteen-identity fixture passed;
the exact distribution is evidence of deterministic repetition, not a statistical
claim about a small sample.

`--profile-install` logs installation stages for a short diagnostic. Never use it
for acceptance FPS captures; `Measure.py` rejects that mode. `--prepared-binding 0`
retains repeated immutable uploads for A/B. Prepared reuse keeps only weak lookup
entries (maximum 128); live providers own the immutable buffers, while transforms,
morphs and runtime sliders remain per-instance. Three fresh packaged body cases
have passed explicit GC and normal shutdown; repeat after ownership changes.
Installation diagnostics also identify the exact component, binding and LOD pair.
Use `SummarizeInstall.py LOG --output NEW.json` on a retained bounded log interval.
Do not infer per-component retries from aggregate totals alone. The opt-in LOD
candidate exposed a 0/0 -> 1/0 -> 1/0 sequence: the surface tick installed a new
LOD before reconciling its requested V5 payload. Reconcile that selection first;
the corrected diagnostic observed exactly 0/0 -> 1/0 for all 54 components.
This does not by itself satisfy the crowd readiness/performance budget.

`RunNativePIE --count 3 --capture 20 --independent-npc-lod 1 --lod-transitions`
checks forced LOD 0/1/0/automatic, live component LOD, three Ready fitted garments
per pawn, visibility and a screenshot per step. Run representative bodies with
their existing archetypes; review images separately. It is diagnostic, rejected
by `Measure.py`, and does not prove extreme morphs or natural camera-distance
transitions. The host guards Slate callback re-entry during high-resolution
screenshots. `Sweep.py --independent-npc-lod 1` selects the experimental policy
for both sides of a matched pair; its default remains zero.
`--profile-gpu` records one detailed frame after readiness, with profiler UI off
and temporary settings restored. It is diagnostic and is rejected by `Measure.py`.
Use `python Tools/NPC/Outfits/SummarizeGPU.py LOG --output NEW.json` to extract
the last graphics GPU-0 frame and preserve its raw named events. These include
background HUB garments; do not label the totals as the fixture alone. New host
receipts hash the roster, graph and runtime sources before PIE, and comparisons
reject different configurations. Keep prior FAILs when refining the fixture.

Schema 36.2 retains bounded frame-local witness planes and fuses incidence
validation into the first cycle of the unchanged six-cycle solver. Every record
is still checked each frame; invalid data uses the original fallback. FullRag reserves
4,870,144 logical plane-buffer bytes per outfit; this is not a measured GPU memory
total. The 25-NPC frozen budget still fails despite the measured GPU reduction.
Any shared graph change requires all three bodies, extreme morphs, body swaps,
movement, coverage ownership and packaged GC/exit validation. Never certify it
from `Ready`, a short profiler frame or a successful C++ build alone.

Native receipts expose `initial_world_outfits` separately from the QA group:
definition/item-class residency at request, asynchronous callback durations,
restoration and engine-relative start/readiness. Use fresh packaged processes for
startup observations. Residency does not measure OS disk caches; negative stage
durations mean that stage did not run. Report what was already resident instead
of relabeling a partly prepared launch as an isolated cold-load benchmark.

Do not assume garment mesh reduction reduces fitting work. The withdrawn 25%
Rag LOD experiment retained roughly the same witness count while concentrating up
to 885 references on a vertex, above the unchanged 256 bound. Catalog freshness
did not detect that runtime incompatibility. Preserve its failed receipt; original
garments/bindings were restored. Any future LOD work must certify runtime incidence
and examine morph/animation transitions as well as render vertex counts.

Before authoring another LOD, run `Run.py ProbeGarmentLOD --ratios 0.5,0.25`
in an idle saved editor. It duplicates source garments into the transient package,
reduces only those copies and estimates witness work at 0.5/0.75/1.0 cm spacing.
Require its fresh `Saved/NPCOutfits/LODProbe/.../result.json`, unchanged source
hashes and no dirty packages. PASS means the probe completed, not that a binding,
visual quality or crowd budget passed. Estimates precede body-specific exclusions;
alternative spacings do not change the compiler. At 50% geometry and 0.5 cm,
shirt/pants estimates still exceed 256 references per vertex and all three exceed
the Vertex*16 plane capacity. Do not raise bounds or truncate constraints to pass.

The runtime LOD experiment is withdrawn. Its historical authoring procedures
are retained for evidence; see the current user direction above before considering
`AuthorGarmentLOD`. `CompileGarmentLOD` also republishes the single-LOD catalog
after withdrawal. Saving a Director policy schedules deferred compilation;
wait for that live work before another dependent action. The authoring host uses
a bounded 120-second MCP cleanup wait inside its 150-second outer limit. Preserve
timeouts as FAILs and inspect native completion instead of repeating authoring.
`InspectWitnessCounts` certifies the nine FullRag/body reduced bindings against
the unchanged incidence and plane-capacity bounds; it cannot certify visual fit.
`RunNativePIE --independent-npc-lod 1 --inspect-lod` captures actual component LODs
for a short diagnostic; `Measure.py` rejects diagnostic snapshots as acceptance.
Halving geometry with 0.75 cm sampling increased measured witness cost. Keeping
99% geometry reduced GPU cost, but neither candidate meets the full budget.

`RunNativePIE --lod-quality --count 1 --capture 45 --independent-npc-lod 1`
adds a diagnostic sequence on one disposable native pawn: source pose, matched
extreme-morph LOD0/LOD1 fronts, LOD1 side/back, automatic far/near, and locomotion.
Set `--body` and `--archetype` for each representative body. For a specific failed
fixture condition, `--lod-quality-case distance|movement --capture 15` avoids
repeating the whole sequence. Require native/host plus `lod-quality.json`; its
mechanical PASS is separate from the deliberately PENDING visual result.
Review every image before recording a visual verdict. `Measure.py` rejects
these moving-camera receipts as performance acceptance.

The diagnostic temporarily sets `r.SkeletalMeshLODBias=0` and restores the exact
prior value. The current quality setting is 2, which clamps a two-LOD garment
to LOD1 regardless of distance; do not mistake that policy for a failed distance
transition. Capture the actual viewport with `Shot`, avoiding high-resolution
render views around a LOD boundary. Record radius scale and bias in comparisons.
The far camera is diagonal to avoid occlusion by the player. Native crowd pawns
can lack controllers: enable `run_physics_with_no_controller` only on that
disposable pawn for physical locomotion, while retaining its animation setup.
This verifies moving fit, not AI pursuit or production controller behavior.

Native fixture version 3 waits for screenshot processing to finish before steady
capture starts. Readiness retains its original spawn-relative timestamp. Require
the completion flag, matching renderer-threading fields and equal fixture versions
for paired comparisons. Do not mix prior version-2 samples or claim that a near-
zero render-time counter proves the render thread is disabled.

`LogicQA --mode ai` retries only pursuit/spacing, using the current single authored
navigation volume to place the existing 1200 cm corridor. The old fixed arena can
fall outside a user-edited HUB. This wrapper never moves, rebuilds or saves bounds.
It preserves prior receipts before invocation; require newly written native and
host completion files. Use `--mode services` or `all` only when those gates need
revalidation. The native 150 s / host 180 s bounds belong to the existing logic host.

Use the protected build/launch wrappers. Preserve user-saved HUB changes separately
from outfit commits. Complete Editor/Game, Blueprint, representative PIE/visual,
cook and packaged gates before declaring the persistent goal complete.

For CPU attribution, run `RunNativePIE --trace --prewarm --count 25 --capture 12`
with a fresh name, then `python Tools/NPC/Outfits/ExportCPUTrace.py NAME`. The
exporter selects six seconds before a two-second teardown margin; inspect that
window if the fixture has an unusually long teardown. Its scope is all scene
garments, including map NPCs. Nested inclusive times must not be added together.
UE 5.8's statistics exporter includes GPU queues even with `-threads=GameThread`;
the compact summary therefore selects only named, owned CPU scopes. These short
traces are diagnostic and cannot satisfy the 30-second acceptance gate.

`--parameter-cache 0|1` controls exact live GPU-byte comparisons before the native
scalar/transform setters. Every live input is still evaluated each frame. First
use goes through the native setter, mismatches go through it again, and detach
clears validation. The host restores the prior CVar and records the option;
`Measure.py` rejects pairs with different settings. Validate morph changes, body
replacement and teardown when modifying this optimization.

The isolated initial-load map is `/Game/_Game/QA/NPCOutfitColdStart`, authored
once through `AuthorColdMap.py`. Preserve inherited NPC tags: replacing the tag
array removes `EF.BodyVariation` and prevents the placed probe from initializing.
Its owned game-mode copy retains ACF world services but uses DefaultPawn,
PlayerController and HUD from Engine; the production controller/HUD require a
player character and produce Blueprint errors with DefaultPawn. Never patch the
source game mode to implement this fixture.

Cook it with `Package-BodyPresets58.ps1 -AdditionalMap /Game/_Game/QA/NPCOutfitColdStart`.
Use `ValidatePackaged.ps1 -Map /Game/_Game/QA/NPCOutfitColdStart -RequireColdItems`
to require one placed initial NPC with a nonresident definition and three
nonresident garment classes, followed by Ready. The QA group is separate and
does not prove cold resources. The wrapper rejects any logged Error, preserves
the loading trace, and still requires GC, empty pending work and normal exit.
This does not establish OS disk-cache state or automatically prove nonresident
binding payloads; inspect the loading trace for those assets.

Outfit roster, definition and garment-class requests use the same high streaming
priority as clothing bindings. Admission remains one owner/frame, at most four
active owners, with the existing 20-second deadline. Named handles distinguish
the three stages. Failure text records the pre-failure state, elapsed time,
handle completion/cancellation and loaded/requested asset counts before canceling.
Do not diagnose a missing definition callback as a fitting or GPU failure. Check
the surrounding async-loading backlog and actual project streaming settings;
do not silently increase deadlines or global loading budgets. A first-PIE startup
run after a fresh editor launch is separate from a prepared performance pair.

`Run.py ClothingRegression --bodies Female.01,Male.01,Male.02 --max-fps 8`
is a frame-pacing diagnostic: preparation uses the original cap, then the chosen
cap applies only after garment Ready during morph movement and screenshots.
It records mesh/leader/AnimInstance identities and named bone positions/visibility
on the game thread at screenshot request. These are not GPU readbacks or proof
of exact screenshot-frame geometry. The cap and background throttle are restored
before EndPIE, including failure exits. Capping the entire preparation previously
failed the fixed overlap readiness assertion before producing any image; retain
that failure rather than treating it as evidence of a visual deformation.
