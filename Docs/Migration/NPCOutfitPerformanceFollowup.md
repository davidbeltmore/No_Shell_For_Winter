# NPC outfit performance follow-up

## Delivery decision, 2026-10-12

The user explicitly accepts the current performance for this delivery and asks
to defer further optimization. This changes release acceptance, not the measured
results: the original 25-NPC budget still FAILS. No further performance work is
required for this delivery. Functional, visual, build and packaged gates remain
required; see [NPCOutfits.md](NPCOutfits.md) and its final evidence.

The user reports that earlier LOD changes damaged animation and gameplay.
FullRag therefore retains its original single LOD, independent NPC LOD stays
disabled, and animation update frequency is unchanged. Reopening LOD experiments
requires a new explicit user request. Historical reduced meshes were withdrawn.

## Latest prepared measurements

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8, HUB, schema 36.2.
Parent snapshot: `7d3823d0`; source/graph hashes and exact controls are retained
in `Evidence/NPCOutfits/final-delivery.json`. Each matched pair uses a 30-second
steady capture after preparing that group in the same PIE world. Admission is
one owner/frame, maximum four active initializations. These are local development
editor measurements, not universal hardware capacity estimates.

| NPCs | Added CPU ms | Added GPU ms | Added frame p95 ms | Dressed frame p95 ms | Prepared ready s | Spawn peak ms | Original budget |
|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 0.096 | 0.252 | 0.271 | 24.221 | 0.286 | 70.179 | PASS |
| 10 | 2.774 | 2.119 | 3.542 | 31.936 | 1.506 | 523.912 | FAIL CPU |
| 25 | 8.591 | 6.186 | 11.363 | 46.678 | 4.131 | 1383.410 | FAIL all four |

Original incremental limits: CPU 2 ms, GPU 3 ms, frame p95 4 ms, prepared group
readiness 2 s. The provisional global reference is 60 FPS; bare HUB p95 was
23.950 / 28.394 / 35.314 ms respectively. Outfit deltas alone cannot certify
the global frame target. Raw pairs and comparison receipts are under
`Saved/NPCOutfits/Native/perf_witness_fused_01_*` and
`Saved/NPCOutfits/Comparisons/perf_witness_fused_01_{1,10,25}.json`.
The earlier 50-NPC run is diagnostic and predates this graph revision; no new
50-NPC capacity claim is made. Initial loading is reported separately in the
final packaged cold-item case; OS disk cache state is uncontrolled.

## Last retained shader change

Schema 36.2 fuses incidence validation with the first ordered witness projection
cycle. Each record is still checked once per receiving vertex each frame.
Invalid data still discards the partial projection through the original fallback.
The projection order, six-cycle ceiling, exact fixed-point exit, displacement
cap, per-frame animation inputs and all bounds remain unchanged. There is no
new resource or mesh/animation change. Prepared planes still reserve 4,870,144
logical bytes per FullRag outfit; this is not measured total GPU memory.

Matched short GPU captures report witness kernel 5.89 -> 5.04 ms across 111
scene garments, including background HUB NPCs (about 14.4% in that one sampled
frame). Other kernels also vary. This is diagnostic evidence of reduced local
kernel work, not proof of CPU improvement or of meeting the crowd budget.
The latest longer CPU delta is higher than an earlier run; its cause has not
been established. Do not attribute that difference to this shader from these
measurements alone. Raw named GPU events and summaries are retained under
`Saved/NPCOutfits/GPU/witness_fused_{before,after}_01.*`.

## Recommended next investigation, when requested

1. Re-establish the baseline on the intended deployment hardware and current
   gameplay map, with the PC otherwise idle. Record renderer, resolution, quality
   CVars, body/outfit distribution, fixture version, source/graph hashes and
   admission settings. Use matched prepared 1/10/25 pairs; keep 50 diagnostic.
   Measure initial nonresident loading separately from preparation and steady
   animation. Agree any new budget before measuring.
2. Attribute the CPU delta before making another optimization. Existing entry
   points are `Run.py RunNativePIE --trace --prewarm --count 25 --capture 12
   --name UNIQUE` and `ExportCPUTrace.py UNIQUE`. Inspect the measured window,
   named fitting/parameter setter scopes, enqueue/render work and unrelated HUB
   cost. Do not sum nested inclusive timings. Short traces do not replace the
   30-second comparison. Use `--profile-gpu` plus `SummarizeGPU.py` for a separate
   bounded GPU diagnostic; all-scene totals include background garments.
3. Inspect allocation, dispatch and parameter submission overhead before changing
   fit quality. Immutable binding reuse and exact GPU parameter-byte caching
   already exist; first use and every changed live value must still use native
   setters. Avoid another cache without evidence of repeated work and a bounded
   lifetime. Preserve detach, ownership, invalidation and GC semantics.
4. For readiness, inspect `fixture_initializations` to separate spawn/body work,
   admission wait, async prerequisite time and render readiness. Raising the
   active-owner cap above four showed diminishing returns and did not satisfy
   the two-second target. Frame-time-budgeted admission or batching is only a
   future hypothesis; preserve appearance-before-equipment, native ACF/ALS
   readiness, exact GUID restoration, cancellation and the 20-second deadline.
5. If GPU fitting remains dominant, inspect witness distribution, prepared-plane
   bandwidth and dispatch structure while preserving constraints. The earlier
   LOD reductions concentrated incidence, sometimes exceeded the unchanged 256
   bound, and could increase fitting work despite fewer triangles. Do not raise
   bounds, truncate constraints, loosen spacing, reduce animation frequency or
   hide entire characters merely to make a benchmark pass.

Only repeat a capture after a meaningful change, a new attribution hypothesis or
a failed fixture correction. Keep prior FAILs. Use existing bounded hosts rather
than open-ended PIE: editor/game builds 600 s, cook 900 s, packaged case 180 s,
and native fixture limits recorded in each receipt. Never overlap captures or
run build, hashing or package work during performance measurement.

Any shared fitting change must again pass all three representative bodies,
extreme morphs, body swaps, movement, overlapping coverage ownership, actual
image review, Blueprint compilation, fresh cook, packaged equipment mutation,
GC and normal exit. `Ready` alone cannot certify visual quality. Preserve the
historical Female extreme-morph anomaly; later controlled checks did not reproduce
it, but its original cause is still unproven. Back up real saves before PIE,
restore after closing that editor, and re-hash protected assets after the phase.
