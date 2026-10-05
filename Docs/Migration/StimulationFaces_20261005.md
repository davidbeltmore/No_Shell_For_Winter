# Stimulation faces and first-card order

Target: `D:/Projects UE5/NoShellForWinter`; source game untouched. Base snapshot: `bbb37618807cc3fd39caf33e497461e8d8ab5ae2`.

## Display contract

The compact comma-HUD order is Stimulation, Stress, Hygiene, Bathroom, Air, Composure and grouped afflictions. Existing authored Willpower/Offensive slots remain intact; runtime survival display data fills those first two slots. Signed numbers and tooltips follow channel identity rather than positional indices.

| Actual value | Face | Symbol |
|---|---|---|
| Above +20 | Happy circular face | StimulationHappy |
| Below -20 | Sad circular face | StimulationSad |
| -20 through +20 inclusive | Neutral circular face | Stimulation |

Use the unrounded value: +20.01 is happy and -20.01 is sad even though the compact integer rounds to +20/-20. There is no hysteresis. Canonical `Stimulation` remains a valid neutral symbol for existing designer references. All other shapes remain unchanged, including the cracked Stress ring and Hygiene soap. Preserve the shared 20px size, 24-unit vector space, stroke and tint.

## Data flow and maintenance

`FProjectInnerStateMath::GetStimulationMoodBand` owns the thresholds. `UProjectInnerStateGlyph::ResolveStimulationSymbol` maps that band to a face. The Doctrine display builder reads the current value, chooses the symbol and preserves the existing card/widget identity. The existing Slate leaf draws a circle, eyes and the appropriate mouth.

`UProjectInnerStateComponent::Publish` also caches the mood band in its transient publication map. A fractional crossing of +20/-20 must notify the existing delegate even if integer rounding has not changed. The HUD then uses its existing coalesced refresh. No new timer/tick, texture, binding, serialized field or physiological rate. This avoids delayed faces without broadcasting every tiny numerical change.

Assets/configuration and reflected layouts are unchanged; the native executable can reuse the previously verified full HUB cook `Package_20261005_125417`.

## Reusable focused gameplay QA

Run `python Tools/InnerState/RunEditorPython58.py Tools/InnerState/Validate-StimulationFaces58.py` with target editor on HUB and no PIE. The disposable fixture has a 45s watchdog and restores editor throttling, ends PIE and never saves gameplay or the map.

Seven exact cases: +50, -50, 0, -20, +20, -20.01, +20.01. Physiology is temporarily paused to hold boundaries. The test reads actual owned public glyph/text objects, verifies the face, signed number, tooltip, and Stimulation/Stress indices 0/1 in the same real HorizontalBox. It captures happy/sad/neutral screenshots for visual review.

Two additional cases resume real simulation from +20.00001 and -20.00001, wait for natural wellbeing relaxation, and verify the face becomes neutral while the displayed integer stays +20/-20. No manual HUD refresh is issued after the crossing.

Python notes: card display data is non-reflected and private widget pointers are inaccessible; use public owned glyph/text objects. UE5.8 exports `SlateLibrary`. Cached geometry can be zero while paused, so use real HorizontalBox child indices and inspect screenshot order. Failed inspector attempts are retained alongside passing reports; game access modifiers were not changed for QA.

## Evidence ledger

Evidence root: `Saved/Migration/StimulationFace20261005`.

- Protected baseline PASS: `Saved/Migration/InnerState20261005/protected_stimulation_face_before.json`, no changed invariants.
- Final Editor build PASS: `build_editor_final.log`, 71.14s; Daz receipt repair PASS.
- Final Game build PASS: `build_game_final.log`, 87.52s; descriptor restored and receipt repair PASS.
- Final native package PASS: `Saved/Migration/InnerState20261005/Package_20261005_141542`, BuildCookRun 57.86s; unchanged full cook reused as above.
- Initial packaged visual/runtime PASS: `PackagedQA_20261005_135325`, 46/46 in 87.576s; neutral/sad faces first, Stress second. Superseded for final binary validation by the post-publication-fix run below.
- Initial focused UI PASS: `PIE_20261005_140921`, seven exact cases in 19.813s. Final nine-case run PASS: `PIE_20261005_142132/report.json`, 28.844s. Both natural crossings reached neutral at 19.9923098435/-19.9884608771 while their displayed integers remained +20/-20. Three final screenshots inspected PASS: happy, sad and neutral; Stimulation first, Stress second.
- Final Blueprint compile PASS: 26 assets, `blueprint_compile_final.json`, no assets saved.
- Final packaged HUB run PASS: `Saved/Migration/InnerState20261005/PackagedQA_20261005_141724/report.json`, 46/46 in 87.558s; strict exit/runtime-error validation PASS. With 200 influences, component p95 .2016ms. No new per-effect timers.
- Final editor context: HUB, PIE stopped. `pie_final.log` retains the session; no handled ensure/fatal/native inventory errors.
- Final source/staged executable parity: `binary_parity.json`. Protected delivery: `Saved/Migration/InnerState20261005/protected_stimulation_face_delivery.json`. Commit/file snapshot: `delivery_snapshot.json`.

Unrelated HUB/chest/cleanup work is excluded from this change. Final commit/file snapshot will be recorded in `delivery_snapshot.json` under the evidence root.
