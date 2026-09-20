# Chronicle typography and wrapping — 2026-09-20

Target: `D:/Projects UE5/NoShellForWinter` (UE 5.8). Source
`D:/Projects UE5/LustAsDeadlySin` was not accessed or modified.
Base commit: `5ec929d76546e95249cab4735f96176e3ca7911f`.
Exact edited-file hashes: `Saved/Migration/ChronicleTypography/SourceSnapshot.json`.

Project-owned changes in `Plugins/EFProjectSystems/Source`:

- `EFProjectSystemsGameplay/UI/ProjectActivityFeedSettings.cpp`: body/name sizes
  10/11, badge sizes 9/10, compact/expanded line spacing 1.08/1.10, name column
  30% of the inline text area.
- `UI/ProjectActivityFeedEntryRowWidget.cpp` in the same module: apply configured
  font sizes to Blueprint trees while preserving their font family and style;
  prevent badge text enlargement; subtract actual decoration widths and slot
  padding before calculating wrapping widths. Preserve automatic row height.
- `UI/ProjectChroniclePanelWidget.cpp`: title size 18/20 for both Blueprint
  and native panels.
- `EFProjectSystemsEditor/Tests/ProjectChronicleLayoutTests.cpp`: updated policy
  expectations; existing native and six Blueprint row growth tests retained.

No content assets were saved. Widget paths under `/Game/_Game/Widgets/Chronicle`
are preserved. Runtime settings apply without regenerating the designer trees.

| Gate | Result | Evidence |
| --- | --- | --- |
| Editor build and Daz receipt | PASS | `Saved/Migration/ChronicleTypography-Build.log` |
| 13 Chronicle WBP compiles | PASS | `Saved/Migration/ChronicleTypography/BlueprintCompile.json` |
| Three Chronicle automation tests | PASS | `Saved/Migration/ChronicleTypography/Automation.log` |
| Visual inspection in PIE | PASS | MCP `http://127.0.0.1:8000/mcp`, `EditorToolset.EditorAppToolset.CaptureEditorImage`; compact and expanded inspected, smaller text and multiline rows contained within their frames |
| Expanded screenshot | PASS | `Saved/Migration/ChronicleTypography/Expanded_Editor.png` |
| PIE fixture completion | PASS | `Saved/Migration/ChronicleTypography/ChronicleVisualQA.json` |
| Cook | PENDING | Not run for this change |
| Packaged validation | PENDING | Not run for this change |
| Historical protected invariant comparison | FAIL | `Saved/Migration/ChronicleTypography-Invariants.json`: historical baseline differs for ACFU loading-screen asset and Daz assets; Daz plugin passes. This change did not write those assets. |

Run `Tools/Migration/Validate-ChronicleTypography58.py` through the existing
environment-gated startup hook for Blueprint compile plus the compact/expanded
PIE fixture. Run automation tests separately: ending the automation session
invalidates the simultaneous PIE fixture's subsystem instances. The first
combined run was discarded for PIE validation.

The fixture's high-resolution screenshots omit UMG; they are not visual proof.
Use the MCP editor captures instead. The compact saved capture includes an
auxiliary log window and is reduced in size; the expanded capture is unobstructed.
Full release validation remains PENDING.
