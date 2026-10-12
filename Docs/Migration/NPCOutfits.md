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

These gates describe the published baseline through `9838df51`. The opt-in
LOD candidates described at the end are experimental: their performance gates
fail, and their full visual/cook/package gates remain PENDING. The retained
package contains the previous validated runtime, not these candidates.

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
| Performance 1 | PASS | Schema-36.1 matched prepared 30-second pair; 0.078 ms added CPU, 0.311 ms GPU |
| Performance 10/25 | FAIL | Frozen budget exceeded; details below |
| Performance 50 | PENDING | Diagnostic measured, no acceptance claim |
| Cold loading | PASS | Isolated packaged initial NPC: definition, three garment classes and binding streaming path initially nonresident; no OS disk-cache claim |
| All eligible roster audit | PASS | 40 configured, 0 unconfigured, 0 failed variants; all 40 Blueprints compile; representative rollout below |
| Cook and packaged QA | PASS | High-priority prerequisite revision: three bodies/archetypes, 25-NPC HUB group and isolated cold map; equip/unequip, GC, zero pending, exit 0 and reviewed images |
| Protection and save preservation | PASS | 7,643 unchanged after package; four original saves unchanged; user HUB hash preserved |
| Skills and GitHub publication | PASS | Skills validate; exact parameter comparison and isolated packaged loading published in 8bbc72b1 and 74b38d95 |

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

The validated checkpoint through `25821cef` is published; user HUB remains
outside the task commits. The schema-35.2 loop pragma trial was withdrawn:
its single rounded GPU frame measured 8.14 ms witness work versus 8.40 ms before,
with a worse maximum dispatch (0.43 versus 0.21 ms). This is insufficient evidence
of a useful improvement. The schema-35.1 source and serialized graph were restored;
rollback and trial copies remain under `Saved/NPCOutfits/GPU/loop_01`.
See `Evidence/NPCOutfits/witness-loop-experiment.json`. No acceptance PASS is
inferred from this diagnostic. The next bounded experiment prepares witness
planes once per frame, retaining the ordered projection solver and full fallback.

## Frame-local witness preparation (schema 36.1, validation in progress)

The owned graph now prepares each face/edge witness plane once per animated
frame, after cohesion B. Its receiving vertices reuse that plane through the
same ordered six-projection solver and exact fixed-point exit. Incidence,
finite-value and correction-cap validation runs once per receiving vertex;
no sample, tolerance or animation update is dropped. A bounded `Vertex * 16`
float4 resource holds the planes. Denser bindings use the complete shared
evaluator through an explicit shader branch. A ternary first trial executed
more work and was slower; its evidence is preserved.

The calculated plane capacity is 4,870,144 bytes per FullRag and 121,753,600 bytes
for 25 outfits, excluding allocator overhead and other resources. This is a
capacity calculation, not a GPU residency measurement. The buffers belong to
the per-instance Optimus resource lifecycle; they do not cache an old pose.

Named GPU diagnostics contain 66 garment dispatches, including background HUB
NPCs. Preparation plus witness resolution measures 4.54 ms (0.76 + 3.78), versus
8.40 ms for the earlier witness kernel. These rounded single-frame observations
justify the optimization but cannot certify crowd capacity. Evidence:
`Evidence/NPCOutfits/prepared-planes-gpu.json`.

The latest matched 30-second 25-NPC pair still **FAILS** all frozen limits:
added CPU 8.875 ms, GPU 6.961 ms, frame p95 13.372 ms, warmed group readiness
4.465 s. Baseline/dressed p95 are 34.156/47.528 ms; spawn peak 1,339.491 ms;
pending work after destruction is zero. Both sides use the fully configured
roster and schema 36.1. Do not compare their total CPU times directly against
the older pilot-roster captures. See
`Evidence/NPCOutfits/performance-planes-prepass-25.json`; the preceding schema-36
pair is also retained in `performance-planes-25.json`.

Runtime work also avoids repeated name-to-string classification through a
bounded 4,096-entry name-only cache; active morph weights remain live. Render
confirmation now arms only for an unvalidated dispatch-failure generation,
preserving recovery instead of continuously rearming after success. These
changes do not establish a passing CPU budget.

Editor/Game builds pass for the fitting change (12.592/53.620 s). The shared
three-body regression passes in 62.188 s with Blueprint compilation, coverage
2→1→0, body transitions, representative extreme morphs and measured movement.
All three new screenshots were reviewed: the visible fitted surfaces remain
coherent. Evidence: `Evidence/NPCOutfits/prepared-planes-visual.json` and
`planes-morph_extreme_*.png`. The earlier unexplained visual anomaly remains in
the history; no cause or unrelated animation fix is claimed.

`SummarizeGPU.py LOG --output NEW.json` extracts the last graphics GPU-0 frame,
records named owned kernels and retains the raw frame. It is diagnostic only.
New performance hosts hash the roster, graph and relevant runtime sources before
PIE; `Measure.py` rejects mismatched configurations. Historical receipts without
these hashes remain historical evidence, not proof of a new configuration.

Initial-load telemetry records definition/item-class residency before requests,
asynchronous callback durations, engine-relative start/readiness and restoration
state for map-owned NPCs separately from the prepared QA group. Residency is
not an OS disk-cache assertion. Packaged observations of these new fields and
schema-36.1 cook/package/GC gates were subsequently completed below.

### Packaged schema-36.1 checkpoint

Cook/package passes in 336.314 s; retained artifact is
`Saved/BodyPresetsQA/Package_20261011_155525`. Three fresh processes pass for
Male.01/SkillGuy, Female.01/MeleeEnemy and Male.02/Zombie. All record zero error
lines, explicit GC completion, zero pending outfit requests and normal exit 0.
The three actual screenshots were reviewed for basic FullRag presentation and
native equipment/pose. See `Evidence/NPCOutfits/packaged-planes.json` and
`planes-packaged-*.png`. This does not certify packaged extreme-morph closeups.

Each fresh process observes twelve initially placed HUB NPCs reaching Ready.
The first process spans 12.500–13.640 engine seconds for their readiness; the
next two span 7.808–8.547 and 7.665–8.369 seconds. Their per-owner initialization
ranges are 0.229–1.894, 0.131–0.912 and 0.105–0.885 s respectively. Each launch
records two initially nonresident definition requests but zero nonresident item
class requests. These measurements therefore describe partially prepared HUB
startup, separately from the controlled warmed groups. They do not satisfy an
isolated cold garment/binding-load claim.

Retention dry-run then apply removes exactly 4,991,590,494 bytes from the former
`Package_20261011_150042/Cooked` and `Staged`; its receipts/logs remain. The latest
pointer retains the new validated artifact. The post-package protection check
passes for all 7,643 files in 74.580 s with no changes. User HUB retains SHA256
`C484678ECD4A21407B59AE07268E84ADE022B5FF33BD8AE0CD93078CA669E78A`.

Final code builds including startup observations pass: Editor 14.559 s and Game
50.955 s. A compiler warning in the transient selection fixture was corrected by
using `TObjectPtr` with its existing GC referencer; selection behavior is unchanged.

### Completed crowd measurements for the packaged fitting revision

Snapshot `864ddd08`; matched prepared 30-second pairs, full roster, same quality,
camera, body and seed. The 25-NPC pair preceded only startup-observation additions
to the QA envelope; the fitting graph and runtime producer hashes match this
checkpoint. Its earlier failure is retained rather than rerun without a relevant
fitting correction.

| Count | Added CPU ms | Added GPU ms | Added p95 ms | Warm ready s | Result |
|---|---:|---:|---:|---:|---|
| 1 | 0.078 | 0.311 | 0.284 | 0.265 | PASS |
| 10 | 2.668 | 2.623 | 2.550 | 1.736 | FAIL: CPU |
| 25 | 8.875 | 6.961 | 13.372 | 4.465 | FAIL: all four limits |
| 50 | 15.160 | 14.174 | 16.291 | 9.524 | Diagnostic, no capacity claim |

All groups leave zero pending outfit work after destruction. Spawn peaks for
1/10/25/50 are 63.080/639.483/1,339.491/2,624.663 ms. Whole-process physical memory,
raw-receipt hashes and all sample paths are recorded in
`Evidence/NPCOutfits/performance-planes-final-{1,10,50}.json` and the existing
25-NPC receipt. Whole-process memory cannot isolate GPU fitting allocations.
The scene baseline already exceeds 16.67 ms; this is not a 60-FPS certification.

The selection fixture after its GC-reference correction passes in 16.170 s native
/ 21.390 s host, preserving weighted repetition, zero weights and saved GUIDs.
Evidence: `Evidence/NPCOutfits/selection-gc-ownership.json`. The final PIE session
was closed with no dirty editor packages, four original saves were restored and
the target editor was reopened through the Daz-safe wrapper.

The persistent goal remains ACTIVE. Remaining acceptance gaps are the 10/25
performance failures, isolated cold garment/binding-load measurement and the
unestablished cause of the historical first female extreme-morph anomaly.
Current representative visual tests pass; that observation does not erase the
older failure or identify its cause. Do not turn these limits into PASS labels.

Fitting checkpoint `864ddd08` is published to GitHub. Final editor verification
uses native MCP `EditorToolset.EditorAppToolset.IsPIERunning=false` and a fresh
read-only inspection with no dirty packages. The project and global clothing
skills both pass `quick_validate.py`. `Evidence/NPCOutfits/final-state-planes.json`
records the protected snapshot, user-save restoration and retained package.

### Exact live parameter comparison (2026-10-11, after the user pause)

CPU scopes separate morph reads, native parameter writes, deformer enqueue and
immutable-provider proxy copying. The matched 25-NPC diagnostic traces include
map actors: 111 garment submissions/frame in both captures. Native parameter
writes cost 2.517 ms/frame with the optimization disabled and 0.622 enabled;
whole clothing component ticks fall from 5.857 to 3.847 ms/frame. These nested
scope times are attribution, not incremental crowd acceptance. Raw traces,
selected time windows and frame counts are in `Evidence/NPCOutfits/parameters-cpu.json`.

The producer now skips a scalar or transform setter only when its current GPU
bytes match the requested value exactly. First use must succeed through the
native setter; mismatches still use it; detach clears identifier validation.
Transforms use the same public Optimus matrix conversion as the native setter.
No morph input, animation update, surface pass, precision or frequency is removed.
`ef.ClothingMorph.SkipUnchangedParameters=0` retains all native writes. The PIE
host records/restores this option and acceptance rejects mismatched settings.

| Count | Added CPU ms | Added GPU ms | Added p95 ms | Warm ready s | Result |
|---|---:|---:|---:|---:|---|
| 10 | 2.184 | 2.683 | 2.734 | 1.619 | FAIL: CPU |
| 25 | 6.601 | 6.902 | 7.284 | 4.167 | FAIL: all four limits |

Both matched 30-second prepared pairs leave zero pending outfit initialization.
Spawn peaks are 534.108 and 1,319.979 ms. Source hashes, raw receipt hashes and
whole-process physical memory are in `performance-parameters-{10,25}.json`.
The frozen budget remains unchanged; the CPU improvement does not certify 25 NPCs.

Editor build passes in 59.091 s, Game build in 54.896 s. The shared three-body
regression passes in 61.953 s, including seven Blueprint compilations, extreme
morphs, body swaps, movement and coverage transitions. All three images were
reviewed (`parameters-visual.json` and accompanying images). Single-NPC native
equip/remove/reinitialize/GC also passes (`parameters_single_01`). Cook and
packaged validation for this revision are PENDING while its new package is built.

The first diagnostic preparation after reopening failed before fitting: outfit
definition callbacks had not completed by the existing 20-second deadline. The
log reported 136 asynchronous packages pending. Exact starvation cause remains
PENDING; the failure is retained at `cpu_parameters_0_01_warmup/runtime.json`.
After a successful single-NPC resource preparation, both diagnostic modes pass.
This does not erase the initial-load failure or increase its timeout.

An owned minimal map `/Game/_Game/QA/NPCOutfitColdStart` now contains one placed
SkillGuy and an owned game-mode copy with a generic DefaultPawn. It preserves
native ACF services and the inherited `EF.BodyVariation`/archetype tags. The first
generic-game-mode attempt lacked those conditions and is withdrawn; its nominal
native fixture PASS is not a valid initial-load gate. Corrected map preflight
`cold_map_preflight_03` passes the native outfit assertions and records the placed
NPC Ready separately from the QA group, but its inherited player controller/HUD
produce Blueprint errors with DefaultPawn. The scene gate is FAIL until those
test-only dependencies are corrected. It is not cold-load evidence.

`Package-BodyPresets58.ps1 -AdditionalMap /Game/_Game/QA/NPCOutfitColdStart`
cooks the additional map alongside HUB. `ValidatePackaged.ps1 -Map ...
-RequireColdItems` requires exactly one initial NPC with a nonresident definition
and all three garment classes nonresident at request, followed by Ready. It also
retains a loading trace. This does not measure OS disk-cache coldness. This new
packaged initial-load gate is PENDING until the process receipt satisfies it.

The cold fixture's owned mode now also uses generic Engine PlayerController and
HUD classes. Corrected preflight `cold_map_preflight_04` passes its native checks
and reports the placed NPC Ready without the prior Blueprint errors. Remaining
end-PIE LogUtils errors came from requesting the editor world before end-play
completed; the host now restores CVars through its still-live game world before
requesting end-play. The first cook candidate (`Package_20261011_211159`, 234.722 s)
predates the controller/HUD correction and is not the final validation artifact.

### Validated parameter package and isolated initial resources

Fitting code commit `8bbc72b1`. Final artifact:
`Saved/BodyPresetsQA/Package_20261011_212026` (cook PASS, 240.676 s bounded host).
Four fresh processes pass with zero logged Errors, explicit GC, zero pending
initializations and exit 0: HUB SkillGuy/Male.01, MeleeEnemy/Female.01,
Zombie/Male.02, and the isolated initial-loading map. All four images were
reviewed. Receipts, exact paths/hashes and images are in
`Evidence/NPCOutfits/packaged-parameters.json`.

The isolated map-owned NPC requested an initially nonresident definition and
three nonresident garment classes. Definition callback: 0.037594 s; item-class
callback: 0.040368 s; complete outfit initialization: 0.471596 s. Initialization
began at engine-relative 5.990279 s and reached Ready at 6.461874 s. These are
separate from the subsequently spawned functional QA group and from warmed
crowd measurements. The full process completed in 16.469 s.

All three slots log V5 payload streaming before the functional group starts.
The recorded `RequestV5BindingLoad` source returns immediately for resolved
objects; its handle is created only after `ResolveObject` fails. The streaming
log requires that handle, establishing the initially nonresident binding path.
This is source-correlated evidence, not per-binding latency or OS cold-disk
measurement. The loading trace is retained with the raw process logs.

Retention dry-run then apply removes 9,983,283,731 bytes from the previous validated
package and the superseded first candidate's Cooked/Staged directories. Their
receipts and logs remain; the latest pointer retains only `Package_20261011_212026`.
Protection passes again for all 7,643 files (58.450 s), and four user saves were
restored before packaging. User HUB SHA256 remains
`C484678ECD4A21407B59AE07268E84ADE022B5FF33BD8AE0CD93078CA669E78A`.

The isolated initial-resource gate is now PASS. Remaining gaps are the frozen
10/25 performance failures, the retained cold editor burst timeout under an
asynchronous loading backlog, and the historical first female visual anomaly's
unestablished cause. Current representative visual and packaged checks pass;
those observations do not erase the earlier failures. The persistent goal stays ACTIVE.

Publication checkpoint: `8bbc72b1` and `74b38d95` are pushed to
`origin/feature/efclothing-v4-multicloth`, including the two new QA assets in LFS.
The protected launch wrapper reopened the target editor (PID 57336); live MCP
reports PIE false and the fresh read-only inspection reports no dirty packages
(`Saved/NPCOutfits/Invocations/1791772435349939900/host.json`). Project outfit
skill validation, Python compilation and Git whitespace checks pass. The only
uncommitted asset is the user's previously saved HUB; it remains excluded from
outfit commits. Further performance work remains necessary, with no new
acceptance claim or adjustment to the frozen budget.

### Bounded prerequisite loading under editor backlog

The retained first-PIE failure occurred before the outfit definition callback:
no item classes had been requested. Its log records 136 pending async packages.
Project settings use a 1 ms asynchronous loading time slice with the loading
thread disabled. Those settings remain unchanged. Outfit roster, definition and
item-class requests now use `FStreamableManager::AsyncLoadHighPriority`, matching
the binding requests they precede. Admission remains one owner/frame, at most
four active owners, and the existing 20-second deadline remains unchanged.
Named streamable handles and failure diagnostics now retain the pre-failure
state, elapsed time, loading/completed/canceled flags and loaded/requested counts.

Editor and Game builds pass in 41.343 / 53.352 s with restored Daz receipts.
The first PIE after a fresh editor start (`priority_first_pie_01`) passes with
25 dressed NPCs, explicit GC and zero pending work. Group readiness: 13.087685 s;
body initialization: 2.552304 s. Two initial map NPCs request a nonresident
definition, whose callbacks take 6.021442 / 5.936143 s, reaching Ready at
6.863554 / 6.778175 s. All 12 initial map NPCs report Ready. The host takes
23.125 s; the reviewed crowd image is coherent at gameplay distance. The short
3-second capture is a startup diagnostic, not prepared performance acceptance.
Different editor starts have uncontrolled OS caches and background work; this
pass does not establish priority alone as the cause of the previous timeout.

Lifecycle regression `1791773000951735400` passes in 8.375 s: both genders retain
exact remaining inventory GUIDs/body after immediate recreation; destruction
while loading drains the queue. Six current save files were backed up, including
two earlier QA saves, and restored after the editor closed. All four original
user-save hashes still match the initial session.

Cook/package passes in 255.344 s. Retained artifact:
`Saved/BodyPresetsQA/Package_20261011_214531`. Fresh packaged processes pass:

| Case | Count | Process seconds | Result |
|---|---:|---:|---|
| Isolated cold map, Male.01/SkillGuy | 3 QA + 1 initial | 32.558 | PASS |
| HUB Male.01/SkillGuy | 25 | 20.294 | PASS |
| HUB Female.01/MeleeEnemy | 3 | 18.263 | PASS |
| HUB Male.02/Zombie | 3 | 18.268 | PASS |

Each process has zero logged Errors, normal exit 0, explicit GC and zero pending
work. Four actual images were reviewed. The isolated initial NPC starts with a
nonresident definition and three nonresident garment classes; callbacks take
0.013398 / 0.018606 s, full outfit initialization 0.838381 s. Engine-relative
start/ready times are 21.797559 / 22.635940 s. The larger full-process startup
compared with the previous package is not presented as a priority improvement.
Native QA groups remain separate from the isolated initial NPC observation.
Source hashes, original failure, build/runtime receipts and images:
`Evidence/NPCOutfits/load-priority.json`.

After successful validation, retention dry-run/apply removes 4,991,701,361 bytes
from the previous package's Cooked/Staged directories, preserving receipts/logs.
The frozen prepared CPU/GPU/p95/readiness failures remain unchanged. No new
steady-state acceptance run is claimed for a loading-only change. The historical
female visual anomaly remains documented without an established cause.

Protected comparison passes again: 7,643 files, zero changes, 34.620 s. User HUB
still hashes to `C484678ECD4A21407B59AE07268E84ADE022B5FF33BD8AE0CD93078CA669E78A`.
The last package's two removed directories are confirmed absent; its receipts
remain, and the latest-package pointer resolves to the validated 214531 artifact.

### Controlled frame-pacing diagnostic for the historical visual anomaly

Runtime remains published commit `70ba613f`; this follow-up changes only Python
QA and documentation. Source inspection confirms that body-mesh change events
release old garment state and the reconciliation watchdog independently compares
the body's current mesh with its recorded asset. No duplicate recovery mechanism
or speculative runtime correction was introduced.

`Run.py ClothingRegression --bodies Female.01,Male.01,Male.02 --max-fps 8`
now supports a bounded pacing diagnostic. The first attempt applied the cap
throughout preparation and failed a fixed overlap readiness assertion in
16.969 s, before producing any image (`1791774146576038700`). That mixes loading
latency with pose behavior and is retained as a failed fixture execution.
The corrected tool prepares at the original frame cap, checks garment Ready,
then applies the requested cap during extreme-morph movement and screenshot
capture. It restores the cap after each case and on final exit. The original
120-second total bound is unchanged.

Corrected execution `1791774273708138500` passes in 62.422 s, including the seven
shared Blueprint compiles, coverage transitions, three extreme-morph cases and
movement. All three actual images are coherent. Three game-thread snapshots
record the requested cap at 8, mesh/leader/AnimInstance identities and finite
named bone positions/visibility at screenshot request. These observations are
not same-frame GPU vertex readbacks. Live MCP confirms `t.MaxFPS=0` afterward.
The host now restores CVars through the live game world before requesting
EndPIE, avoiding the previous editor-world query during asynchronous teardown.

Evidence and images: `Evidence/NPCOutfits/frame-pacing.json`. This diagnostic does
not reproduce or explain the historical anomaly; that issue remains PENDING.
No new crowd performance acceptance result is claimed. The last validated game
package remains `Package_20261011_214531` because runtime/content are unchanged.

### Transient LOD feasibility probe

At parent snapshot `d701a51f`, an editor-only native helper duplicates each source
garment into `/Engine/Transient`, uses native skeletal reduction on the copy, and
checks source LOD0 serialization, LOD count and package dirtiness afterward.
`Run.py ProbeGarmentLOD --ratios 0.5,0.25` also compares source file hashes.
It saves no assets and runs no PIE. Editor build passes in 60.760 s; all six
reductions finish in 3.032 s. Receipt:
`Saved/NPCOutfits/LODProbe/1791775744650260100/result.json`.

These are analytic upper bounds before body/PreserveUpstream exclusions, not
compiled bindings or frame-time measurements. Each cell below is witness count /
maximum references on one vertex. The current compiler uses 0.5 cm spacing.

| Garment | Triangle fraction | Vertices | 0.5 cm | 0.75 cm | 1.0 cm |
|---|---:|---:|---:|---:|---:|
| Shirt | 50% | 4,024 | 83,383 / 287 | 42,933 / 133 | 28,450 / 81 |
| Shirt | 25% | 2,156 | 80,485 / 462 | 41,435 / 210 | 26,196 / 132 |
| Pants | 50% | 4,655 | 103,322 / 333 | 53,100 / 158 | 34,994 / 96 |
| Pants | 25% | 2,455 | 108,865 / 885 | 55,574 / 429 | 35,088 / 252 |
| Foot wraps | 50% | 1,209 | 20,631 / 193 | 10,745 / 98 | 7,002 / 63 |
| Foot wraps | 25% | 726 | 20,916 / 502 | 10,827 / 243 | 6,803 / 142 |

Geometry reduction alone does not provide a viable bounded candidate at current
sampling density: all six estimates exceed the Vertex*16 plane capacity, and
five exceed the 256-reference limit. This does not certify rejection for every
body-specific binding, but rules out assuming a universal safe reduction.
50% geometry with 0.75 cm sampling fits both numerical bounds in all three
estimates; 25% with 1.0 cm does too, with pants close to the incidence bound.
These are candidates for future distance-quality work, not production changes.
Neither alternative sampling density is implemented. No runtime cap is raised,
no witness is truncated and the player/near geometry remains unchanged.

Protected comparison passes: 7,643 unchanged files in 7.360 s; all six backed-up
saves and the user HUB hash are unchanged. MCP confirms PIE is stopped. Compact
evidence: `Evidence/NPCOutfits/lod-feasibility.json`. Runtime and content remain
at the previously packaged revision; no new cook or performance pass is claimed.
The frozen crowd failures and historical visual anomaly remain open.

### Runtime LOD candidates: bounded tests, not accepted

The editor-only feasibility result justified testing an explicitly disabled-by-
default NPC LOD policy (`ef.ClothingMorph.IndependentNPCLod=0`). It restores prior
leader/parent LOD flags on release, keeps player-controlled pawns on the existing
policy, and does not change solver iteration or incidence limits. Generated LODs
use their actual reduced skin weights with section-local bone indices translated
through the section bone map. LOD0 sampling stays 0.5 cm. A Director row setting
controls only LOD1+ witness spacing; its original 0.5 cm value preserves existing
fingerprints, while a changed value invalidates all affected body variants.

Editor build passes in 42.751 s. The extended native body-variant contract passes
in 70.6 s, including policy propagation, fingerprint invalidation/restoration and
rejection of out-of-range sampling. Authoring backs up the three garments,
Director and associated generated outputs. The native operation verifies LOD0
serialization, skeleton and morph names before saving. First backup:
`Saved/NPCOutfits/GarmentLOD/1791776577225473800/Backups` (27 files).

At 50% triangles and 0.75 cm spacing, all nine reduced body/garment bindings pass
the unchanged 256-reference and Vertex*16 plane bounds; maximum incidence is 158.
The three-Male.01 functional run passes with nine visible garments observed at
LOD1, equip/unequip, GC and zero pending work. Its image is coherent. Nevertheless,
the 25-NPC prepared 30-second pair fails. A subsequent same-editor GPU diagnostic
observes all 75 fixture garments at LOD1: witness work is 9.28 ms for all 111 scene
garments versus 5.85 ms in the LOD0 control. These rounded one-frame totals are
diagnostic, not acceptance measurements or proof of the underlying solver cause.

A second candidate retains 99% of triangles while keeping 0.75 cm sampling, to
separate geometry simplification from sampling reduction. All nine reduced
bindings again meet both bounds. The GPU diagnostic observes 75 fixture garments
at LOD1 and 3.09 ms witness work for the scene. The full pair reduces incremental
GPU time but still fails every frozen threshold:

| Candidate | CPU increment | GPU increment | p95 increment | Prepared readiness |
|---|---:|---:|---:|---:|
| 50% geometry / 0.75 cm | 6.269 ms | 8.974 ms | 5.110 ms | 6.085 s |
| 99% geometry / 0.75 cm | 6.899 ms | 4.792 ms | 8.178 ms | 6.184 s |

Both use matching configurations, the original camera, 30-second captures and
explicit preparation; teardown leaves zero pending work. Both actual 25-NPC
images were reviewed and are coherent at that distance, with occlusion/cropping
limitations. They do not certify extreme morphs or near/far transitions.
Evidence: `Evidence/NPCOutfits/lod-runtime-candidates.json` and its two images.

Saving the Director schedules its compiler after the authoring script returns.
Both authoring actions passed, but the host's original 45-second MCP cleanup wait
expired behind this deferred work. Those host FAIL receipts remain intact. The
compiler subsequently completed; explicit V4 validation/V5 publication reused all
18 fresh bindings, and independent witness audits passed. The bridge now accepts
an optional bounded MCP timeout (default still 45 s); this authoring action uses
120 s within its 150-second outer bound. A timeout never permits restarting work
without checking the live editor and compiler first.

The new captures report almost zero render-thread time, unlike earlier captures.
This is an observation, not proof that rendering became single-threaded. The
engine's Slate presentation path updates this statistic. A read-only OS thread
name query was unavailable. Explicit render/RHI threading flags were added to the
native fixture; their Editor build passes in 17.806 s. A fresh editor observation
is pending before drawing further CPU conclusions. All six session saves were
checked/restored with the editor closed; only ProjectIntimacy required restoration.

The fresh three-NPC observation `lod_fresh_thread_state_01` passes in 15.812 s:
threaded rendering, requested threaded rendering and a separate RHI thread are
all true. Render-time samples still average about 0.0012 ms. The single-threaded
rendering hypothesis is therefore unsupported; restarting did not repair that
counter. No acceptance result was changed on this basis.

Source review also found that fixture version 2 requested its screenshot at the
same instant it started steady-state capture, including the screenshot's CPU
readback/processing stall in the samples. Version 3 waits for Unreal's screenshot-
processed event, then starts measurement on a later tick. It preserves the
original readiness timestamp and reports screenshot-to-capture separation. The
original 30-second and frozen performance thresholds remain unchanged. The
comparison tool rejects mixed fixture versions and, for version 3, requires
completed screenshots plus matching explicit renderer-threading observations.
The first build found a shared-pointer callback signature mismatch; the corrected
Editor build passes in 15.969 s. Version-3 runtime measurements follow below.

The version-3 prepared 25-NPC pair completes with all screenshot-completion and
threading checks satisfied. Screenshot-to-capture separation is 0.286 s bare /
0.263 s dressed. The frozen result is still FAIL: CPU increment 3.029 ms, GPU
4.845 ms, p95 increment 3.670 ms (this individual threshold passes), readiness
6.185 s, zero pending after teardown. The actual dressed image is coherent at the
fixture camera; see `Evidence/NPCOutfits/lod-dense-v3-25.png`. A fresh editor and
fixture correction changed between versions, so the CPU delta difference is not
attributed entirely to the screenshot fix. Full quality/transition and packaged
acceptance of this candidate remain PENDING. No failed threshold was relaxed.

Checkpoint: Development Game build passes in 59.664 s with Daz receipt/descriptor
restoration. The final protected comparison passes: 7,643 files unchanged in
7.320 s. All six saves from `Session_20261011_231705` and the user HUB hash are
preserved after closing the editor; the editor is reopened through the protected
launcher. The outfit skill validates. The candidate remains opt-in and no new
package supersedes the validated `Package_20261011_214531` artifact.

## Reconcile the requested LOD before installing its producer (2026-10-11)

The next bounded installation diagnostic adds component paths and exact LOD pairs
to the existing opt-in profile log. `install_identity_02` records 54 garments
(12 map NPCs plus two groups of three): 48 perform `0/0 -> 1/0 -> 1/0`, six
perform `0/0 -> 1/0`, for 156 installations. The surface tick was installing
the new LOD against the previous requested payload selection; reconciliation
subsequently invalidated that identity and installed the same LOD again.

`UEFClothingMorphV3RuntimeComponent::TickSurfacePasses` now releases that producer
and requests reconciliation when the predicted garment LOD differs from the
requested binding LOD. Selection of the exact V5 payload precedes installation.
Body-only LOD changes retain their existing path. No cache ownership, shader,
solver, admission limit, deadline or threshold changes are involved.

`install_reconcile_01` confirms exactly `0/0 -> 1/0` for all 54 garments: 108
installations, with no passthrough warnings. Both diagnostics complete with native
and host PASS. Total installation CPU is 919.082 ms before / 717.543 ms after
across the full diagnostic interval, including cold initial immutable uploads;
these totals are not steady frame costs or a crowd acceptance result. Raw logs,
component sequences and fresh receipts remain under `Saved/NPCOutfits/Native`.
`SummarizeInstall.py` retains source hashes, stage counts and per-component LOD
sequences without labeling a diagnostic as a performance PASS.

The three bounded transition cases (`lod_transitions_male01_01`,
`lod_transitions_female01_01`, `lod_transitions_male02_01`) each check nine garments
through forced LOD 0, LOD 1, LOD 0 and automatic selection. Host/native PASS,
four completed steps, zero pending after destruction, explicit GC. All twelve
actual images were reviewed and show coherent clothing at the fixture distance.
Representative images and receipts are in `Evidence/NPCOutfits/lod-reconcile.json`.
This covers forced LOD transitions on ordinary poses; extreme NPC morphs at LOD1
and natural near/far camera transitions remain PENDING. High-resolution capture
can re-enter Slate callbacks; the host now guards re-entry and marks screenshot
requests before invoking the capture API. Acceptance comparisons reject this
diagnostic mode. Subsequent receipts also hash the Python host itself.

The matched, prepared, 30-second version-3 pair `perf_lod_reconcile_01_25` remains
FAIL: CPU +5.908605 ms, GPU +4.623237 ms, frame p95 +6.553299 ms and readiness
5.368716 s. Bare/dressed p95 are 33.832199/40.385498 ms; pending work is zero.
Readiness is lower than the prior 6.184630 s observation, but exceeds 2 s.
Changes in steady CPU/frame timing between separate pairs are not attributed to
this initialization-only correction. No thresholds are changed.

The shared LOD0 player regression `ClothingRegression/1791780570160364000`
passes in 62.078 s with 15 coverage/morph/movement checks and Blueprint compile
PASS. All three actual extreme-morph screenshots were reviewed and are coherent.
The previously retained intermittent Female visual anomaly remains unexplained;
this pass does not establish its cause. The first identity diagnostic invocation
could not connect while the restarted editor was still loading; it did not start
PIE. Its FAIL receipt is retained, and the runner now decodes failure logs with
replacement for non-UTF8 Windows error text rather than masking the error.

The independent NPC LOD candidate remains disabled by default. This correction
does not certify its remaining performance, full visual, cook or packaged gates;
the retained validated package still contains runtime `70ba613f`.

Checkpoint builds: identity instrumentation Editor PASS 54.882 s, corrected
Editor PASS 23.987 s, Development Game PASS 49.750 s; Daz descriptor and receipt
guards pass. The protected comparison passes in 45.470 s with 7,643 unchanged
files. All six saves are restored after each editor session (`233006`, `233831`,
`234309`); HUB retains SHA256
`C484678ECD4A21407B59AE07268E84ADE022B5FF33BD8AE0CD93078CA669E78A`.
The outfit skill validates, Python tools compile, and the diff whitespace check
passes. The editor is reopened through the protected launcher.
