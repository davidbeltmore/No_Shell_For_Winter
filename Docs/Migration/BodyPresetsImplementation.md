# Body presets implementation

Started 2026-10-08 15:20 America/Bogota. Body-feature gates completed at 17:50. Initial estimate 4–8 hours;
this is a planning range, not a promised completion time. Revise using measured build,
asset processing and cook durations. Existing user edits remain outside task commits.

| Block | Initial estimate | Exit metrics | Status |
|---|---|---|---|
| Baseline and Male2 conversion diagnosis/repair | 30–60 min | Protected baseline; exact mesh parent comparison; local skeleton valid; 416 morph names preserved | PASS: conversion, binding and visual gate |
| Shared catalog, player selector, legacy saves | 60–120 min | 2 Male + 1 Female definitions; switch/cancel/save/reload/fallback tests pass | Native contracts and visible creator PASS |
| NPC appearance selection/persistence | 60–120 min | Deterministic identities; weighted body and supported morph selection; reload stability; existing policy preserved | Real 12-NPC ALS round trip and clean process exit PASS |
| Clothing, visual integration, packaging | 90–180 min | Build/Blueprint/PIE/visual/cook/package gates; no protected drift; measured performance | Clothing, visuals, cook/package and packaged body execution PASS |

Broad tests should exercise distinct risks: conversion, skeletal animation, appearance
state, clothing/coverage, UI, NPC lifetime, save migration, missing inputs and packaged
asset loading. Repeat only on relevant changes or failures. A failed operation gets
a bounded diagnostic attempt and then a different evidence-based approach; timeouts
never count as passes. No fabricated performance targets or percentage completion.

Evidence root: `Saved/BodyPresetsQA`; baseline captures scoped candidate rollback files
and hashes the protected plugins/assets including the shared UEFN skeleton.

## Measured checkpoint, 2026-10-08 15:49 Bogota

- Protected baseline: 7,643 files, 71.22 s. Post-repair rehash: 27.25 s, zero changes.
- Male2 repaired to its own existing skeleton. 416 morphs preserved; native parent compatibility PASS.
- Six real PIE player switches: 53–84 ms per warm call; both body components preserve their animation classes. Visual motion still PENDING.
- Native `NoShellForWinter.BodyPresets.CatalogAndState`: PASS, 73.79 s including editor startup; 1,000 paired seeded choices plus selection, gender memory, cancel, SaveGame codec, legacy path migration and removed-preset fallback.
- Latest substantial build: 51.85 s. Test fixture rebuild: 9 s.
- Three preceding native attempts failed in test setup (array self-alias, then temporary-world creation); final fixture uses a transient character in the initialized editor world. These are retained as failed evidence, not passes.
- Existing user changes to `Config/DefaultGame.ini` and `HUB.umap` are preserved. Baseline PIE reports Calysto configuration errors; unrelated startup warnings remain separately attributable.

Native evidence: `Saved/Migration/EFClothingMorphV51/NativeAutomation_BodyPresets_20261008_04/Report/index.json`.
Build evidence: `Saved/BodyPresetsQA/BuildEditor05.log`, `BuildEditor06.log`.
Snapshot: base Git `6a0fc12f4c88045cba8449de5d53ef8ec5c2e76c`, task working tree plus scoped backups in `Saved/BodyPresetsQA/Baseline`.

## Measured checkpoint, 2026-10-08 16:03 Bogota

- Core implementation commit `aaae20ae`: gender-scoped catalog and persistent player selection.
- Local body clothing contract: explicit skeleton compatibility plus skin-palette bones and their ancestors. Imported unused internal branches do not invalidate a garment. Both mesh and separate skeleton fingerprints remain mandatory for local bodies.
- Clothing compiler and runtime sync: 18/18 valid combinations, 0 invalid/stale; six source garments, Female/Male/Male2. Original 12 bindings reused. Male2 was missing its default deformer; assigned the established DQS deformer to this candidate only.
- Actual 12-NPC HUB test: PASS, 23.313 s total; ALS save 0.203 s, load 3.765 s. Both male bodies reachable; different morphs within the same body; player excluded; all 12 identities/body IDs/profile values/skin colors equal after disk reload. Actor spawning took 138 ms but **excludes deferred appearance work**, so it is not the generation cost.
- NPC eligibility now compares loaded class ancestry to configured soft paths; newly loaded classes no longer wait for every enemy class to be loaded.
- Evidence: `Saved/BodyPresetsQA/npc-save.json`, `NPCSave02.log`, `SaveGuard_20261008_160053/SaveGuard.json`, `clothing-authoring.json`.
- `DefaultGame.ini`: retain all prior user deletions, add only the body-catalog cook-directory rule required by this task. This task addition must be staged independently from the preexisting deletions.
- Visual review, fresh unconverted-import automation, cook and packaged execution remain PENDING. No whole-feature completion claim.

## Diagnostic checkpoint, 2026-10-08 16:25 Bogota

- Visual gate FAIL: Male2 reference pose/elevated feet; RagPants visible distortion. Numeric clothing certification did not prove animation or fit. Evidence: `Visual_20261008_160549`, `visual-review.json`.
- Evaluated component transforms show the failure originates in CharacterMesh0, then is copied to SkeletalMesh. Male1 selects `PSD_Dense_Stand_Idles`; a skeleton-changing switch recreates the AnimInstance and loses the fragment-authored ACF moveset/overlay tables. ACF movement retains its BeginPlay instance pointer. This also affects returning to Male1 after Male2.
- Targeted restoration of the tables and active layers recovers the Motion Matching database. The evaluated feet remain wrong; reference-pose/retarget investigation is still PENDING. Do not certify the proposed bridge from database selection alone.
- Project-owned ACFU bridge now reconnects the cache and restores only declared layer tables and active gameplay tags when the animation instance changes. Editor build 11 PASS, 30.22 s; cold runtime verification PENDING.
- Diagnostic evidence: `pose-evaluation.json`, `layer-restore.json`, exported chooser T3D and Blueprint node reports under `Saved/BodyPresetsQA`. These are diagnostic fixtures, not complete gameplay passes.
- Game build 01 PASS, 105.09 s, predates the new animation bridge and must be rebuilt later. No cook or package pass yet.
- Diagnostic SaveGuard restored the user's slots after editor closure: `DiagnosticSaveGuard_20261008_161026/SaveGuard.json`.

## Measured checkpoint, 2026-10-08 16:55 Bogota

- Animation repair now verified through the ordinary body-selection path. Added the missing post-swap appearance notification, repaired ACF's stale AnimInstance cache/layer tables, and aligned the candidate's **local skeleton animation reference frames** to the established UEFN common hierarchy. Candidate mesh bind pose, weights and morphs remain separate and unchanged by the frame repair. The private bone branches retain their own hierarchy.
- Visual02 (`Visual_20261008_163647`): 39 numeric clothing/coverage/movement checks, 45 captures, 7 Blueprint compiles; normal Male2 pose and RagPants deformation recovered. Clean editor exit after replacing a static strong catalog pointer with a GC-owned settings reference.
- Male2 material slot `Genesis9_Torso_Back` was null. Assigned the candidate's existing `Genesis9_Body` material. No shared material changed.
- Added optional per-preset `MorphPhysicsAsset`; existing explicit settings overrides retain precedence. Authored `/Game/_Game/Characters/Physics/PA_Male2_Morph` from its own imported physics asset, with seven target-specific secondary-body constraints. Catalog references the existing Male/Female profiles without restoring unrelated user INI deletions.
- Visual03 (`Visual_20261008_164909`): another 39 checks and 45 captures after material/physics changes; 7 Blueprint compiles PASS, clean editor exit. Reviewed Male2 shirt, pants, foot wraps, underwear coverage and moving extreme body morph: material resolved, feet on ground, no previous reference-pose/spike failure. Full packaged validation remains pending.
- Editor build 16 PASS, 38.04 s. Build 17 failed at a C++ TObjectPtr deduction in the new conversion helper; fixed directly. Build 18 PASS, 8.26 s.
- New G9 conversion adapter and reusable `Tools/BodyPresets/OnboardBody.py` are implemented but **fresh-import certification PENDING**. Adapter executes a versioned G9 recipe using SkeletonModifier, commits against an isolated tree, replaces only the candidate's local tree, rebuilds only its local physics, checks morph-delta digest/material/LOD preservation and rejects a second conversion. It never calls the vendor's final shared-skeleton merge.
- Current scope limits: future non-G9 hierarchies require another validated recipe; hair-specific fitting is not established by body compatibility; network appearance replication is not claimed. Existing `Blink` configuration warnings are attributable to the pre-task INI state and are distinct from supported `Eye Blink Left/Right` morphs.
- Remaining blocks: fresh-import negative/idempotence tests and cold reload, creator/animation/physics runtime gate, final NPC regression, game rebuild, HUB cook/package execution, protected hash review and scoped commits. Initial 4–8 h range remains a planning range; about 95 min elapsed at this checkpoint, no cook duration measured yet.

## Conversion and geometry diagnostic, 17:05 Bogota

- Fresh FBX fixture imported 416 morphs and 196 bones from the user's existing `D:/DAZ/Male2/UpdatedFBX/Male2.fbx`, into an isolated `/Game/DazToUnreal/__BodyPresetQA` folder. No DAZ export modified. The first fixture failed before import because an editor-only import property required `set_editor_property`; corrected. The second imported mesh/skeleton but omitted its physics asset, so the converter refused with no bone mutation. The next fixture explicitly creates its own physics asset and preserves detailed errors through a string-returning wrapper.
- MikkTSpace zero-normal warning reproduced **during fresh FBX import, before any skeleton conversion**. Raw fixture: 71,079 render vertices, 3,077 zero normals, 4 zero tangents, 0 non-finite positions. Existing Male2: 70,894 vertices, the same 3,077 zero normals and 4 zero tangents. Existing Male: 2,817 zero normals/3 zero tangents; Female: 1,015/0. This warning is separate from skeleton parent incompatibility and is not caused by assigning a local skeleton. Exact source-face attribution/geometry repair is not claimed; preserving morph topology takes precedence over an untested rebuild.
- Runtime01 failed an overbroad fixture assertion: the retarget follower inherits ACF properties but does not own a locomotion layer. The assertion now targets ACharacter::GetMesh(), and additionally verifies the unique physics owner and live simulation. Runtime01 also exited with an access violation after LogExit; preserved as a failed process gate, despite restoration of saves. Runtime02 is the fresh retry with released Python wrappers on shutdown.
- Protected rehash after profiles: PASS, 7,643 files, 28.16 s, no changes. NPC implementation commit `35872624` follows core commit `aaae20ae`.

## Runtime checkpoint, 17:07 Bogota

- Runtime02 PASS, 22.871 s: Male1 -> Male2 -> Female -> Male2, both skeletal components show changing evaluated poses, standing feet stay near ground, animation-source ACF locomotion layer remains valid, exactly one physics owner with seven live simulating constraints, Male2 moves 312.941 cm.
- Creator Previous/Next handlers switch Male2 -> Male1 -> Male2; male option count is exactly two. `Saved/BodyPresetsQA/Runtime/CreatorMale2.png` visually confirms Body preset / Preset 2 and the morph controls. This is handler-level automation plus visual UI review, not a claimed physical mouse-click test.
- Warm switch calls: 82.249, 81.104, 71.650, 71.829 ms on this session/machine. These synchronous calls are measurable hitches and are not a streaming/performance guarantee for crowds or cold loading.
- Runtime02 editor exit and SaveGuard restoration PASS (`Runtime02.log` and matching RuntimeSaveGuard). Latest editor build 20 PASS, 11.71 s; editor build 21 adds only conversion error reporting.
- Visual03 review additionally covered all six Male2 garment types and extreme-morph movement for all three bodies. A capture suite is sampled visual evidence, not proof for every possible morph combination.

## Fresh conversion and NPC checkpoint, 17:23 Bogota

- FreshConversion04 PASS: 72.094 s for legacy FBX import + conversion + checks + scoped save. Imported 416 morphs/196 bones; all morph delta bytes, LOD count and materials preserved; regenerated local physics. The resulting 196 mesh names/parents/bind transforms exactly equal production Male2, and a second conversion is refused. Earlier Interchange attempts produced no physics asset despite FbxImportUI settings; explicit FbxFactory matches the installed DAZ import route and resolves that fixture issue.
- Converted QA asset cold reload PASS in OnboardWorkflow01. Reusable registration then hit an editor-only Director property assignment; fixed to use `set_editor_property`. No publish gate inferred from that failed run. OnboardWorkflow02 reruns the corrected preparation and idempotence checks.
- NPCSave03 and NPCSave04: all 11 assertions PASS, including four automatically generated GUID identities among the 12 NPCs, both male body options, female-only selection, morph diversity, player exclusion and exact ALS restoration. NPCSave03 save 0.219 s/load 5.469 s, total 32.203 s; spawn call 184.087 ms excludes deferred appearance work.
- Both latest NPC sessions have a separate **FAIL process-exit gate**: access violation after LogExit. Three-second EndPIE delay and Python wrapper cleanup did not remove it; do not claim a root cause. Windows Application Error identifies ntdll.dll offset 0x5fbb0, without a useful project stack. Numeric save/load results and SaveGuard restoration remain distinct from this failure. The ordinary runtime and visual sessions had clean exits. Further packaged behavior/exit validation remains required.
- Runtime/ACF/clothing adapter commit: `0112af07`. No shared skeleton or protected body modification. All future non-G9 support and high-population performance remain outside the verified G9 fixture; warm body switching is synchronous and measured above.


## Measured checkpoint, 17:43 Bogota

- Reusable OnboardWorkflow02 PASS, 28.156 s: cold reload; two idempotent preparations
  of Male.02; stable catalog order/IDs, reused physics, all 18 clothing fits valid.
  The three isolated raw-import QA assets were backed up and removed through the
  editor after referencer inspection (`onboard-workflow.json`).
- Final native catalog automation PASS (`NativeFinal.log`); game build02 PASS,
  76.52 s. G9 tools commit `ffd73baa`; Male2/catalog/physics/clothing assets commit
  `0697e0b7`. Existing user HUB and INI changes remain outside these commits.
- Cook/package01 FAIL: GameFeatureData type registration was missing in the
  pre-task INI state. Restored that one required type, with no scan directories.
  Cook/package02 PASS: 5,294 packages, UAT 265.13 s, exit 0.
- Packaged01 body runtime assertions PASS, 22.733 s; total process 40.530 s,
  exit 0. Both skeletal components animate across all four changes, Male2 moves
  313.319 cm, secondary physics and creator Previous/Next checks pass. Reviewed
  the actual packaged creator screenshot showing Preset 2. This run overlapped
  debugger startup and is not an isolated performance benchmark.
- **Package completeness still FAIL for clothing**: packaged log cannot resolve
  the configured V4 fit registry. Added the V4/V5 registry, Director and deformer
  cook directories required by this feature. Package03 reruns this gate. The
  validator now also rejects missing clothing registry/manifest/Director assets.
  Its earlier narrow `error_count=0` was not a zero-error project claim.
- Other packaged startup errors are independently recorded: configured equipment
  pickup class absent and Calysto configuration absent in the pre-task INI state.
  This feature's HUB package is not certification of all game systems/maps.
- NPCSave05 repeats all appearance assertions successfully (28.813 s), then the
  debugger captures the shutdown access violation in Slate text/widget destruction
  during DLL/static teardown (`NPCShutdownCdb.log`). Export-only Engine symbols
  do not identify a precise project owner. Python launcher source shows direct
  quit bypasses its normal notification-completion path; NPCSave06 tests returning
  keep-alive to false so FEditorPythonExecuter schedules its own deferred exit.
  This is a concrete hypothesis under test, not yet a confirmed resolution.


## Certification checkpoint, 17:49 Bogota

- NPCSave06 **PASS including process exit**, all 11 assertions / 12 NPCs, 29.687 s
  assertion interval; original save hashes restored by SaveGuard. Returning the
  startup Python runner's keep-alive to false allows notification completion and
  deferred editor exit. This resolves the observed harness shutdown failure in the
  fresh regression; no Engine, Marketplace or game persistence code was patched.
  The same completion path is now used in RuntimeQA.py; Runtime03 verifies it.
- Package03 **PASS**, fresh HUB cook of 5,337 packages, complete wrapper 292.164 s.
  Packaged02 **PASS**, total process 30.405 s, native assertions 22.734 s, exit 0,
  feature error count 0. The V4 clothing registry resolves after the scoped cook
  additions. Runtime moves Male2 308.286 cm; four switches take 79.250?89.307 ms.
  Reviewed actual packaged Male2 standing pose and creator Preset 2 captures.
- Packaged scope is body presets, animation/physics and creator selection. The
  separate PIE visual suite establishes clothing fit/coverage; packaged asset
  availability does not certify every garment animation, combat move or ragdoll.
- Four unrelated startup error lines remain (three categories): configured
  equipment pickup class unavailable (twice), missing procedural managed-map/class
  configuration, and missing legacy Calysto PCG asset. Existing INI deletions were
  not wholesale restored. See `unrelated-packaged-startup-errors.txt`. A whole-game
  release, all-map cook or zero-error project is **not** certified by these gates.
- Final protected snapshot **PASS**: 7,643 files, 78.49 s, zero changes. Protected
  Player/Female/Male/Multiple/Frederick/shared UEFN skeleton and vendor plugins retain
  their baseline bytes. User source project remains untouched.
- Reusable skill validation **PASS**. Versioned quickstart:
  `Docs/Migration/BodyPresetWorkflow.md`; AGENTS.md routes future work to the master
  skill. One-off implementation mutation scripts were moved into scoped Saved
  evidence; supported commands stay under Tools/BodyPresets.
- Versioned evidence: `Docs/Migration/Evidence/BodyPresets_20261008/` includes native,
  conversion, cold/idempotent preparation, NPC save/process/SaveGuard, clothing,
  Blueprint, protected hash, package and packaged runtime receipts. The evidence
  index hashes larger logs and the immutable baseline kept in Saved.

### Practical limits

Current content is Male.01/Male.02/Female.01. Additional meshes extend data, not a
hard-coded five-entry array. G9 conversion is certified against the actual imported
Male2 fixture; another DAZ generation requires a reviewed recipe. Morph names and
ranges are per body. Normal/tangent warnings already exist in the raw FBX and have
not been repaired by changing topology. Body switches are synchronous, around
70?90 ms warm here; no high-population or cold-streaming guarantee. Hair-specific
fitting, network replication and exhaustive combat/ragdoll combinations remain
outside this feature's evidence. Do not infer those passes from the standing pose.


## Closeout, 17:50 Bogota

Runtime03 PASS with clean editor exit, 22.849 s of assertions, 314.862 cm movement;
normal Python-runner completion verified independently of the NPC script. Editor
reopening through the DAZ-safe launcher. Implementation and certification work took
about 2 h 30 min from the 15:20 start, below the initial 4?8 h planning range.
No remaining body-feature gate is blocked; the unrelated startup errors and explicit
coverage limits above remain recorded rather than relabeled as passes.
