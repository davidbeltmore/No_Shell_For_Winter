---
name: inner-state-global-qa
description: Run bounded real gameplay validation of all NoShellForWinter Inner State channels, poison/healing, awareness, simultaneous needs, save/load and HUD, with performance evidence outside Calysto Dungeon.
---

# Inner State global gameplay QA

Invoke `noshellforwinter-unreal-mcp` and `inner-state-workbench`. Use the project-owned runtime harness identified in `Docs/Migration/InnerStateImplementation_20261005.md`. If that harness is still PENDING, complete it before claiming runtime coverage; pure math automation is not gameplay evidence.

## Required bounds and evidence

- Use HUB or the dedicated non-dungeon fixture. Never load Calysto, BP_MassiveDungeon, a dungeon floor, or the Dungeon Harness for this suite.
- Cap the automatic gameplay sequence at **120 wall-clock seconds**, including its performance sampling. Fail and report on timeout; never extend the run silently. Editor startup/build/cook are separate gates and must be reported separately.
- Test the actual possessed player components and gameplay callbacks. Capture initial state, inject bounded inputs through public APIs/items, assert resulting values and restore the original state in cleanup. Use disposable PIE for fatal cases. Never save test conditions into the user's HUB or savegame.
- Cover every primary/secondary need, bipolar signs, simultaneous opposed influences, sickness discovery/critical recovery, bathroom deadline/washing, breathing restoration, unconsciousness/waking and fatal asphyxia.
- Cover repeated poison exposures, independent latency, uncapped 200 points, health potion preserving debt, antidote cancelling remaining debt and save/load retaining it. Accelerate calendar inputs explicitly; don't wait multiple in-game days.
- Compare baseline against simultaneous active needs and many influence sources in the same map/pawn. Report sample count, median/p95 frame time and Inner State CPU time, source count, allocated effects/timers and incremental cost. Do not call normal FPS fluctuations a regression or claim zero lag from one sample.
- Inspect one real visible comma-HUD screenshot for frame/layout continuity, all seven compact cells, simultaneous afflictions, signed values, overflow abbreviation and six primary rows. Numeric logs alone cannot prove visual success.
- Output machine-readable results, map/pawn identity, run duration, screenshot/log paths and exact commit/worktree snapshot under `Saved/Migration/InnerState20261005` (or a dated successor). Mark skipped/unavailable gates PENDING, never PASS.

Run once after a coherent build. Rerun only failed coverage or checks affected by a subsequent fix. Follow the project build/launch scripts so Daz and its bridge stay enabled in editor receipts. Complete required Blueprint compile and cook/packaged gates separately before declaring the implementation complete.
