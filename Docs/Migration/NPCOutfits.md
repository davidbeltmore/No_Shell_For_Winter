# NPC outfits

## Scope and frozen acceptance budget (2026-10-11)

Target: `D:/Projects UE5/NoShellForWinter`; source remains read-only. Initial
snapshot `086fbd423569ab433c0b03393191d636ba881847`, clean branch
`feature/efclothing-v4-multicloth`. UE 5.8 target editor PID 12972, MCP
`http://127.0.0.1:8000/mcp`, `EditorAppToolset.IsPIERunning=false` at preflight.

Roster: 20 archetypes / 40 Male/Female variants. Body catalog: enabled NPC bodies
`Male.01`, `Male.02`, `Female.01`. Pilot: SkillGuy service and MeleeEnemy, then
eligible roster after pilot validation. Existing nonhumanoid/player exclusions
remain in `Tools/NPC/Roster.json`.

User accepted the proposed budget before measurement: global provisional 60 FPS
(16.67 ms/frame); at 25 NPCs incremental dressed-minus-undressed CPU <=2 ms,
GPU <=3 ms, frame p95 increment <=4 ms, warmed group outfit completion <=2 s.
Groups 1, 10, 25; 50 diagnostic only. Cold loading separate. Same scene, bodies,
seed, camera, resolution and animation conditions. Record 30–45 s steady capture,
spawn peak, readiness, memory/render resources where available and teardown.
No acceptance threshold may be changed after seeing results.

## Authorities and implementation in progress

- Native ACF owns inventory/items. EFEquipment coordinates destinations and
  reservations. Explicit `CreateForNPC` uses shared operations without the player
  discovery timer, UI notification, carry projection or native death adapter.
- `UProjectNPCOutfit` owns stable ID and garment/destination references.
  `FProjectNPCArchetype.Outfits`: empty disables integration, one entry fixes it,
  multiple positive weights use appearance seed xor a separate outfit salt.
  `UProjectNPCOutfitComponent.OverrideChoices` supplies per-instance selection.
- `UProjectNPCOutfitSubsystem` retains the small roster, admits one pawn/frame
  and up to four initializing outfits. This bounds initial attachment; it is not
  yet proof of a global binding-stream budget or measured crowd capacity.
- Appearance resolution calls the outfit queue only after applying body/morphs.
  Native equipment initialization and ALS idle precede equip. The same appearance
  record stores outfit ID/version plus the existing native inventory envelope.
  A versioned saved record takes precedence, including an emptied inventory.
- EFClothingMorph remains the fit/coverage authority. Only selected owners receive
  its runtime and equipment bridge. Global non-player enable remains false.
- Failures are explicit and bounded (20 s initial readiness). Initial grant failure
  attempts inventory rollback. No whole-character visibility suppression is added.

Live preflight: `Saved/NPCOutfits/inspect.json`. Protection baseline:
`Saved/NPCOutfits/Baseline/before.json` (7,643 protected files, 72.39 s).
The loaded settings confirm `bEnableForNonPlayerPawns=false`,
`MorphSyncIntervalSeconds=0`, `MaximumConcurrentBindingLoads=2`.

## Gates

| Gate | Status | Evidence |
|---|---|---|
| Target/editor/clean initial branch | PASS | MCP and initial Git/process inspection above |
| Scope and budget frozen | PASS | User accepted before implementation measurement |
| Protected baseline | PASS | before.json, initial commit above |
| Shared NPC equipment / FullRag pilot | PASS | Four variants, three bodies; evidence below, commit bb199a65 |
| Editor build and Blueprint compile | PASS | Final incremental Editor build + all 40 owned variants |
| Game build | PASS | Development builds with protected descriptor/receipt restoration |
| Pilot persistence and functional PIE | PASS | Modified shirt state, exact disk round trip, no duplicates |
| Loot / immediate recreate | PASS | Synchronous snapshot, both genders, exact remaining GUIDs, commit 72c8d694 |
| Cancellation during load | PASS | Destroy in Loading, zero requests after teardown |
| Weighted selection | PASS | 16 fresh-generation identity repeats, zero weight, fixed instance override and saved GUID precedence |
| Visual fit, movement, morphs, body swap | PENDING | Three-body shared coverage/movement passes; initial visual anomaly retained, controlled follow-up below |
| Native service interaction | PASS | Six service menus and two quest interaction contracts; SkillGuy clothed images reviewed |
| Native pursuit / spacing | PASS | Four natural pursuit/spacing samples in a clear authored-nav corridor; fresh gameplay-distance images reviewed |
| Performance 1 | PASS | Explicitly prepared 30-second pair after two optimizations |
| Performance 10/25 | FAIL | Frozen budget exceeded; details below |
| Performance 50 | PENDING | Diagnostic measured, no acceptance claim |
| Cold loading | PENDING | Preparation passes are not cold packaged loading |
| All eligible roster audit | PASS | 40 configured, 0 unconfigured, 0 failed variants; all 40 Blueprints compile; representative rollout below |
| Cook and packaged QA | PASS | Three bodies/archetypes, equip/unequip, explicit GC, zero pending, exit 0 and reviewed images |
| Protection and save preservation | PASS | 7,643 unchanged after package; four original saves unchanged; user HUB hash preserved |
| Skills and GitHub publication | PENDING | Skills updated/validated; publication pending |

## Work preservation

The initial editor had an unsaved `/Game/_Game/Hub/HUB` although Git was clean.
The user chose to save it personally. A subsequent read-only inspection confirmed
no dirty packages and Git showed `Content/_Game/Hub/HUB.umap` modified. This is
user work, not an outfit change; preserve it when committing/publishing.

The external `A:/Skills Codex/ef-clothing-unisex-workbench/SKILL.md` now correctly
documents RagFootWrap's live `ItemSlot.Armor.Feet`; skill validation passed.
Garment leaf destinations are Chest, Legs, Feet; no garment asset was modified.

## Pilot evidence (2026-10-11)

Editor build PASS: `Saved/NPCDAZ/outfits-editor-build-process.json` (304.473 s).
After adding the native fixture, one fixture-only type-deduction compile error
was corrected; final incremental build PASS in `outfits-editor-build3-process.json`
(16.086 s). Daz receipt guards passed. Protected comparison PASS: all 7,643
files unchanged (`Saved/NPCOutfits/Baseline/after.json`).

At pilot commit `bb199a65`, authoring passed with 4 configured variants and 36
intentionally unconfigured until rollout. Audit/Blueprint compile passed for
40 variants, zero errors, 11.921 s. The current `author.json` and `audit.json`
now describe the later full rollout below; they are not the original pilot receipts.
An inherited duplicate Ammo slot prevented initial rule initialization. Shared
slot normalization now removes duplicates before testing tag ancestry, preserving
weapons and all unique native destinations. Failed receipts remain in Saved.

Four-variant PIE PASS (27.36 s): SkillGuy/MeleeEnemy x Male/Female, exactly three
equipped Rag entries with quantity one, 3/3 Ready and no clothing issues.
The reviewed screenshot shows all three pieces on each fixture. This establishes
basic presentation; close movement/morph/coverage QA remains PENDING.
First-use readiness included 10.5–10.9 s on three fixtures and 0.572 s on one;
these are not a warmed crowd benchmark.

`Saved/NPCOutfits/Native/pilot_male02_01`: PASS for Male.02 body selection,
three rendered pieces, native death adapter absent, shirt unequip preserved by
reinitialization, inventory count unchanged, zero pending initialization after
destruction. Readiness 1.893 s. Screenshot reviewed: garment presence/basic fit
PASS; 2-second capture is functional evidence, not a performance sample.

Real disk round trip PASS in `Saved/NPCOutfits/Save` (6.656 s): two service NPCs,
unequip shirt -> save via EFEquipmentSaveLibrary -> re-equip -> load; exact saved
GUID/count/equipment/body/morph equality and zero duplicate GUIDs. Loot removal,
destroy/recreate, weighted-choice tests and cold packaged restoration remain
PENDING. Four existing user saves are guarded; restoration after the previous
session verified all four hashes and recovered ProjectIntimacy.sav exactly.

## Initial performance failures (before optimization)

Native fixture receipts passed their functional checks; their paired performance
comparisons failed. These are different gates. Samples use the existing HUB,
fixed fixture camera, matching body/seed/resolution and 30-second captures.
Fixture AI/movement are disabled, so this does not certify active crowd gameplay.

| Warm NPC count | Added CPU ms | Added GPU ms | Added frame p95 ms | Dressed frame p95 ms | Group ready s |
|---|---:|---:|---:|---:|---:|
| 1 | 13.955 | 1.312 | 15.347 | 56.092 | 1.397 |
| 10 | 106.856 | 58.312 | 113.706 | 152.378 | 5.913 |

Evidence: `Saved/NPCOutfits/Comparisons/perf1_01.json` and
`perf10_01.json`, referencing the four native raw receipts. Both groups reported
zero pending outfit initialization after destruction. Baseline frame p95 was
40.745/38.673 ms respectively: the existing scene already exceeds 16.67 ms.
The incremental clothing failures remain independent of that baseline.

Diagnosis is in progress. Instrumentation has been prepared to separate game,
render, surface-submission and reconciliation CPU times, but has not yet been
built or measured in the original pair. Subsequent instrumented diagnostic
`diag10_instrument_01` (10 seconds, not an acceptance run) measured mean surface
submission 56.050 ms, reconcile 0.337 ms, game thread 124.188 ms, render thread
141.768 ms and GPU 59.091 ms. User explicitly authorized closing/compiling/reopening
after saving HUB. Instrumentation build PASS (36.289 s).

The first targeted optimization is an owned per-instance ComputeFramework provider
for the existing surface graph, reusing immutable binding arrays across dispatches
and matching live owners. Scalars and all surface kernels still run every frame.
It validates reflected UE 5.8 layout before replacing instance providers, without
changing Engine/Marketplace code or graph assets. Failure retains the original
providers and logs the cause. A weak lookup is capped at 128 entries; live providers
and in-flight proxies own resource lifetime. `ef.ClothingMorph.ImmutableBindingBuffers`
selects the provider on subsequent garment installation for A/B diagnostics.
An unexported metadata accessor caused a link failure; using exported datatype
accessors corrected it. The first runtime attempt retained stock providers because
array values legitimately have zero inline bytes; correcting that bound produced
build PASS (9.541 s). Protected comparison still passes all 7,643 files.

`diag10_buffers_02` then passed its native fixture and reduced mean surface CPU
submission from 56.050 to 0.754 ms; game/render/GPU totals were 18.106/22.340/16.786
ms. Reviewed before/after overview images show the same ten garments and basic
fit; movement/morph extremes and packaged GPU validation are still PENDING.

The matched 30-second pair `perf10_cached_bare_01` / `perf10_cached_dressed_01`
still FAILS the frozen budget: added CPU 8.581 ms, GPU 11.393 ms, frame p95
10.693 ms, readiness 4.441 s, zero queued outfit initialization after destruction.
Cache optimization is effective, but this is not certification of the requested
crowd capacity. Original FAIL receipts remain intact.

GPU trace: `Saved/NPCOutfits/Native/diag10_gpu_trace_01/capture.utrace` with Insights
exports `timers.csv` and `gpu.csv`. The trace includes the engine's buffered
history; GPU export uses seconds 277–286, within the final fixture capture.
ComputeFramework kernels remain a large cost. Graph schema 35.1 adds an exact
fixed-point exit to the witness loop: no tolerance change, six-cycle maximum
retained for changing vertices. Editor build PASS (24.756 s); owned graph refresh
PASS in `Saved/ClothingMorphV4QA/SurfaceGraph`, with scoped rollback copies.
Performance and visual/regression gates for this shader change are PENDING.

The follow-up diagnostic `diag10_convergence_01` measured 9.522 ms GPU mean.
Subsequent acceptance pairs now explicitly prepare the same dressed group inside
the same PIE world, destroy it, then measure a fresh group. Actual quality CVars
and the preparation receipt are mandatory in `Measure.py`; merely labeling a run
`warm` no longer suffices. Earlier failures remain initial diagnostics, not proof
of warmed resource residency. The 25-NPC budget itself is unchanged.

| Count | Added CPU ms | Added GPU ms | Added p95 ms | Warm ready s | Result |
|---|---:|---:|---:|---:|---|
| 1 | 0.383 | 0.515 | 0.421 | 0.358 | PASS |
| 10 | 2.968 | 4.626 | 4.745 | 3.490 | FAIL |
| 25 | 7.670 | 11.555 | 10.260 | 9.679 | FAIL |
| 50 | 16.847 | 22.888 | 22.034 | 20.642 | Diagnostic |

All four groups drained outfit initialization to zero after destruction. Hardware:
RTX 5070 Ti, D3D12, viewport 1595x1222, quality groups 3. Controlled animation/AI
conditions remain as stated above. Compact evidence and exact source hashes:
`Evidence/NPCOutfits/performance-initial.json`. Raw receipts remain in Saved.
At 25, bare readiness was 1.583 s and dressed 9.679 s; the clothing initialization
cost requires further work. Live MCP confirms each Rag mesh has only one LOD;
shirt/pants/foot wraps have 7,767 / 9,081 / 2,176 vertices respectively.

`LifecycleQA.py` found a separate persistence failure: remove the shirt through
native inventory and destroy the NPC before the next tick, then recreate with the
same identity. The deferred snapshot lost that final change. The fix snapshots
coordinator-published mutations synchronously and rejects mutations during actor
destruction. Editor/Game builds PASS (17.004 / 34.668 s). The follow-up lifecycle
fixture PASS in 18.859 s: both Male.02 and Female.01 preserve removal and exact
remaining GUIDs after immediate recreation; destruction during `Loading` cancels
with zero queued requests. Receipt:
`Saved/NPCOutfits/Lifecycle/1791743297544539600/runtime.json`.

The earlier crowd fixture selected its requested body after the initial outfit
was ready, causing two fits during initialization. Fixture version 2 selects the
final body before admitting clothing, records body initialization separately and
requires equal fixture versions in comparisons. This is a measurement correction,
not a runtime speedup; the older receipts and failures remain intact. New matched
version-2 25-NPC pair `perf_fixture2_01_25` still FAILS: added CPU 7.405 ms,
GPU 11.618 ms, frame p95 9.878 ms, warmed readiness 7.280 s and spawn peak
1325.740 ms. No pending outfit work after destruction. Correcting the fixture
does not remove the crowd-capacity failure.

Same-frame unequip then real ALS disk save/load PASS in 6.453 s, preserving
GUID/count/equip/body/morph state without duplicates; compact evidence is
`Evidence/NPCOutfits/save-same-frame.json`.

Shared fitting regression after the provider/graph optimization: structural
coverage ownership and movement checks PASS for Female.01, Male.01 and Male.02
(66.094 s), but visual review FAILS the Female.01 extreme-morph capture. Male
captures are coherent. An isolated Female.01 run with stock providers is visually
coherent, but it omits the preceding body changes; the identical full sequence
with stock providers must distinguish cache behavior from body transition state.
Do not treat the script's structural `PASS` as visual certification. Evidence:
`Evidence/NPCOutfits/clothing-regression.json`.

The matched full stock-provider sequence (66.140 s) and subsequent cached-provider
sequence (66.813 s) both pass structural checks and have coherent reviewed images
for all three bodies. This does not reproduce the initial Female anomaly; its
cause is not established. Preserve that failed image and check a fresh editor
session before closing the visual gate. Compact A/B evidence:
`Evidence/NPCOutfits/clothing-ab.json`. Protection after these runs PASS: all
7,643 protected files unchanged (57.030 s).

A fresh-editor cached run after restoring the four user saves also passes the
three-body structural/visual sequence (68.234 s). It records 125 ms Slate steps;
the shared visual script had not disabled background editor throttling. The script
now temporarily disables it like the other runtime hosts and restores it on exit.
The controlled follow-up `1791744600153364600` passes all three reviewed bodies
in 64.719 s, still with some 125 ms Slate steps. This does not establish throttling
as the cause. The original anomaly is not claimed fixed or attributed to the
provider; subsequent warm, fresh and controlled runs did not reproduce it.

Native logic run: six service menus and two quest interaction contracts passed,
including dressed SkillGuy screenshots. The run then failed before pursuit with
`No navigation on authored HUB volume` (63.074 s native / 65.047 s host). Live
read-only `InspectNavigation.py` finds the current bounds centered at
(2520, -2420, 30), extent (1250, 1250, 100), so the old fixed corridor is outside
the map's current navigable area. `LogicQA.py` chooses the same 1200 cm native
pursuit corridor inside that existing volume; it does not move/rebuild/save it.
Native QA accepts `arena_x` / `arena_y`; unchanged AI/controller logic still has
to project, perceive the player, produce a path and move naturally. The retry is
now PASS: four native pursuit/spacing samples, 24.328 s host. The first relocated
corridor was still blocked by a placed enemy capsule: project sight uses
`ECC_Pawn`, whereas `LineOfSightTo` used Visibility and returned true. The fixture
now checks a 100 cm radius Pawn-profile corridor before play. It selects
(1920, -3232.5) here. No source registration, listener refresh, target override,
MoveTo, AI behavior change or map save is used for acceptance. Failed diagnostics
are retained. Fresh dressed melee Male/Female movement screenshots are coherent
at gameplay distance; see `Evidence/NPCOutfits/logic-corrected.json`.
Editor/Game builds PASS in 42.685 / 45.415 s, Daz guards restored.

Selection fixture `selection_01` PASS (14.937 s native / 20.219 s host): sixteen
identities generate fresh twice after deleting only their QA appearance records.
The 1:3 choices yield FullRag 3 / RagLower 13 both times; zero weight is never
selected. A seventeenth fixed-override pawn keeps its selected outfit and exact
inventory GUIDs after recreation with a different override. Definitions are
transient duplicates; roster and authored assets are unchanged. Evidence:
`Evidence/NPCOutfits/selection.json`. Editor/Game builds PASS 12.998 / 45.694 s.

`install_profile_01` isolates startup work with opt-in logging (not an acceptance
FPS capture). The final 30 garment installations spend roughly 8–43 ms each in
`UploadImmutableBinding`; topology validation is much smaller. Full raw timing
lines and a compact receipt are retained in `Evidence/NPCOutfits/install-profile.json`.
Generated LOD1 at 25% triangles was evaluated and withdrawn. The three garments
reduced vertices from 7,767/9,081/2,176 to 2,156/2,455/726, but their witness
workload stayed approximately constant and maximum per-vertex incidence rose to
462/885/502, above the unchanged runtime limit of 256. The bounded
`lod_diag10_01` prewarm failed visibly rather than accepting an incorrect fit.
Compiler freshness alone had not checked this runtime bound and was insufficient
certification. Both the LOD authoring and independent NPC LOD policy are restored
to the previously validated state; this is not a performance improvement.
The complete experimental patch, source/generated packages and rollback receipt
remain in `Saved/NPCOutfits/GarmentLOD/rollback_1791747842010807800`.
The exact 3 source packages and catalog backups were restored with the editor
closed, without changing HUB. Compact evidence: `Evidence/NPCOutfits/lod-experiment.json`.

Prepared binding reuse is being validated. A weak lookup capped at 128 entries
reuses existing immutable GPU arrays and their installation counts for the same
loaded binding, BuildGuid, authoring fingerprint, LOD pair and compiled graph.
Each receiving provider revalidates the reflected variable layout before an
atomic replacement; a miss uses the established full upload. Provider ownership
controls lifetime. No UObject, body pose, morph or mutable slider is cached.
`ef.ClothingMorph.PreparedBinding=0` retains the earlier upload path for A/B.
Representative runtime, visual and packaged checks now pass for this optimization;
the frozen 25-NPC performance budget still fails. Runtime commit: `b1d08eb4`.

The first prepared-binding diagnostic passes: final 30 installations take
0.710–3.109 ms (mean 1.789), with 57 actual prepared reuses across the two groups.
Ten measured NPCs become ready in 1.674 s and drain to zero pending after teardown.
The gameplay-distance image is coherent; close morph/transition review is separate.
Editor/Game builds PASS (10.263 / 43.655 s for final runtime code at this check);
the restored catalog passes freshness, 18/18 rows and a valid V5 manifest.
Evidence: `Evidence/NPCOutfits/prepared-binding-profile.json`.

The matched version-2 25-NPC pair still FAILS: added CPU 5.873 ms, GPU 11.577 ms,
frame p95 5.702 ms and warmed readiness 4.040 s, compared with 7.280 s previously.
GPU cost remains approximately unchanged, as expected for an installation-only
optimization. Spawn peak is 1357.163 ms; teardown pending count is zero.
No capacity or 60 FPS claim follows from this improvement. Exact measurements,
frozen limits and source hashes: `Evidence/NPCOutfits/performance-prepared-25.json`.

Prepared-binding shared regression PASS in 61.828 s: Female.01/Male.01/Male.02
extreme morphs, four active garments, 2→1→0 coverage owners and 234–237 cm movement.
All three captured images were reviewed as coherent. The historical anomaly is
retained without an invented cause. Evidence/images: `Evidence/NPCOutfits/prepared-binding-visual.json`.

FullRag rollout is authored for all 20 eligible archetypes / 40 gender variants.
The five named nonhumanoid assets and the existing player/menu exclusions remain
unchanged. Structural audit and all 40 Blueprint compilations PASS in 11.562 s:
40 configured, 0 unconfigured, 0 failed variants. An eight-character runtime
sample covers BaseCompanion, GunEnemy, Vendor and Zombie in both genders, including
all three registered bodies, with exactly three equipped items and no fitting
issues (10.469 s). Basic presentation image reviewed; partial occlusion and rear
characters do not establish close seam/combat quality. Evidence:
`Evidence/NPCOutfits/rollout.json` and `rollout.png`.

Latest Editor/Game builds including the explicit post-destruction GC fixture PASS
in 23.030 / 45.627 s. `Project.NPCOutfits.QA ... gc quit` exercises garbage
collection before normal packaged shutdown. `Tools/NPC/Outfits/ValidatePackaged.ps1`
uses a fresh isolated UserDir and a 180-second process limit; structural process
PASS still requires separate screenshot review. Its fresh-process label does not
pretend that clothes already loaded by HUB are cold resources at fixture spawn.

Cook/package PASS in 329.314 s (900 s bound), retained artifact
`Saved/BodyPresetsQA/Package_20261011_150042`. Fresh isolated executable cases:
Male.01/SkillGuy, Female.01/MeleeEnemy and Male.02/Zombie, three NPCs each.
All three pass native equipment, same-instance removal/reinitialization, explicit
GC, zero pending requests and normal exit 0. Each observes 36 prepared binding
reuses; all three logs contain zero errors. Gameplay-distance images reviewed as
coherent, including native weapons and bent zombie poses. These do not certify
extreme morphs outside the Editor or isolate first-resource latency from HUB.
Evidence and published images: `Evidence/NPCOutfits/packaged.json`.

Retention dry-run identified exactly the preceding package's Cooked/Staged
directories. Apply removed 4,989,040,367 bytes; old receipts/logs remain, and the
latest-package pointer retains the new validated artifact. No productive assets,
saves or rollback backups were cleanup targets. Post-package protection PASS:
7,643 unchanged files in 12.010 s. The four original user saves needed no restore
after isolated packaged tests. HUB still hashes to
`C484678ECD4A21407B59AE07268E84ADE022B5FF33BD8AE0CD93078CA669E78A`.

Packaged fixture/tools commit: `85138f7b`. The subsequent single-frame GPU
diagnostic includes the now fully dressed HUB background, not just its ten
spawned fixtures. Rounded event timings attribute about 8.40 ms of 10.14 ms
surface compute to the witness kernel (66 garment dispatches), versus about
0.66 ms each for the two cohesion passes. This directs the next optimization
at witness evaluation; it is not an acceptance benchmark. Raw named events and
source hashes are recorded in `Evidence/NPCOutfits/gpu-kernel-profile.json`.
