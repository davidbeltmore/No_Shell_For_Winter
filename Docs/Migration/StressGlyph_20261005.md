# Stress glyph: circle under strain

Target: `D:/Projects UE5/NoShellForWinter`. Source project untouched.
Base: `693edb9b8f52dc4d9703b2f65c7186a86d071e4f`.

`ProjectInnerStateGlyph.cpp`, Stress branch: replace the lightning bolt with a nearly circular outline, locally deformed at the upper-right edge. Two separated zigzag edges describe the crack. Retain the shared 24-unit drawing space, 20px desired size, 1.3px stroke, tint, cell dimensions and existing invalidation behavior. No new animation/timer/asset or need calculation changes.

Validation scope: compile Editor/Game, existing Blueprint compile closure, short HUB PIE HUD preview, packaged HUD/runtime smoke. No new unit tests or physiological calibration needed for a vector-only change. Reuse the previously verified full HUB cook from `Package_20261005_125417` because assets/configuration and reflected contracts are unchanged; restage the new executable and record parity. Mark unverified gates PENDING.

- Protected baseline: `Saved/Migration/InnerState20261005/protected_stress_glyph_before.json` PASS, no changed invariants.
- Editor build: `Saved/Migration/StressGlyph20261005/build_editor.log` PASS (45.10s), Daz receipt repaired.
- Game build: `Saved/Migration/StressGlyph20261005/build_game.log` PASS (55.17s), descriptor restored and Daz receipt repaired.
- Package: `Saved/Migration/InnerState20261005/Package_20261005_131135`, PASS, 71.65s. Cook reused as described above; new native executable staged.
- Packaged HUB runtime: `Saved/Migration/InnerState20261005/PackagedQA_20261005_131255/report.json`, PASS 46/46, 87.59s, exit0 and no handled ensure/fatal/native inventory errors. Existing bounded harness reused; no new tests added.
- Packaged visual: `hygiene_water_soap.png` in that same runtime directory, inspected PASS: near-circular outline and localized upper-right break visible in the first compact cell, consistent with the surrounding icons.
- Blueprint compile PASS:26 assets, no failed compile; `Saved/Migration/StressGlyph20261005/blueprint_compile.json`. No assets saved.
- Short real HUB PIE preview PASS: 25-second automatic session; actual player HUD inspected in `Saved/Migration/StressGlyph20261005/hud_preview.png`, runtime identity/values in `preview_runtime.json`, full session `pie.log`. Native MCP confirms PIE ended, editor remains open in HUB. No runtime ensure/fatal/Python errors.
- Protected hashes PASS: `Saved/Migration/InnerState20261005/protected_stress_glyph_delivery.json`, no changes to ACFU,Daz,Player,Female,Frederick,Multiple,Male. Final post-commit snapshot: `protected_stress_glyph_final.json`.
- Staged/native executable parity PASS: `Saved/Migration/StressGlyph20261005/binary_parity.json`.
- Code snapshot: `Saved/Migration/StressGlyph20261005/delivery_snapshot.json`.

Unrelated HUB/chest/cleanup changes remain the user's work and are excluded from this change.
