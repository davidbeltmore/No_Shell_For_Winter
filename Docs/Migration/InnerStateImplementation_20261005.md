# Inner State implementation — 2026-10-05

## Goal and accepted contract

Implement the conversation-approved system, preserving the existing comma HUD style and project/Marketplace invariants. Source `D:/Projects UE5/LustAsDeadlySin` is read-only and is not required by this implementation. Target is `D:/Projects UE5/NoShellForWinter`; initial HEAD `efdebbf8`.

- Primary: Hunger, Thirst, Sleep, Madness, Awareness, Sensation (legacy Pain adapter).
- Sensation: -100 pleasure, 0 neutral, +100 pain. Composure: -100 terrified, 0 neutral, +100 determined. Stimulation: long-term wellbeing, bipolar.
- Secondary: Stress, Stimulation, Hygiene, Bathroom, Air, Composure; seventh compact card groups Sickness/Poison/Curse with simultaneous values. Preserve Doctrine attributes outside this compact HUD.
- Poison has no gameplay maximum. Every new exposure adds finite damage debt based on max health at exposure. Healing does not change debt. Fast/normal/slow latency 3/10/30 simulation seconds; rates 1/.5/.25 dose points/s. Total pending >=100 activates waiting doses immediately. No discarded overflow. Antidote cancels eligible debt, not past damage.
- Awareness is the single physiological unconsciousness gate; no free health on physiological recovery. Active asphyxia + Air zero + Awareness zero is fatal.
- Slow processes use calendar hours (current day 1800 simulation seconds). Acute processes use simulation seconds, paused with gameplay. All finite values validated; no NaN/infinity accepted.
- New definitions/settings, state, adapters and APIs must be maintainable, extensible and project-owned. Integrate ACF status/damage infrastructure without modifying its plugin or applying damage twice.

## Blocks / continuation ledger

1. **PASS (math block only)**: deterministic math, poison ledger, tunable settings, three focused automation tests; protected snapshot unchanged.
2. **ACTIVE**: runtime component, legacy bridges, save/load/travel, awareness/defeat, air/exposure, slow synergies and states.
3. PENDING: native consumables and authored assets; medicine/antidote/washing/bathroom/sleep/social/exercise integration and accessible player actions.
4. PENDING: compact HUD with icons, signed Sensation, conditional afflictions; reusable authoring and QA skills.
5. PENDING: final runtime/visual/cook/package gates and documentation.

## Validation budget

User steering during implementation: mandatory REAL gameplay global automation <=120 seconds (including its in-game waits), entirely outside Calysto/dungeon, with simultaneous-needs stress and baseline-vs-loaded performance measurements. A reusable global QA skill and additional focused control/tuning skills are authorized. Full PC interaction authorized. Do not use Calysto fixtures or dungeon maps. Startup/build/cook are separately reported, never disguised as in-game test duration.

- One deterministic suite covering conservation, latency, mixed poison, antidote, heal independence, max-health snapshots, partition invariance, finite bounds, thresholds and recovery; rerun when affected behavior changes.
- Build after each coherent C++ block; Blueprint compile after asset changes. One focused PIE matrix using representative safe/critical/recovery cases, save/travel roundtrip and HUD screenshot.
- Performance target: player core with 200 active effects p95 <=0.2 ms/update on reference machine, measured separately from startup/asset loads. Report hardware and actual measurements; target is not a PASS claim.
- One final cook/package and representative packaged smoke. Repeat only failed/changed gates.
- No repeated idle waits, broad soak loops or irrelevant test suites.
- Rehash protected inputs after each block. Commit only task-owned paths.

## Initial evidence

- MCP `http://127.0.0.1:8000/mcp`: reachable; list_toolsets obtained. Live runtime gates remain PENDING.
- Existing worktree changes: `Content/_Game/Hub/HUB.umap`; untracked `Content/_Game/Chest/`, `Docs/Migration/ChestMoveRollback.md`, `Docs/Migration/ProjectSizeCleanup_20260924.md`, `Tools/Migration/Invoke-SafeGeneratedCleanup.ps1`. Do not stage or revert these.
- Existing ACF assets inspected in prior design turn: `/AscentCombatFramework/StatusEffects/ACFDamageOverTimeBP`, `/Game/FullSample/Blueprints/StatusEffects/ACF_PoisonStatus_BP`, `/AscentCombatFramework/Blueprints/DamageTypes/ACFPoisonDamageType`. Reuse contract requires further graph/runtime verification.

## Completion gates

Build PENDING; mathematical automation PENDING; Blueprint compile PENDING; PIE PENDING; visual QA PENDING; save/travel PENDING; performance PENDING; cook PENDING; packaged validation PENDING; skill validation PENDING; invariants PENDING.

## Block 1 evidence

- Build PASS: `Saved/Migration/InnerState20261005/build_block1.log`, 56.09 seconds, receipt repaired with Daz enabled. Applies only to current math/settings block, not final implementation.
- Math PASS: `Saved/Migration/InnerState20261005/Automation_20261005_073306/index.json`, 3 succeeded / 0 warnings / 0 failed / 0 not run. Poison conservation/heal/new exposure, latency, mixed speeds/max-health references, USTRUCT SaveGame roundtrip, partition invariance, infinity rejection, wellbeing and awareness.
- Invariants PASS: `Saved/Migration/InnerState20261005/protected_block1.json`, no changed protected hashes against `protected_before.json`.
- Existing editor closed gracefully with CloseMainWindow (no forced discard); commandlet suite ran and exited. Reopen via approved launcher after next coherent build.
- Core files: `Survival/ProjectInnerStateMath.{h,cpp}`, `Survival/ProjectInnerStateSettings.h`, `Tests/ProjectInnerStateMathTests.cpp` in `EFProjectSystemsGameplay`.
- Reusable commands: `Tools/Migration/Test-InnerState58.ps1`, `Tools/Migration/InnerStateProtection58.py <unique-phase>`.
- Current runtime is not connected yet; all gameplay/UI/assets gates remain PENDING.

## Block 2 integration snapshot

- Build PASS: `Saved/Migration/InnerState20261005/build_block2e.log`, 12.90 seconds incremental (full block build `build_block2.log`:89.94 seconds). Daz receipt repaired. Intermediate adapter compile errors were corrected in project code; final block build succeeded.
- Automation PASS: `Saved/Migration/InnerState20261005/Automation_20261005_080741/index.json`:4 passed,0 warnings,0 failed. Added actual component SaveGame serialization and bipolar legacy compatibility checks. This is not the real gameplay gate.
- Protected invariants PASS: `protected_block2.json`, no changed hashes.
- Skills PASS (metadata validation): `.agents/skills/inner-state-workbench`, `.agents/skills/inner-state-global-qa`. Runtime harness remains PENDING until implemented/run.
- Runtime component now bootstraps beside Needs/Defeat outside dungeon-specific systems; ALS/travel capture includes needs and Inner State. Legacy Pain API is harmful-only, while Sensation supports signed values and normalizes the existing dynamic pain maximum to -100..100.
- Physiological KO uses Awareness, preserves health/pain on waking, and bypasses the combat struggle freeze/invulnerability. Existing nonphysiological combat flow remains intact.
- New status definitions, ACF poison adapter (asset reparenting PENDING), configurable consumable base (authored items PENDING), concussive damage type (weapon assignment PENDING), GAS stamina modifier, practice/wellbeing/madness links and Y Care menu are compiled.
- Care: Rest enters ProjectEmote runtime and recovers Sleep at 15.125 points/calendar hour gross (~12.5 net under normal decay). An empty meter takes ~8 hours =10 real minutes at day1800, or~75 seconds at existing8x time speed if permitted. Y cancels. Wash requires water/head above surface; bathroom anywhere costs5 hygiene. Actual animation/menu behavior PENDING gameplay validation.
- Remaining integration: exposure volumes, existing wash/social/poison assets, item assets and distribution, runtime performance harness, HUD and final gates. No final completion claim.
