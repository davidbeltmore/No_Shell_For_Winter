# Character Creation — visual rework, block 1

Target: `D:/Projects UE5/NoShellForWinter` (UE 5.8). Source
`D:/Projects UE5/LustAsDeadlySin` was not modified. Changes build on the
existing dirty target; unrelated Calysto and clothing work is preserved.

## Presentation

- 55/45 preview/editor split; camera offset follows the preview area, preserving
  orbit, pan, zoom and restoration of the original camera rig.
- Dark theme panel, restrained double outline with constant corner radius,
  Chronicle divider, readable heading. The former landscape frame is no longer
  stretched vertically. The incompatible font resource that rendered repeated
  decorative A glyphs is removed.
- Additional vertical inset follows the camera's constrained aspect ratio and
  viewport DPI, keeping the panel inside the letterbox bars after resizing.
  Header, tabs and Apply/Cancel stay fixed; contents scroll independently.
- Head/Body anatomical navigation, All, full-tab search, per-section scroll
  memory. Only available sections appear. Presets and Hair Advanced collapse.
- Reusable morph cards preserve native callbacks, -3..3 ranges, reset values,
  warnings and technical-name tooltips. Randomize All/Reset All retain global scope.
- Tattoo management and workspace mount through the existing hosts, each with
  vertical scrolling and a shared horizontal scroll. Internal tattoo design is
  deferred to block 2.

## Compatibility

`EFMorphPresentation` replaces substring classification with anatomical tokens,
technical prefixes, aliases and explicit expression handling. Authored category
and section metadata take priority; legacy Upper/Middle/Lower sections migrate
to anatomical groups. Unknown names remain in General & Proportions and are
identified in the classification TSV. Morph identities, destinations and save
keys are unchanged.

Public assets remain `/EFCharacterCreation/UI/WBP_EFCharacterCreationRoot` and
`/EFCharacterCreation/UI/WBP_EFMorphSlider`. Their instantiated presentation uses
the native root and rebound morph controls; the native fallback shares that
layout. No replacement asset path or Blueprint parent was introduced.

`EFProjectDynamicThemeSubsystem` applies shared profiles to the registered
creator widgets, including plugin Blueprint subclasses outside auto-discovery
prefixes. Rebuilt lists notify `OnWidgetReady`. Appearance swatches retain their
NoTheme exclusion. Module dependency direction is unchanged.

The UE 5.8 editable-text Slate style setter requires rebinding the persisted
member style after copying a temporary style; the project adapter does this to
avoid a dangling Slate style pointer. Engine code was not changed.

## Gates and evidence

Durable evidence: `Docs/Migration/Evidence/CharacterCreationRework/`.
Complete working evidence: `Saved/Migration/CharacterCreationRework/`.
All statuses describe this target snapshot, not the source game.

| Gate | Status | Evidence |
|---|---|---|
| Editor build + Daz receipt guard | PASS | Build06.log, project UE 5.8 wrapper |
| Native classification regression | PASS | EF.CharacterCreation.Rework.Anatomy / MeshCoverage, PIE02.log |
| Mesh inventory | PASS | MorphClassification.tsv: Female 446, Male 415; no unknown entries |
| Live category coverage and search | PASS | response_functional03.json: no loss/duplication; Pear stays in Body |
| Morph callback/reset | PASS | response_functional03.json: Eye Almond Inner changed to 0.25 on both mesh components, restored to 0 |
| Blueprint compile | PASS | response_finalstatic.json: both WBP statuses BS_UP_TO_DATE |
| Preset save/load round trip | PASS | response_preset04.json; temporary QA preset removed afterward |
| Older preset compatibility | PENDING | No pre-existing presets were available in the target save slot |
| Apply/Cancel | PASS | ConfirmCancel.json: cancel restored baseline; apply survived reopen; test value restored afterward |
| Section scroll memory | PASS | response_scroll04.json: Eyes offset 210 restored after selecting Nose |
| Skin/iris and animation pause | PASS | AppearanceControls.json: both HSV callbacks and restoration; animation pause |
| Hair availability | PASS / PENDING | Conditional hiding verified; active Hair/Advanced interaction pending a configured hair option |
| Period input | PASS | response_reopenthemefinal.json: Windows Period message reopened a complete 74-widget tree with the active blue theme |
| Camera input | PASS | response_camerabefore2 / cameraafter2 / cameraorbit / camerapan: wheel changes distance, drag changes yaw/pitch, middle drag translates with rotation unchanged; preview reset afterward |
| Equipped clothing regression | PENDING | No equipped clothing case was exercised |
| Theme profiles and appearance swatches | PASS | ThemeProfiles.json: Red, Blue, Purple, Green, Black; swatches unchanged; blue verified while open and after rebuild/reopen |
| Visual resolution matrix | PASS (scoped) | Head720Verified.png, Final1920x1080.png, Body2560x1440.png, HeadUltrawide.png; viewports 1280x720 / 1920x1080 / 2560x1440 / 3440x1440 |
| Letterbox inset | PASS | Letterbox1920x1200.png + response_letterboxgeometry.json: inset adapts to 70 logical pixels, returns to 48 at 16:9 |
| Tattoo containment | PASS (scoped) | Tattoo720.png, TattooEditor1080.png, response_tattoocancel01.json: library/editor/cancel/tab return, no manual layer left behind |
| Protected resolved components | PASS | before.json versus final.json: 5,517 files unchanged (ACFU, Daz plugin/assets, Player, Female, Multiple, Male) |
| Frederick invariant | PENDING | Authoritative path/hash unresolved in the inherited Phase0 baseline; not claimed as hashed |
| Development game build | FAIL (external blocker) | BuildGame.log: pre-existing ProjectCalystoDormantController.cpp:32 uses editor-only UClass::ClassGeneratedBy |
| Cook/package | PENDING (preflight blocked) | PackageAttempt.log: Calysto V6 cook-closure receipt no longer matches the authored policy receipt/asset |
| Packaged runtime validation | PENDING | Requires successful current game build and cook; an older executable was not substituted |

Live inspection used `http://127.0.0.1:8000/mcp`, refreshed tool discovery after
each editor restart, and `EditorToolset.EditorAppToolset` for PIE. Local editor
driver commands and response JSON files retain the inspected operations.
Blueprint compile does not constitute packaged validation.

Snapshots: initial target `b56d423`; classification `87cf813`; layout/camera/Tattoo
`c1b6b71`; shared theme adapter `0875e73`. Prior appearance-hook changes in
`EFCharacterCustomizationComponent.cpp` were deliberately excluded from these
commits and remain in the working tree.

The screenshots include the floating PIE window chrome; recorded viewport sizes
exclude it. The 720p/1440p/ultrawide captures preceded the final letterbox-only
refinement, which leaves their 16:9 or wider vertical insets at 48. The 1080p and
1920x1200 captures verify the final build. The large floating windows were resized
for capture without changing the monitor's display settings.

After the final Blueprint compile, PIE was restarted before the final input and
reopen checks; an in-session Blueprint reinstancing probe is retained as a failed
probe and is not used as evidence of normal creator behavior.

The first UI test exposed the editable-text style lifetime issue; it was fixed
before subsequent builds. Two long-running Python validation attempts were
interrupted after the editor stopped responding. The driver now retains its
Unreal array backing storage and avoids repeatedly enumerating all widgets from
inside another widget loop. Those interrupted runs are not counted as passes.

## Reproduction

1. Build with `Tools/Migration/Build-NoShellForWinterEditor58.ps1`.
2. Launch with `Tools/Migration/Launch-NoShellForWinterEditor58.ps1` and optionally
   execute `Tools/Migration/Validate-CharacterCreationRework58.py`.
3. Run `Automation RunTests EF.CharacterCreation.Rework` in the editor.
4. Start real HUB PIE, open with Period, exercise sections, morphs, presets,
   appearance controls and Tattoo. Test the five shared theme profiles while open.
5. Re-hash protected inputs using `CharacterCreationReworkSnapshot.py <phase>`.
   The tool refuses to overwrite an existing snapshot.

Full release validation remains pending until the independent game/cook blockers
are resolved and the packaged runtime is tested.
