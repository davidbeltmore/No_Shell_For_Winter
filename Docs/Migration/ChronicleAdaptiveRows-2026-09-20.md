# Chronicle adaptive rows — 2026-09-20

Target: `D:/Projects UE5/NoShellForWinter`, Unreal Engine 5.8.
Read-only source `D:/Projects UE5/LustAsDeadlySin` was not accessed or changed.
This extends the previous Chronicle typography change.

Changes are confined to project-owned `Plugins/EFProjectSystems` C++ and QA:

- `ProjectActivityFeedSettings.cpp`: compact/expanded names use 8/9 point text,
  while messages retain 10/11 points. Designer previews and row defaults agree.
- `ProjectActivityFeedEntryRowWidget.cpp`: type and name remain centered;
  the name fills its allocated column and is centered across the full row height.
  The inline container fills the row vertically, so a short name stays centered
  alongside any number of message lines. All rows receive symmetric padding.
- Message justification uses the actual configured font's measured width:
  one-line messages center horizontally and vertically, longer messages use
  readable left-aligned wrapping. Explicit newlines remain paragraphs. Words
  wrap normally, with a character fallback only for tokens wider than a column.
  Row height remains content-driven; replacing a long message also shrinks it.
- `ProjectActivityFeedSubsystem::AddDialogueEntry`: a message without a speaker
  is no longer duplicated as the speaker's name. It uses the full message area.
- Existing public widget bindings and `/Game/_Game/Widgets/Chronicle` paths
  remain intact; no assets were saved or regenerated.

Evidence is under `Saved/Migration/ChronicleAdaptive*`. MCP endpoint:
`http://127.0.0.1:8000/mcp`, `EditorToolset.EditorAppToolset`; initial
`IsPIERunning` was false. Visible images use `CaptureEditorImage`, because the
fixture's high-resolution viewport screenshot does not include UMG.

| Gate | Result | Evidence |
| --- | --- | --- |
| Editor build + Daz receipt | PASS | `ChronicleAdaptive-Build.log` |
| 13 project Chronicle Blueprint compiles | PASS | `ChronicleAdaptive/BlueprintCompile.json` |
| Native and six Blueprint row variants; layout policy | PASS | `ChronicleAdaptive-Automation.log` |
| Long-token wrapping and long-to-short row reuse | PASS | Same automation log |
| Compact/expanded visual inspection | PASS | `ChronicleAdaptive/Compact_Editor.png`, `ChronicleAdaptive/Expanded_Editor.png` |
| PIE mode/visibility fixture | PASS | `ChronicleAdaptive/ChronicleVisualQA.json` |
| Exact runtime geometry via Python | PENDING | Native row wrappers return empty cached geometry; `ChronicleAdaptive/GeometryProbeUnavailable.json` |
| Historical protected invariant gate | FAIL, unchanged historical deviations | `ChronicleAdaptive-Invariants.json`; mismatch list equals previous typography run, Daz plugin PASS |
| Cook | PENDING | Not run |
| Packaged validation | PENDING | Not run |

`Tools/Migration/Validate-ChronicleAdaptiveRows58.py` seeds a real HUB NPC bark,
short and long NPC dialogue, speakerless dialogue, and short/long system logs.
Native automation tests cover standard, gain and dialogue variants in both modes,
configured name sizes, centering, long-message growth, and row reuse.
Automation and PIE execute separately to avoid automation teardown invalidating
the PIE world. The first geometry probe was discarded after correcting the UE
Python API name to `SlateLibrary`. Subsequent geometry probes found that native
wrappers and their Slate roots do not expose populated cached geometry to Python.
The fixture explicitly marks that measurement PENDING and removes the legacy
empty-list overlap assertions, rather than presenting them as measured PASS.
Visual inspection and native automation are the layout evidence for this change.

Full release validation remains PENDING. Exact edited file hashes and base commit
are recorded in `ChronicleAdaptive/SourceSnapshot.json`.
