---
name: calysto-dungeon-master
description: Implement, author, operate, debug, and validate Calysto Dungeon Director through project-owned EFProcedural and live UE 5.8 tools. Use for V7 migration, Styles, room Themes, probability, placement, native PCG, materials, navigation, travel, content, and bounded gameplay tests.
---

# Calysto Dungeon Master

Enhance native Calysto through `Plugins/EFProcedural`. This skill supports V7
implementation and operation; its existence does not prove V7 is implemented.
Establish actual authority and gate status before each task. All editor text,
APIs, tooltips, logs, and documentation are English.

## Evidence preflight

1. Invoke `$noshellforwinter-unreal-mcp`. Confirm writable target
   `D:/Projects UE5/NoShellForWinter` and UE 5.8. Discover/describe MCP and inspect
   loaded map, PIE state, dirty packages and exact asset/class read-only.
   Re-discover after restart.
2. Read [calysto-contracts.md](references/calysto-contracts.md) and relevant
   [operator-control.md](references/operator-control.md) sections. For migration
   or implementation read the full
   [master plan](../../../Docs/Migration/Calysto_Dungeon_Director_V7.md).
3. Record Git HEAD/status, dirty packages, exact configuration/asset hashes and a
   snapshot of files about to change. Preserve unrelated dirty work. Use unique
   `Saved/Migration/CalystoDungeonDirectorV7/` evidence directories and durable
   receipts in `Docs/Migration/Evidence/`, outside runtime/cook.
4. Before/after runtime or asset changes run
   `scripts/Test-CalystoProtectedAssets.ps1` and
   `Tools/Migration/Test-ProtectedInvariants.ps1`. Compare historical baseline
   and task pre-change snapshot separately. Never silently rebaseline existing
   drift. The Calysto helper alone does not cover ACFU, Daz or characters; a hash
   capture without comparison is not proof of parity.
5. Choose the smallest applicable [validation gate](references/validation-gates.md).
   Missing V7 tooling is work to implement, not permission to call a V6 test V7.

V7 target: `UEFCalystoDungeonDirectorAsset` at
`/Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector`, internal schema 7.
Use unversioned public APIs. Inspect Config and loaded class to distinguish
active V6, V7 candidate and accepted V7-only authority. Never compile V7 through
V6 structures or fall back to V6 assets. Preserve V6 as an immutable migration
source until proven cutover.

## Workflows

- **Implement/migrate:** follow the nine master-plan milestones. Freeze/reproduce
  first; prove native traversal before population; migrate field by field;
  accept the candidate before retirement; rebuild/repackage the final tree.
  Resume the first incomplete gate.
- **Author:** inspect actual properties, use native transactions and exact asset
  edits, validate exact field errors, save only intended project packages.
  Behavior-test each changed functional control. Diagnostics belong in the
  on-demand inspector; keep native Details clean.
- **Operate/test:** use discovered APIs or the grounded cookbook. Check ownership,
  issue one action, observe its postcondition within the deadline, capture proof.
  Do not repeat an interaction while its request is Pending.
- **Debug entry/nav:** inspect settled structural instances, collision, bounds,
  Start/End room ownership, floor contact/capsule clearance, registered nav
  bounds, relevant tiles and complete route. Idle build queue is not ready.
- **Release:** short gates first, then bounded release matrix, cook and fresh
  Development/Shipping packages, exact referencer retirement and final-tree
  validation. Keep entitlement and protected discrepancies visible.

The implementing agent performs technical tests, real PIE, screenshots and
visual inspection, cook and packaged tests. Existing task authorization covers
requested implementation and routine reversible QA; do not repeatedly ask for
confirmation or delegate QA back to the user. Preserve unsaved work before an
authorized restart. External facts remain gates; installed files do not prove
distribution entitlement.

## Operational invariants

- One Style per floor. Exclude Start/End/Critical/Progression from Themes;
  guarantee one eligible room, then theme each remaining room independently at
  25%. Theme weights cannot change presence for unchanged topology.
- Chance, conditional Amount, Weight and Limits are distinct. Filter eligibility
  and reserve feasible placement/budgets before random commitment. No hidden
  rarity Nothing; normalize populated eligible tiers including Winter.
  Every selected reservation materializes or the whole attempt is rejected.
- Persist stable identities; labels, array order, global RNG and request-routing
  IDs never define gameplay identity. Use independent deterministic domains.
- Native Calysto owns topology/construction. One adapter holds vendor knowledge,
  retains transient clones and issues one root GenerateLocal per attempt.
  Validated custom PCG only proposes bounded content.
- One transaction owns Preflight -> Native Generation -> Structural Verification
  -> Navigation/Reservations -> Realization Verification -> Commit -> Player
  Release. EFLevelFlow consumes the Director's sole verified entry transform.
- One 30-second request deadline covers at most four attempts and cleanup.
  Pending consumes time, not attempts. Reseed recoverable spatial failure;
  preserve Style/run/floor/rules/budgets/outcomes. Exhaustion protects the player
  and offers Retry and Return to HUB, without a silent HUB bounce.
- Per-surface material precedence is Theme override > Style; empty explicit
  override fails. Verify actual instances and shared boundaries, with no
  per-room MID. Initial appearance: grey general, orange Forge, blue Shrine.
- Preserve all catalog/gameplay bridges, inputs, native torch effects and exact
  decal pool/dependency rules. Adaptation off has mathematically zero influence.

## Bounded testing

Use [validation-gates.md](references/validation-gates.md) for timing and proof.
Poll external progress at 1-2 seconds, yield control within 10-30 seconds and
communicate within 60 seconds. No unbounded wait or unattended retry loop.

The rapid traversal gate is three consecutive real-door floors in one run;
three Floor-1 samples do not count. Probability gates use at least 100,000
**in-memory decisions**, not world generations. The 25-consecutive-floor
retention soak runs for release after the short smoke. Never schedule 1,000
world floors for this plan.

`scripts/calysto_test_plan.py plan --mode quick|release` emits a schedule only.
PLANNED is not evidence. Inspect runner bindings and implement missing V7
bindings before execution. Run the helper's behavioral tests after edits.

Require exact current test inventory/assertions, clean logs, process exit 0 and
protected invariants together. PASS JSON followed by a crash fails. Unrun claims
are PENDING; observed failures are FAIL. Authoring Valid, native tests, gameplay
verification and release acceptance are separate states.

## Protected boundary

Never write the read-only LustAsDeadlySin source, Marketplace/Engine plugins,
`/Game/Calysto`, BP_MassiveDungeon or protected ACFU/Daz/Player/character assets.
Use protected build/launch wrappers preserving both Daz plugins in receipts and
launch. No bulk copy/cleanup, raw asset deletion or broad incompatible redirects.
Retire exact audited legacy assets through Unreal Editor after their gates pass.
Keep useful unversioned internal materials, runtime Blueprints and cooked PCG
compatibility assets.
