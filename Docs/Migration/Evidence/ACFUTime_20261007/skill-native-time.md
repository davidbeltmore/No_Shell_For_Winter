---
name: acfu-native-time-routines
description: Inspect and maintain the NoShellForWinter unified DaySequence calendar, native ACFU NPC routine schedules, x1/x2/x4/pause, time HUD and ALS calendar persistence.
---

# ACFU native time and routines

Invoke `noshellforwinter-unreal-mcp` first. Read `Docs/Migration/ACFUTime_20261007.md` and `Docs/Migration/TimeControl_20260928.md` in `D:/Projects UE5/NoShellForWinter` for the current validation state.

ACFU uses Unreal's `UDaySequenceSubsystem`, not a separate ACF calendar. `UACFTimeBlueprintLibrary` reads `GetTimeOfDay`; `UACFAIRoutineComponent::GetTimeHours` reads `GetApparentTimeOfDay`. Static-time modifiers or interpolation curves can make those differ; do not attach them to the gameplay clock without an explicit design change.

The project calendar actor lives at `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/DayCycle/ProjectDayCycleStateActor.*`. Preserve its class path, actor name `ProjectDayCycleState`, `ElapsedCalendarDays` SaveGame field and ALS interface. Native DaySequence owns sub-day advancement; the project adds persistent day epoch and travel handoff. Do not reintroduce a DeltaSeconds calendar accumulator alongside native playback.

Global Unreal dilation already affects DaySequence, training, needs, timed statuses, GAS and activities. Use `UProjectDayCycleSubsystem::RequestTimeSpeed`, `SetPauseReason` and `TogglePlayerPause` for gameplay controls, respecting threat/modal checks. Do not multiply DaySequence play rate by x2/x4 as well. A raw native time seek is not equivalent to simulating skipped status/training durations; use it only for explicit calendar restoration or fixtures unless a catch-up design is provided.

The current day is 1800 simulation seconds, with Morning/Afternoon/Night in equal thirds. Preserve the existing widget and original input contracts. Native routine schedules use minute-of-day entries with distinct GUIDs and instanced `UACFTask`s. `UpdateByTime` selects the latest eligible entry, wrapping to the last before the first. Verify the real component changes tasks, not merely that a clock label moves.

Use existing bounded checks under `Tools/Migration`: `Run-TimeControl58.ps1` (Automation/Visual/Reload), canonical Editor/Game build wrappers, `Package-TimeControl58.ps1`, `Validate-TimeControlPackaged58.ps1`, and task protected-hash script. Visual PIE has a 420-second limit and SaveGuard. Read the current report before choosing which checks to repeat; only expand after a new failure or changed subsystem. Require native/calendar agreement, rate/pause, day rollover, routine transition, ALS reload, visual and packaged evidence for a completed migration claim.
