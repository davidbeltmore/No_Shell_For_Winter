# Calysto Director V7 acceptance ledger

Initial resumed scope: commit `5ec929d76546e95249cab4735f96176e3ca7911f`, preserved dirty snapshot
[`Resume_20260908/Snapshot.json`](../../../Saved/Migration/CalystoDungeonDirectorV7/Resume_20260908/Snapshot.json).
Latest source changes were uncompiled at the initial snapshot. The 12:12 UTC
checkpoint now includes accepted native fixture trilogies for both failing seeds.

**Verified: 0 / 31 original criteria (0%). Current: 31 PENDING, 0 PASS, 0 FAIL.**
This is evidence coverage of whole original criteria, not an implementation estimate.
The [JSON ledger](Calysto_Director_V7_Acceptance.json) freezes the exact original
Section 11.1-11.4 bullets, their source hashes, scope, next actions and receipts.
Section 11.5 governs every receipt and adds no counted bullet. Partial tests earn
no fractional credit; a changed current source keeps historical results separate.

Historical production `Director.Probability.100000Trials` passed its in-memory
subcheck in **0.389746 seconds** with 100,000 trials and zero test warnings/errors.
The broader probability criterion remains PENDING. Its surrounding native run
had **69 clean tests and 1 warning**, so
[`StrictSummary.json`](../../../Saved/Migration/CalystoDungeonDirectorV7/Native_DoorMaterializer_20260905/StrictSummary.json)
is **FAIL** despite launcher exit 0. Exact tested-source/actual-child-exit linkage
is incomplete in this initial ledger. Earlier traversal failures remain evidence;
they do not establish acceptance of the resumed source.

The September 8 operator checkpoint adds verified discovery and individual use
of `UEFCalystoDirectorToolset` context, traversal arming, actual PIE diagnostics
and deferred shutdown. See `EV-MCP-OPERATOR-20260908` in the JSON ledger for exact
artifact hashes, engine context and completed owned Editor exit 0. Diagnostics
retain the original `diagnostics_json` string to preserve int64 run seeds.

Both `Traversal_20260908_NativeMcpCold` and `Traversal_20260908_NativeMcpWarm`
remain **FAIL**: cold shared loading expired before native generation; warm
loading completed but native wall-light output failed and exhausted the request
deadline. Later cold/warm `LightObservationSeed177` diagnostics confirm V7 run
seed `1219803037753221267` produces first topology `1779679224`; those traversals
also FAIL. The mapping `-3003287693235273058` to `1190737158` was subsequently
confirmed live. Both native fixture runs now completed floors 1–3 through actual
doors. The topology119 run also walked all three complete protected routes
within 77.594 seconds. See `EV-NATIVE-TRILOGIES-20260908` in the ledger and the
consolidation report. Black screenshots leave visual QA PENDING; native fixtures
exclude full gameplay. The final V7-only production/package scope remains unverified.

| Original ID | Criterion | Current status |
| --- | --- | --- |
| V7-11.1-01 | Clean discoverable authoring | PENDING |
| V7-11.1-02 | Every control and native editor behavior | PENDING |
| V7-11.1-03 | 100K probability laws and exact endpoints | PENDING |
| V7-11.1-04 | Identity and independent random domains | PENDING |
| V7-11.1-05 | Empty and infeasible boundary cases | PENDING |
| V7-11.1-06 | Amounts, inheritance, limits and disabled adaptation | PENDING |
| V7-11.1-07 | Separate requested, feasible and accepted statistics | PENDING |
| V7-11.2-01 | Native parity and one root generation | PENDING |
| V7-11.2-02 | Protected rooms and guaranteed Theme | PENDING |
| V7-11.2-03 | Structural collision, entry and navigation | PENDING |
| V7-11.2-04 | Exact realization and whole-attempt rollback | PENDING |
| V7-11.2-05 | Pending, stable reseeding and atomic outcomes | PENDING |
| V7-11.2-06 | Custom PCG preflight | PENDING |
| V7-11.2-07 | Bounded recovery, cancellation and cleanup | PENDING |
| V7-11.3-01 | Real three-floor door traversal and final HUB return | PENDING |
| V7-11.3-02 | Failing seeds, Styles and finite extremes matrix | PENDING |
| V7-11.3-03 | Grey, orange and blue surfaces and boundaries | PENDING |
| V7-11.3-04 | Placement, torches, decals and native architecture | PENDING |
| V7-11.3-05 | Catalogs, companions, inventory and containers | PENDING |
| V7-11.3-06 | Replay, reroll, restart and recovery cases | PENDING |
| V7-11.3-07 | 25-floor ownership retention soak | PENDING |
| V7-11.4-01 | Cold Editor, Development and Shipping builds | PENDING |
| V7-11.4-02 | Affected Blueprint compile without protected saves | PENDING |
| V7-11.4-03 | Fresh cook and both package configurations | PENDING |
| V7-11.4-04 | Packaged traversal before and after retirement | PENDING |
| V7-11.4-05 | V7-only final authority and dependencies | PENDING |
| V7-11.4-06 | Blood texture closure and native torches | PENDING |
| V7-11.4-07 | Hardware, latency, PSO, navigation, hitches and memory | PENDING |
| V7-11.4-08 | Clean actual process exits and logs | PENDING |
| V7-11.4-09 | Per-phase protected comparisons | PENDING |
| V7-11.4-10 | Distribution entitlement | PENDING |

**Checkpoint, 2026-09-08 20:47 UTC:** the exact master migrated/reloaded cleanly;
both failing topology seeds and three consecutive native-parity routes have
evidence. Same-world gameplay snapshot, accepted materializer activation, baked
probability (100,000 trials per law in 4.397 seconds), actual surface physics and
real phased loading pass their scoped tests. These partial proofs do not complete
the final candidate/tree criteria. The frozen fraction remains 0/31.

**Next action:** finish positive Player-Recast candidate coverage, connect native
architecture/content reservations and materialization, and integrate the real
gameplay bridge and cross-world handoff. The parent owns Editor/build/assets;
one auxiliary owns the bounded surface fixture. Full master gameplay, remaining
functions, legacy retirement and both package checkpoints remain pending.
Continue automatically using [continuous execution](../../../.agents/skills/calysto-dungeon-master/references/continuous-execution.md).
Keep entitlement PENDING until explicitly confirmed, independently of packages.
