# EF Clothing Morph latency investigation — 2026-09-20

Target: `D:/Projects UE5/NoShellForWinter` (UE 5.8).
Source `D:/Projects UE5/LustAsDeadlySin` remains read-only.
The initial phase measured hypotheses. The user subsequently authorized optimizing
the equipment system; the project-owned native bridge below is now implemented.

## Reproduction and definitions

`Tools/ClothingMorphV5/Run-ClothingLatency58.ps1` launches a fresh editor through
the protected Daz launcher and runs actual HUB PIE and ACF equipment. It requires
the existing target editor to be closed. The process exits at the end, with
bounded internal and external timeouts. It saves reports under
`Saved/ClothingLatencyQA/<RunName>` and saves no test assets or character state.

- `baseline`: existing production settings, 2-second reconciliation watchdog.
- `notify`: one transient `NotifyEquipmentChanged()` after the test observes the
  actual visible source mesh. This simulates a completion notification. It is
  not a completed C++ integration and does not call `ForceReconcile()`.
- `watchdog`: only the PIE component's watchdog is temporarily changed to 0.25 s.
- `armor_event`: forwards the real ACF armor event once, without waiting for its
  mesh assignment. This deliberately tests the insufficient early-event approach.
- `-Transitions`: three-item outfits and in-place Female/Male body changes;
  checks anatomy coverage and captures post-ready images outside the timed interval.

Single-item runs contain six garments x two bodies x three repetitions (36).
Their deterministic shuffled order and 0.05–1.95 s pre-equip jitter vary the
watchdog phase. Cleanup waits for zero managed garments, without forcing it.
Item class loading is measured separately, before the equip interval. Inventory
UI latency is excluded. The equipment call itself is included.

The per-Slate-tick observer records wall and game time, first visible source
component, first managed state, warmup, and first `Ready` state. It also records
callback gaps and observer execution cost. `Ready` means the native producer
has a render-thread validated submission after its latest fallback. The native
contract explicitly does NOT guarantee a GPU completion fence or pixel-present
timestamp. Do not label these measurements first-displayed-pixel latency.

Each experiment uses a fresh editor process with existing OS/DDC caches. Editor
PIE preflight validates/publishes bindings, so first equip is NOT evidence of a
cold packaged streaming load. No cache purge or destructive cold-load simulation
is used. Hardware observed: NVIDIA GeForce RTX 5070 Ti.

## Causal code path

- `EFClothingMorphSettings::EquipmentReconcileFallbackIntervalSeconds = 2.0`.
  This value was also inspected in the loaded editor via MCP ObjectTools in
  the preceding feasibility review. `MorphSyncIntervalSeconds = 0`.
- `UEFClothingMorphV3RuntimeComponent::TickComponent` calls `ReconcileGarments`
  on notification or the watchdog deadline. Surface passes otherwise tick
  every frame. The measured baseline spends most time with a visible mesh
  and `managed=0`, not in surface warmup.
- `UEFClothingMorphWorldSubsystem` listens to the project appearance mesh hook.
  That hook is emitted by customization initialization and clothing visibility
  changes. The active V5 runtime does not directly subscribe to the ACF armor
  event. The legacy `UEFClothingMorphComponent` has a separate ACF subscription;
  that is not equivalent to notifying V5.
- Read-only ACF inspection: `UACFEquipmentComponent::AddSkeletalMeshComponent`
  calls `InitArmor` then broadcasts `OnEquippedArmorChanged`.
  `UACFArmorSlotComponent::InitArmor` starts `RequestAsyncLoad`, and its private
  `OnAssetLoaded` assigns the mesh later. A production bridge must account for
  this ordering rather than assuming the initial armor event means mesh-ready.
- `TransientRetrySeconds = 0.25` can add delay when a deformer/source writer is
  not yet ready. It is a secondary candidate, not the main measured baseline wait.
- The producer's `MinimumInitialWarmupSubmissions = 30` is part of a recovery
  timeout test, NOT a mandatory 30-frame success delay. Do not shorten it as
  a supposed fixed startup wait. Success uses render confirmation over frames.

## Results

PENDING: remaining correctness/regression gates and final `summary.json`.
Raw evidence: `Saved/ClothingLatencyQA/baseline_01`, `notify_01`, `watchdog_01`.
The baseline 36-sample median is 1.034 s; p95 1.960 s; maximum 2.009 s.
The mesh is observed after a median 0.017 s. Managed-to-ready median is 0.034 s.
The 36-sample completion-notification simulation median is 0.092 s; p95 0.140 s;
maximum 0.170 s; all 36 samples meet 0.5 s.
The shorter-watchdog simulation median is 0.222 s; maximum 0.351 s (36 samples).
The actual native implementation (`native_01`, normal baseline mode, no Python
notification and unchanged 2-second watchdog) has median 0.093 s, p95 0.170 s,
maximum 0.189 s; all 36 samples meet 0.5 s.
At a 30 FPS cap (`native_30`), all 12 samples also meet 0.5 s: median 0.147 s,
maximum 0.211 s. Forwarding only the early ACF event at that frame cap
(`armor_event_30_02`, before the native change) gave median 2.109 s and maximum
2.177 s. The completion observation is necessary; an event subscription alone
does not provide the measured improvement.

Native equipment correctness (`equipment_native_02`): 13 checks PASS across
Female/Male, chest/lower replacement bursts, same-frame cancellation, bulk
unequip, synthetic late visibility resurrection and a 7-second idle interval.
The bridge reports eight stale visibility corrections and no new fitter
notifications during that idle interval. The fixture uses actual inventory
entries with `EquipItemFromInventory`; repeatedly adding an existing item with
auto-equip is not equivalent to explicitly selecting that inventory entry.

The original three-item transition suite (`transitions_base_03`) passes 18
measurements and coverage checks: first outfits can still take 2.214 s, whereas
already-equipped body swaps take about 0.10–0.19 s. This isolates equip detection
from body change handling. Earlier transition attempts exposed two fixture
constraints: parent `ItemSlot.Armor` must precede child slots, and bulk ACF
removals can queue re-loads of the remaining garments. The successful baseline
uses paced child-first removal. Native equipment QA explicitly exercises bulk
removal instead, so it cannot hide that behavior behind fixture pacing.

## Native equipment bridge

`UEFClothingEquipmentBridgeComponent` is automatically composed with the existing
runtime by `UEFClothingMorphWorldSubsystem`. It uses validated reflection contracts
for the optional ACF integration, with no new Marketplace/Engine dependency or
modification. It:

1. Subscribes to `OnEquippedArmorChanged`, then observes actual slot mesh and
   visibility changes for a bounded 5-second readiness window.
2. Notifies the fitter only when that component state changes. It does not
   reconcile the whole catalog every frame. Late render-state dirties wake the
   observer; a 2-second safety scan remains for missed notifications.
3. Reads the final equipment state after the current mutation, matching exact
   slot names and the current armor's mesh request. Obsolete **catalog clothing**
   is suppressed if old asynchronous callbacks make it visible again. Unknown
   items, empty-slot body meshes and non-catalog components are not inferred away.
4. Restores bridge-owned temporary visibility when the requested garment arrives,
   respecting the customization clothing-visibility switch.
5. Unbinds delegates at EndPlay and exposes bounded diagnostic counters for QA.

The existing geometric algorithm, 0.25-second readiness retry, binding integrity
checks, coverage ownership, and source meshes remain unchanged. The measured
improvement did not require reducing quality or forcing synchronous loads.

## Validation gates for the implementation

| Gate | Status | Evidence under `Saved/ClothingLatencyQA` unless specified |
|---|---|---|
| Editor C++ / UHT | PASS | `build_editor.log` (Daz receipt repaired) |
| Native latency, normal frame rate | PASS, 36/36 <= 0.5 s | `native_01/result.json` |
| Native latency, 30 FPS | PASS, 12/12 <= 0.5 s | `native_30/result.json` |
| Equipment replacement/cancellation | PASS, 13 checks | `equipment_native_02/result.json` |
| Three-item outfits and body swaps | PASS, 18/18 <= 0.5 s; maximum 0.299306 s | `transitions_native/result.json` |
| Full unisex/morph regression and Blueprint compile | PASS, 28 checks; Blueprint compile PASS | `UnisexRegression/PIE/result.json`, `unisex_runner.log` |
| Selected visual regression | PASS within captured poses | `UnisexRegression/PIE`: Male RagShirt/RagPants and Female/Male extreme morph screenshots; transition outfit screenshots |
| Native C++ contracts | PASS, 2 tests, zero warnings/failures | `native_runner.log`; `Saved/Migration/EFClothingMorphV51/NativeAutomation_20260920_222619/Report/index.json` and `NativeAutomation_20260920_222727/Report/index.json` |
| Current Game build | FAIL, pre-existing Calysto blockers; new clothing bridge compiles | `build_game.log`, exit 6 |
| Cook/package | PENDING | Game build prerequisite fails |
| Protected hashes | PASS, 5,260 baseline files unchanged | `protected_check.json`; source snapshot `task_snapshot.json` |
| Editor restored | PASS | `reopen.log`, target process 19164 with both Daz plugins enabled; fresh live MCP `list_toolsets` responded after launch |

The three native timing runs contain 66 successful measurements, all below
0.5 seconds. The full regression preserves its six synthetic coverage-overlap
checks by temporarily suspending only the equipment adapter for that fixture:
it deliberately assigns a garment inconsistent with ACF inventory. All actual
equipment, body-change, morph, cancellation and latency tests retain the adapter.
Visual review confirms fitted silhouettes and covered lower anatomy in the
selected screenshots, including extreme morphs in movement; it does not establish
every animation pose or first-displayed-pixel timing.

## Follow-up criteria

Validate the native bridge with no synthetic notifications. Keep the 2-second
watchdog as recovery. Avoid a full catalog reconciliation every frame, global
Marketplace changes, or skipping binding/render validation.

Only optimize readiness retries, immutable validation caching, or prefetch after
measuring a remaining contribution. A globally shorter watchdog increases full
reconciliation frequency even when no equipment changes; compare it as a
diagnostic alternative, not automatically as the preferred implementation.

Production acceptance for the next phase should include equip/re-equip, slot
replacement during asynchronous loading, unequip/cancellation, multiple items,
body swaps, morph changes, coverage ownership, lower frame rates, and cold versus
warm packaged loads. A synthetic notification PASS is not implementation PASS.
Cook/package timing remains PENDING. The earlier unisex report records the
pre-existing Calysto Game-build blocker. The new Development attempt reproduces
`EFCalystoContentCollisionContract.cpp:62` (`GetBoolMetaData`), line 99
(`GetMetaData`), and `ProjectCalystoDormantController.cpp:32` (`ClassGeneratedBy`)
being unavailable in the non-editor target. Neither Calysto file was changed by
this task. The new equipment bridge's Game compilation step succeeds. These
errors prevent full release/package acceptance; cold packaged latency is PENDING.
