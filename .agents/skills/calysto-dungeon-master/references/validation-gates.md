# Bounded validation and timing

Read the full [V7 master plan](../../../../Docs/Migration/Calysto_Dungeon_Director_V7.md)
for acceptance requirements. This reference controls how to run them efficiently.
A plan, simulation model or fixture receipt is not a completed gameplay gate.

## Timing policy

The following are initial target-machine budgets, not claims about measured
performance. Start each step with a monotonic deadline. Watch real progress
(events, logs, process and phase state), not sleep alone. External observation
interval is 1-2 seconds; native callbacks drive runtime readiness. Yield tool
control every 10-30 seconds and report meaningful progress at least every
60 seconds. No single assistant blocking wait exceeds 60 seconds.

| Step | Expected useful duration | Hard budget / stop rule |
|---|---:|---|
| MCP metadata/context | 1-5 s | 10 s per call; two connection attempts total, then verified filesystem fallback and live PENDING |
| Protected hashes / dirty snapshot | 10-90 s | 180 s; record coverage and incomplete sets, never call a partial hash set PASS |
| Editor startup | 20-90 s warm | 180 s; record startup separately from Door-to-Ready |
| Schema/preflight/contract check | under 5 s resident | 10 s diagnostic ceiling, always inside shared floor request deadline when serving a request |
| Async resource preparation | 1-8 s warm | remaining shared request time; unavailable resources fail without spatial reseed |
| Native generation/structural settling | 1-10 s | remaining request time; use completion + real instance/collision checks |
| Navigation registration/path observation | 0.5-8 s | remaining request time; delayed registration stays Pending, no repeated full builds |
| Reservation and selected realization | 0.5-5 s | remaining request time; any selected failure rejects whole attempt |
| Rejected-attempt cleanup | typically under 2 s | remaining request time; never overlap a new attempt with unfinished cleanup |
| One Door-to-FloorReady request | target below 30 s | **30 s total**, maximum four attempts including initial attempt, rejection and cleanup |
| One screenshot + structural/material inspection | 2-10 s | 15 s each; batch useful viewpoints, not per-frame captures |
| Real HUB -> Floor 1 -> Floor 2 -> Floor 3 smoke | 45-120 s | 180 s gameplay body, startup separate; stop at first invalid floor |
| Probability computation, 100K per law | target <=15 s | 180 s process total including UE startup; no world generation for sampling |
| Focused native/Blueprint compile gate | 30-180 s | 300 s; exact inventory, logs and process exit required |
| Replay/reroll/cancel/fault fixture | 5-30 s | one 30 s request per case; <=12 focused floor requests initially |
| 25-consecutive-floor retention soak | about 3-10 min | 900 s body, each request <=30 s; only after three-floor smoke succeeds |
| Editor cold build | about 2-10 min | 900 s, 180 s without compiler progress triggers diagnosis |
| Game Development / Shipping build | about 2-10 min each | 900 s each, same progress rule |
| Fresh cook + package | about 5-30 min per configuration | 2400 s each, diagnose after 180 s without real progress |
| Packaged three-floor smoke | 45-120 s each | 180 s body + measured startup; capture all three floors |
| Process shutdown / owned cleanup | normally <10 s | 60 s grace; nonzero/missing exit or forced termination fails gate |
| Exact referencer audit / retirement verification | 10-90 s | 180 s before diagnosis; do not delete when audit incomplete |

Phase expectations do not extend the 30-second request deadline and do not sum
to a separate allowance. A retry does not get 30 new seconds. The harness's
outer process grace does not extend runtime acceptance. Slow cold shaders are a
measured performance issue, not permission to accept a >30-second request.

Timeout => capture bounded diagnostic/log tail and ownership state, cancel the
owned job, finish cleanup, mark FAIL (or PENDING if never executed), and diagnose
the first failing stage. Do not rerun unchanged failures automatically. Retry a
test only after an identified code/config/environment correction, or one explicit
cold/warm comparison. Never let a wrapper's unbounded Wait hide behind its name.

For build wrappers temporarily excluding Daz from UBT, request cancellation at a
safe point and let finally/receipt repair complete. Do not force-kill the wrapper
mid-restoration or launch with disabled Daz plugins. Verify/repair the exact
descriptor and receipt after interrupted work before launching. Never stop
unrelated project processes. Preserve unsaved editor work before restart.

## Gate selection and concise execution

1. **Skill/tool-only edit:** validate the skill and modified helper behavior.
   This says nothing about runtime V7.
2. **Model/probability edit:** cold build as required, focused native tests,
   100K-per-law in-memory suite, exact field/runtime coverage. Run spatial tests
   only when spatial behavior changed or the milestone requires traversal.
3. **Geometry/navigation/travel edit:** capture the two failures, then one real
   three-floor run and relevant delayed/failure fixtures. Stop on first broken
   postcondition; do not continue filling a large matrix with the same error.
4. **Material/placement/content edit:** focused category tests + three floors,
   actual component inspection and a small viewpoint set covering affected
   surfaces/zones. Reuse valid evidence from unchanged build/config snapshots.
5. **Candidate release:** all acceptance categories, 25-floor retention run,
   fresh builds/cook/packages, candidate audit, then retirement and final-tree
   rebuild/package/traversal. Short gates must pass before expensive gates.

Use `scripts/calysto_test_plan.py plan --mode quick|release` for a deterministic
schedule with explicit runner status. It executes nothing and keeps V7 bindings
REQUIRED_NOT_IMPLEMENTED until real runners exist. Never confuse helper unit
tests with Director probability trials. Keep plans and measured receipts separate.

## Probability set

Call the **same C++ decision library used by generation** through native tests or
the on-demand inspector. An independent mathematical oracle checks it; a Python
reimplementation alone is insufficient. Minimum 100,000 trials for each relevant
stochastic law, with zero generated worlds:

- Style normalized eligible weights.
- Additional Theme presence on nonguaranteed eligible rooms.
- Conditional Theme weights, separate from presence.
- Guaranteed-floor count law `1 + Binomial(N-1,0.25)` at multiple N; mean,
  histogram and guaranteed rank symmetry. N=0 rejects and N=1 is exact.
- Content Chance with Amount conditional on success.
- Fixed counts exactly; inclusive discrete uniform amounts; discrete triangular
  count PMF explicitly defined; continuous triangular layout values checked
  against their exact CDF/mean. Name mathematics accurately.
- Populated rarity normalization, including Winter; no implicit Nothing.
- Weighted entry/architecture alternatives including explicitly authored Empty
  where optional native decoration supports it.
- Independent material/topology and Theme-weight/presence domains.
- Decal Style 10%, Forge replacement 25%, Shrine 0% before feasibility conditioning.
- Conditional containers and custom PCG proposal selection using same semantics.
- Adaptive modifiers only when enabled, bounded effects; disabled equality exact.

Use deterministic seeds and six-sigma binomial/multinomial tolerances where
appropriate, with exact assertions for p=0/1 and small boundary fixtures.
For continuous distributions use predetermined CDF checks/tolerances and report
method/sample count. Test exact curve endpoints/interpolation separately.
Do not hide rejection by omitting failed draws. Record requested opportunities,
eligible/feasible counts, chance successes, frozen selections, realized objects,
rejections, accepted requests and final frequencies. Accepted/feasible/requested
denominators remain explicit; recovery conditions observed distributions.

Boundary fixtures: empty catalogs, zero/negative/nonfinite weights, exhausted
cooldowns/budgets, incompatible surfaces/footprints, no eligible rooms, one room,
min=mode=max, amount feasibility gaps, quotas/drought/repetition, every rarity
tier, array reorder, display rename and identity-preserving replay. Extend must
not silently change inherited chance/count/limits; all modes preserve floor caps.

Maintain a field coverage table in evidence: exact authored field path, runtime
consumer symbol, inputs, observable effect, test name and result. Every editable
gameplay setting must be covered; remove or implement dead controls before
exposing them. Authoring Valid never implies Gameplay Verified.

## Three consecutive floors and failing seeds

Preserve topology seeds 1779679224 and 1190737158 exactly. A topology seed is not
an Int64 run seed. Record derived topology seed and deterministic domains; a
harness must support exact topology replay without altering Style/rules.

The quick test follows one GameInstance/run through real DoorToLevel in HUB,
Floor 1, generated floor door, Floor 2, generated floor door, Floor 3. Keep
one run identity and advancing floor indices. Do not restart the run after a
door probe. Direct floor-jump APIs, teleporting between maps, three independent
Floor-1 requests and setting readiness flags do not satisfy this gate.
Door-selection fixtures may approach a door deterministically, but record that
separately from actual capsule locomotion/traversal evidence.

For each floor record: request/attempt identities and seeds, selected Style,
structural counts/bounds/collision, unique room-owned Start/End, one GenerateLocal,
relevant nav bounds/tiles/path, sole entry transform, frozen/reserved/realized
counts, material assignments, player release, timing and screenshot. Verify
player movement on blocking floor and protected route. Capture material evidence
for Floor/Wall/Roof and boundaries; NullRHI is never visual QA.

Natural final HUB return is a separate terminal-floor test using the real final
door and progression rule. Three floors prove neither all Styles nor final
return. Add targeted cold/warm, initial Styles and supported depth/size extremes,
without a full combinatorial sweep. Run each known failing seed once after a
correction and stop on its first failure.

## Transaction, material and gameplay fixtures

Require whole-attempt rollback on selected actor/decal/PCG/resource injection
failure. Count actors/components/instances/nav registrations/reservations/leases/
callbacks/transients before and after. Delayed geometry/nav stays Pending.
Retries preserve Style/run/floor/budgets/pre-floor companions and outcomes;
reproducible seeds exclude request IDs. Cancellation never commits or recovers.
Repeat interaction/stale callback tests must not duplicate attempts or outcomes.
Exhaustion stays protected with Retry/Return to HUB and concise English error.

Validate native parity with extensions disabled, all structural variants/native
zones, transform/scale/rotation/variation, footprints/clearance and route/door
protection. Reject unsupported PCG pins/dependencies/footprint/surfaces at
preflight; no uncontrolled spawning or topology changes.

Inspect actual grey/orange/blue materials on all surfaces and boundaries.
Verify pool capacity24, active8, one/room and Floor3/Wall4/Roof1 decal limits,
10%/25%/blocked selection, distance culling and correctly named fading.
Check intended RealisticBlood closure; preserve native torch effects.

Cover typed enemies/archetype/gender/rarity/levels/threat/depth, support NPCs,
recruitment/companions/lifecycle/inventory/graveyard/Winter's Recall, loose loot,
armor/food/drink/chests/lockpicking/contents, cooldown/repetition/drought/quotas,
outcomes/ecology and advanced modifiers including disabled adaptation equality.
Test exact source input interactions, particularly T/Plus/Minus.

## Release and evidence

After quick gates, run **25 consecutive floors in one process** for retained
object/actor/component/lease growth. Record warm cache plateau separately;
do not reset process between floors or hide counts with cleanup not used by the
game. Stop on first invalid floor or clear monotonic ownership growth. Never
expand this into 1,000 floors. Use per-request samples including rejected
attempts/cleanup for P95 Door-to-FloorReady. Report sample size, percentile method,
hardware, cold/warm state, PSO/nav/hitch/memory metrics. Three samples alone are
not a robust P95 claim; no invented improvement against broken V6.

Cold Editor/Development/Shipping builds, affected Blueprint compile zero errors
with no protected saves, fresh non-iterative cook and fresh archives in both
configurations are mandatory. Run packaged real-door/visual smoke in both.
Capture exit codes for actual child processes, not merely wrapper success.
A missing exit code, crash after report, stale report or incomplete inventory
cannot PASS. Keep logs and artifacts even when wrappers would purge archives.

Candidate acceptance precedes exact legacy asset referencer/deletion actions.
Audit native code, Config, tools, registry, generated manifests and package
closure. No active V3/V4/V5/V6 authority/class/redirect/dependency in final tree.
Retain useful unversioned payloads; archive historical evidence outside runtime.
Repeat builds/packages/traversal/visual gates after retirement. RealisticBlood
entitlement is an explicit distribution gate until confirmed.

Each gate receipt records status (PASS/FAIL/PENDING), exact target/source path or
symbol, snapshot/commit, engine/build/config, commands/arguments, start/end/
elapsed, process IDs and exits, actual test inventory/trial counts, seeds/run/
attempts, hashes including historical discrepancies, log/report/screenshot paths,
assertions and remaining gaps. Plans and authoring validation cannot substitute.
