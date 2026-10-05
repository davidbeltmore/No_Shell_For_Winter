---
name: inner-state-global-qa
description: Run bounded real gameplay validation of all NoShellForWinter Inner State channels, poison/healing, awareness, simultaneous needs, save/load and HUD, with performance evidence outside Calysto Dungeon.
---

# Inner State global gameplay QA

Invoke `noshellforwinter-unreal-mcp` and `inner-state-workbench`. The runtime harness is `AProjectInnerStateQA`, invoked with `Project.InnerState.QA`. It uses actual possessed ACF player components and inventory consumption. Pure math automation is not gameplay evidence.

## Run

1. Build through `Tools/Migration/Build-NoShellForWinterEditor58.ps1` and launch through the corresponding `Launch-...` script. Wait for the native MCP server and verify HUB is loaded.
2. Run `python Tools/InnerState/RunEditorPython58.py Tools/InnerState/Run-GlobalPIE58.py`. It starts PIE if needed, disables background CPU throttling temporarily, stops PIE on report/120-second timeout, then restores the editor throttle preference. A floating PIE window is preferred for visual capture. The native fixture has its own 110-second watchdog.
3. Read the newest `Saved/InnerStateQA/<timestamp>/report.json` and `simultaneous.png`. Preserve failed evidence. The fixture deliberately kills the disposable player at the end; never continue/save that session.
4. For packaged validation: build Game through `Tools/Migration/Build-NoShellForWinterGame58.ps1`, run `Tools/InnerState/Package-InnerState58.ps1`, then `Tools/InnerState/Validate-Packaged58.ps1 -StagedRoot <package/Staged>`. It uses isolated UserDir and the same real gameplay matrix on HUB.
   For a native-code-only correction with unchanged content/configuration, `Package-InnerState58.ps1 -ReuseCookedPackage <previous-package>` stages the new executable against the existing cook. Record both package paths; never reuse cooked content after changing assets or their cook configuration.
5. Only when persistence/travel changes: run `Tools/InnerState/Run-SaveTravel58.ps1`. The focused test saves through ALS, travels HUB→StorySelection, loads HUB, checks saved clocks/debt and bounds subsequent physiological recovery. It closes its editor and the external SaveGuard restores original `.sav` files byte-exact. Startup scripts with deferred callbacks must use `EditorPythonScripting.set_keep_python_script_alive(True)`; never quit while an ALS task is running. Loading resumes simulation, so compare against measured saved/loaded calendar time rather than requiring frozen values after a delay.

## Required bounds and evidence

- Use HUB or the dedicated non-dungeon fixture. Never load Calysto, BP_MassiveDungeon, a dungeon floor, or the Dungeon Harness for this suite.
- Cap the automatic gameplay sequence at **120 wall-clock seconds**, including its performance sampling. Fail and report on timeout; never extend the run silently. Editor startup/build/cook are separate gates and must be reported separately.
- Test the actual possessed player components and gameplay callbacks. Use disposable PIE for the full suite, including fatal cases; ending PIE discards fixture state. For isolated nonfatal checks in a continuing session, capture and restore original state. Never save test conditions into the user's HUB or savegame.
- Cover every primary/secondary need, bipolar signs, simultaneous opposed influences, sickness discovery/critical recovery, bathroom deadline/washing, breathing restoration, unconsciousness/waking and fatal asphyxia.
- Cover repeated poison exposures, independent latency, uncapped 200 points, health potion preserving debt, antidote cancelling remaining debt and save/load retaining it. Accelerate calendar inputs explicitly; don't wait multiple in-game days.
- Verify the existing ACF poison asset on an actual HUB enemy without the player survival stack. The fixture starts at 25% health with exactly 25 normal poison debt; verify native death at debt settlement, including float-health rounding. Preserve configured ACF world-item classes in cook so death loot does not produce an ensure. Functional report success alone does not waive runtime errors.
- Compare baseline against simultaneous active needs and many influence sources in the same map/pawn. Report sample count, median/p95 frame time and Inner State CPU time, source count, allocated effects/timers and incremental cost. Do not call normal FPS fluctuations a regression or claim zero lag from one sample.
- Inspect one real visible comma-HUD screenshot for frame/layout continuity, all seven compact cells, simultaneous afflictions, signed values, overflow abbreviation and six primary rows. Numeric logs alone cannot prove visual success.
- Output machine-readable results, map/pawn identity, run duration, screenshot/log paths and exact commit/worktree snapshot under `Saved/InnerStateQA` and the dated migration evidence directory. Mark skipped/unavailable gates PENDING, never PASS. The fixture measures p95 component CPU including its synchronous callbacks; coalesced UI drawing is separately represented by frame samples. Do not conflate background-throttled 3 FPS editor samples with game performance.

Run once after a coherent build. Rerun only failed coverage or checks affected by a subsequent fix. Follow the project build/launch scripts so Daz and its bridge stay enabled in editor receipts. Complete required Blueprint compile and cook/packaged gates separately before declaring the implementation complete.
