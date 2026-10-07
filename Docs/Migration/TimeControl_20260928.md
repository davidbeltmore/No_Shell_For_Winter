# Unified time control — 2026-09-28

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8. Source project is read-only.
Starting commit: `5f48f230`. Existing HUB/Chest/cleanup changes are unrelated and preserved.

## Contract

- One world simulation, native global dilation 1/2/4, real engine pause.
- Default calendar day: 1800 simulation seconds. At x1 one calendar hour is 75 real seconds; at x4 it is 18.75 seconds.
- Historical controls at this snapshot: P or Pause toggled player pause; F6/F7/F8 requested x1/x2/x4. Superseded by the explicit October 7 request: see `TimeKeys_20261007.md` for -/=, native P menu and apostrophe wait.
- Acceleration requires standalone, a living initialized pawn with a valid ACF team, and an exact allowed map package (HUB or StorySelection).
- Nearby hostile ACF teams (30 m), a pursuing ACF AI, damage, critical needs, manual timing minigames, and loading/saving prevent acceleration. Automatic stops do not automatically resume acceleration.
- Named pause owners compose: releasing a modal's pause cannot release the player's pause. Resume returns to x1.
- The clock is an ALS-savable actor with calendar days stored as double, not UTC or elapsed time since application startup. A game-instance handoff preserves the calendar across ordinary travel. Loading restores the saved clock and x1.
- Needs retain their authored per-calendar-hour baseline through a single 600/DayLength conversion. Global dilation is already present in DeltaTime and must not be applied again.
- Existing sensations, statuses, GAS effects, animations and DirtyPawn continue using simulation time. No second need simulation is introduced.
- Pause suspends an emote instead of deleting the session. Training and lockpick confirmation reject paused gameplay, including direct component entry points.
- This work does not implement the forthcoming practice XP, book content, prayer animations or training-fatigue system. Their durations must consume calendar hours from the shared clock.

## Validation and evidence

Evidence root: `Saved/Analysis/TimeControl`. `baseline.json` captures 7,973 protected/plugin/save/HUB file hashes before edits. Historical invariant comparison is separate from this task baseline.

Durable receipts, screenshots and implementation/log SHA256 snapshot: [Evidence/TimeControl_20260928/manifest.json](Evidence/TimeControl_20260928/manifest.json).

Reproducible commands (PowerShell ExecutionPolicy Bypass):

1. `Tools/Migration/Build-NoShellForWinterEditor58.ps1`
2. `Tools/Migration/Run-TimeControl58.ps1 -Mode Automation`
3. `Tools/Migration/Run-TimeControl58.ps1 -Mode Visual`
4. `Tools/Migration/Build-NoShellForWinterGame58.ps1`
5. `Tools/Migration/Run-TimeControl58.ps1 -Mode Reload` (optional `-CompileBlueprints` compiles the three dependencies in memory)
6. `Tools/Migration/Package-TimeControl58.ps1`
7. `Tools/Migration/Validate-TimeControlPackaged58.ps1 -StagedRoot <package>/Staged/Windows`

The time runners use Progression-SaveGuard. A successful memory archive round-trip is not evidence of a successful full ALS disk load.

| Gate | Status |
|---|---|
| Live MCP | PASS — `mcp_tools.json`, `mcp_editor_schema.json`, `mcp_live_pie.json`; native `EditorToolset.EditorAppToolset.IsPIERunning` returned true |
| Final Editor build | PASS — `editor_build6.log`, canonical wrapper, Daz receipt repaired |
| Automation mathematics / need rates / clock archive | PASS — `Automation_20260928_020035/index.json`, 4 succeeded, zero failures/warnings |
| Real HUB / StorySelection, threat and pause behavior | PASS — `Visual_20260928_022101/receipt.json`, 34 checks; real hostile ACF dummy moved into radius only in PIE |
| Real ALS save/load and ordinary map travel | PASS — same visual receipt; clock returns to saved time and x1 |
| Fresh-process disk load and clean exit | PASS — `Reload_20260928_025235/receipt.json` and `reload_final.log`; saved 0.509843792 h, restored 0.515177125 h including live frames, x1, no debugger, editor exit 0 |
| Legacy UI pause ownership | PASS — same reload receipt; an external native unpause cannot release an outstanding player pause |
| Blueprint compile | PASS in memory, without saving assets — Player, BP_TSChar and background WBP all BS_UP_TO_DATE in `Reload_20260928_022315/receipt_checks_before_shutdown_fault.json` |
| Visual inspection | PASS — existing HUD retained, x4 and PAUSED visible in `Visual/time_x400000.png` and `Visual/time_paused00003.png`; user independently confirmed x2/x4 |
| Game build | PASS — `game_build3.log`, canonical wrapper and repaired Daz receipt |
| HUB/StorySelection cook and package | PASS — `Package_20260928_024916/uat.log`, 0 errors / 45 warnings, UAT exit 0, HUD cook roots included |
| Packaged time behavior and clean exit | PASS — `PackagedQA_20260928_025422/receipt.json`, x1/x2/x4, frozen clock across two paused samples, resume x1, process exit 0 |
| Packaged HUD appearance | PASS — inspected `PackagedQA_20260928_025422/UserData/Saved/Screenshots/Windows/time_x400000.png` and `time_paused00000.png`; authored chrome restored, clock readable, no HUD redesign |
| Protected hashes | PASS — `protected_hashes.json`, all 7,973 baseline hashes unchanged, including existing HUB and save files; final post-validation comparison in `protected_hashes_final.json` |

These gates cover the time subsystem in the tested scenarios. The cook retains 45 warnings, including vendor redirects/tags, legacy asset versions and existing asset references; runtime logs also retain the existing quest-start error. No claim of a warning-free whole-game migration is made.

## Scope and qualifications

- StorySelection currently opens a story-selection modal that owns a pause. Its pause correctly rejects acceleration; closing a legitimate modal is required before a speed request can succeed. The allowlist includes the map but does not override its modal.
- Releasing the player pause returns to x1. A named modal can retain the pause until its own owner releases it.
- Combat/effect durations remain simulation seconds; future long activities should use calendar hours. A 48-hour book is 60/30/15 real minutes at x1/x2/x4; a three-hour activity is 225/112.5/56.25 real seconds.
- Full ALS world saves persist the clock. Ordinary travel uses a game-instance handoff. This does not add a new-game menu or replace the existing save UI. The existing new-game lifecycle bridge has no authoritative project caller; restarting the application creates a fresh clock, and loading a world restores its clock. Future same-process new-game flow must explicitly reset its calendar.
- No respawn-specific clock reset is introduced: the clock belongs to the world rather than the pawn. A complete death/respawn gameplay scenario is PENDING, separate from the passed ordinary-travel and disk-load scenarios.
- Native simulation pause governs timers, actor/component updates and GAS. The breathing effect's real-time fallback was removed for game worlds. This is not an exhaustive visual certification of every possible animation, physics asset or status combination.
- Computer Use's native pipe was unavailable in this session. Programmatic rendered tests and the user's independent key test are recorded separately; no OS key-injection result is claimed.

## Test harness incidents

Initial visual attempts exposed Python delegate marshaling/lifetime issues and a test that incorrectly expected acceleration while StorySelection's modal was paused. The final visual run retains callback proxies across travel and verifies the actual modal contract. One run was interrupted by the user and is not a completed gate.

Early fresh-process loads passed their functional checks and in-memory Blueprint compiles, then exited with Windows access violation 0xC0000005 in ntdll.dll after the engine log closed. SaveGuard restored originals. Separating Blueprint compilation and allowing PIE teardown to settle did not by themselves resolve the exit fault. A debugger-attached repeat exited cleanly; this alone did not establish a fix.

The packaged probe also passed every time-control assertion, then exited with 777003 (`ECrashExitCodes::CrashReporterCrashed`). A first-chance CDB trace in `package_cdb_runner.log` identified `FCsvProfilerProcessingThread::Run -> TMallocBinnedCommon::FPerThreadFreeBlockLists::ClearTLS -> RtlEnterCriticalSection` after the engine log closed, followed by a crash-reporting thread fault. The QA runners now use the engine's supported `-csvNoProcessingThread` option: the same packaged scenario subsequently passed with exit code 0. This is a test-runner configuration, not an Engine or Marketplace patch or a change to gameplay time. The regular application launch remains unchanged. The reload probe now polls ALS's native idle state instead of keeping a Python delegate across travel and shutdown.

The initial rendered package exposed missing HUD chrome loaded by runtime string paths. `Config/DefaultGame.ini` now explicitly cooks `/Game/_Game/Widgets` and `/Game/_Game/Textures/UI/Themes`, preserving the authored assets rather than replacing their appearance. Package and visual repeat evidence is recorded above.

The final fresh-process reload also completed without a debugger, passed the external-pause-owner regression and exited cleanly with the synchronous CSV setting. Original saves were restored by SaveGuard. The packaged run uses a separate `-UserDir`, including its own SaveGames directory.
