# Chronicle wrap and dynamic-row acceptance

Date: 2026-07-19 (America/Bogota)

## Scope

- Writable target: NoShellForWinter.
- Read-only reference: legacy private build; identity intentionally omitted.
- Runtime owner: `UProjectActivityFeedSubsystem`.
- Widget owner: `UProjectActivityFeedWidget`.
- Public Widget Blueprint paths under `/Game/_Game/Widgets/Chronicle/**` remain unchanged.

## Implemented behavior

- `FProjectChronicleLayoutPolicy` owns the minimum row height, maximum text
  width, inter-row gap, inline-column ratio, and line-height percentage.
- Message, primary, and secondary text blocks use automatic wrapping with
  `DefaultWrapping`, so ordinary text wraps at word boundaries.
- Compact and expanded layouts use configurable maximum widths of 390 and
  450 pixels.
- The fixed row-height override is cleared. Each row retains only a minimum
  height and grows vertically from its wrapped text.
- `EntriesBox` keeps automatic child sizing; the next row is therefore placed
  after the desired height of the previous row.
- Explicit newlines remain inside the same dynamic row.
- Unbreakable tokens are clipped to their row bounds rather than painting
  over neighboring content.
- Unchanged feed snapshots no longer rebuild all transient row widgets every
  tick.

## Acceptance gates

| Gate | Result | Evidence |
|---|---|---|
| Development Editor build | PASS | Cold build after the runtime refresh guard; UBT `Result: Succeeded` |
| Chronicle automation | PASS | `Saved/Migration/Automation/ChronicleWrapAcceptance_20260719_042919/index.json`: 3/3 success |
| Real WBP dynamic-row automation | PASS | `NoShellForWinter.ProjectSystems.UI.Chronicle.DesignerDynamicRows` loads normal/expanded standard, gain, and dialogue WBPs |
| Chronicle WBP compile | PASS | `Saved/Migration/Phase5/Tests/ChronicleWBPCompile_20260719_042857.json`: 13/13 `BS_UP_TO_DATE`, no asset saves |
| Visible HUB PIE | PASS | `Saved/Migration/Phase5/Visual/ChronicleWrap_FinalVisual_20260719_042608/ChronicleVisualQA.json` |
| Compact screenshot | PASS | `Saved/Migration/Phase5/Visual/ChronicleWrap_FinalVisual_20260719_042608/Chronicle_Compact_Window.png` |
| Expanded screenshot | PASS | `Saved/Migration/Phase5/Visual/ChronicleWrap_FinalVisual_20260719_042608/Chronicle_Expanded_Window.png` |
| Development Game build, cook, stage, pak, package, archive | PASS | `Saved/Migration/Phase5/Logs/ChronicleWrapPackage_20260719_043033.stdout.log`: 7,044/7,044 cooked, IoStore success, `BUILD SUCCESSFUL` |
| Packaged executable smoke | PASS | Inner packaged process PID 19536 remained alive for 20 seconds under `-nullrhi -nosound -unattended` and only that owned process was terminated |

The final PIE report contains eight passing checks: both HUD modes visible,
correct compact/expanded state, rows visible, and no overlap in either mode.
Observed desired heights range from the 32-pixel minimum to more than 100
pixels for wrapped standard rows and more than 150 pixels for dialogue rows.

## Project and Calysto boundary

The NoShellForWinter Chronicle contract was validated against the read-only behavioral reference:
the same activity-feed subsystem ownership, public WBP paths, J
expand/collapse contract, channel rendering, explicit newlines, dialogue
columns, and scroll behavior are preserved. This Chronicle change has no
direct dependency on the Calysto procedural runtime and did not modify any
Calysto package.

Full Calysto procedural parity is not promoted by this evidence. The existing
project-wide gate still records `BP_CalystoController` as inherited
`PENDING` because its serialized `/Game/Calysto/World/Blueprint/Legacy/BP_Biome3_0`
dependency is absent from both source and target. Resolving that separate
system requires its own authorized migration and runtime/visual QA.

## Protected invariants

`Saved/Migration/Evidence/ChronicleWrap_ProtectedInvariantVerification_20260719.json`
re-hashes the protected target after the package:

- ACFU 4.3.5: PASS, 5,043/5,043 files.
- DazToUnreal 5.8.0.491 plugin: PASS, 213/213 files.
- Female: PASS.
- Multiple: PASS.
- `Target_Daz_Assets`, Player, and Male: inherited Phase-0 mismatch remains
  (69 total findings), matching the pre-task documented state.
- Frederick remains `PENDING_RUNTIME_IDENTITY`; the migration evidence still
  has no unambiguous asset path to hash.

No protected Marketplace plugin or authoritative character asset was edited
by this task.
