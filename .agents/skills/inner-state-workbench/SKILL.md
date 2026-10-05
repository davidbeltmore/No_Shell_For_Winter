---
name: inner-state-workbench
description: Tune, extend, inspect and maintain NoShellForWinter Inner State needs, bipolar sensations, awareness, poison, influences, statuses, items, persistence and the comma HUD. Use for changes to rates, durations, synergies or survival content.
---

# Inner State workbench

Invoke `noshellforwinter-unreal-mcp` first. Follow project AGENTS.md. Work only in NoShellForWinter and project-owned plugins; never modify the source game, Engine or Marketplace code.

Read `Docs/Migration/InnerStateImplementation_20261005.md` for the accepted contract and validation ledger. Source of truth is `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Survival/ProjectInnerStateSettings.h`, `ProjectInnerStateComponent`, and `ProjectInnerStateMath`.

## Changing numbers

1. Inspect live day length and `ProjectDayCycleSettings`; do not assume real seconds equal calendar hours. Conversion: `calendarHours = simulationSeconds * 24 / dayLengthSeconds`.
2. Change a named setting, never a UI display constant. Express slow rates per calendar hour, acute rates per simulation second. Record depletion/recovery times with the current day length and with a second day length.
3. Use half-life smoothing for targets and the exact damped wellbeing integrator. Sum independent positive/negative contributions before clamping. Avoid feedback that writes to its own inputs during the same step.
4. Preserve the poison invariant: every exposure snapshots max HP; pending damage only decreases through delivered damage or treatment. Health potions never touch it. Poison has no gameplay maximum. Never restart old incubation when adding poison.
5. Preserve legacy `Pain` asset/API identity. Public Sensation is bipolar; negative pleasure must never count as harmful pain. Awareness alone owns physiological unconsciousness and waking must not grant health.
6. Update the implementation document's timing table and run focused math tests with `Tools/Migration/Test-InnerState58.ps1`. Use `inner-state-global-qa` when runtime behavior changes.

## Adding an effect or item

Use a stable source ID and refresh/remove only the effect's owned entry. Families prevent farming equivalent benefits. Prefer data/configuration to a new tick or timer. Keep UI a read-only observer. Existing ACF health/status/item contracts remain the integration points; never apply both ACF DOT and the Inner State poison debt for one exposure.

Persistent state must survive ALS save/load and travel via `ProjectProgressionState`. Do not serialize live actor pointers or wall-clock deadlines. Document new fields' neutral defaults for old saves.

Use small commits and record build, BP, runtime, visual, cook/package evidence. Hash protected assets with `Tools/Migration/InnerStateProtection58.py` before/after. Mark absent evidence PENDING. Avoid repeated full suites when focused checks answer the question.
