# Chronicle expanded history input — 2026-09-20

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.
Source `D:/Projects UE5/LustAsDeadlySin` was not accessed or modified.

Contract: Up/Down navigate Chronicle only while the comma Needs HUD is visible
AND Chronicle is expanded with J. An expanded flag or visibility provided by
another gameplay context alone does not enable history input.

Implementation in project-owned `Plugins/EFProjectSystems/Source`:

- `ProjectActivityFeedSubsystem::CanScrollHistory` checks both conditions and
  the live widget. A separate priority-76 input component consumes Up/Down
  pressed/repeat only in that context. Closing the HUD or collapsing Chronicle
  removes it. Other keys remain available. The normal Down/surrender binding
  remains available outside this context.
- `ProjectSurvivalNeedsSubsystem::SetNeedsHudVisible` refreshes Chronicle
  synchronously so changing the comma HUD updates the input context immediately.
- `ProjectActivityFeedWidget::ScrollHistoryByEntries` moves in bounded pixel
  increments, so the first press moves and even rows taller than the viewport
  can be read. Held arrows repeat via engine input events.
- Expanded mode loads all retained entries (default `MaxStoredEntries=80`),
  instead of truncating the display to the latest 20. The legacy expanded limit
  remains config-readable for compatibility but is no longer an active setting.
- New entries preserve the reader's position using an entry sequence plus the
  offset within its row. Reaching the bottom resumes following new entries.
  If retention evicts the anchored entry, the view clamps to the oldest retained
  entry. Compact behavior remains unchanged.

`ProjectChronicleHistoryPieTests.cpp` drives real PlayerController input events
through the input stack on `/Game/_Game/Hub/HUB`. It checks comma/J state gates,
first press, repeat, both directions, history beyond 20 entries, clamping,
new-entry position preservation and restoring lower-priority Down input.
A temporary lower-priority sentinel observes pass-through without causing a
real surrender during the test. The editor test module explicitly depends on
InputCore for the key symbols.

The test explicitly expects one existing HUB startup error, `Can't Start the
quest`. No other errors are suppressed; quest behavior was not changed.

| Gate | Result | Evidence |
| --- | --- | --- |
| Build and Daz receipt | PASS | `Saved/Migration/ChronicleHistory-Build.log` |
| Chronicle Blueprint compile | PASS, 13 widgets, no saves | `Saved/Migration/ChronicleHistory/BlueprintCompile.json` |
| Chronicle automation + history PIE | PASS, 4 tests | `Saved/Migration/ChronicleHistory/Automation.log` |
| Visual history screenshot | PASS, oldest entries visible in expanded HUD | `Saved/Migration/ChronicleHistory/OldestEntries.png` |
| Protected invariant baseline | FAIL, unchanged historical deviations | `Saved/Migration/ChronicleHistory-Invariants.json`; same mismatch list as preceding Chronicle task |
| Cook | PENDING | Not run |
| Packaged validation | PENDING | Not run |

MCP context was read-only: `http://127.0.0.1:8000/mcp`,
`EditorToolset.EditorAppToolset.IsPIERunning` returned false before changes.
No Blueprint/content assets were saved. Run the isolated regression with
`Tools/Migration/Run-ChronicleHistoryPIE58.ps1` after a successful cold build.
Exact edited-file hashes and the starting commit are recorded in
`Saved/Migration/ChronicleHistory/SourceSnapshot.json`.
Full release validation remains PENDING.
