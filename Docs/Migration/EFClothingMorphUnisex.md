# EF Clothing Morph: unisex body fitting

This system update keeps the single public Director at
`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector` and the existing
six clothing identities. The serialized Director schema remains compatible.

## Root cause

Live UE 5.8 MCP inspection on 2026-09-20 found that all six clothing rows
referenced `/Game/DazToUnreal/Female/Female.Female`. Runtime required a unique
visible component using that exact mesh. Switching the visible component to
`/Game/DazToUnreal/Male/Male.Male` therefore left clothing without its surface
producer. V5 metadata also contained only the reference body's bindings.

The original clothing offsets are relative to Female's rest shape. Compiling
against Male's rest shape alone would preserve those original volumes rather
than adapting the chest inward. In addition, `Genesis9_GP_Torso` is not a Male
material slot. Male anatomy shares skin materials and needs branch coverage.

PIE also exposed two visibility integration defects: `HideBoneByName` has no
effect on a LeaderPose follower, and the active UE DQS kernel converts the zero-scale
hidden matrices to rigid dual quaternions, producing stretched anatomy. The
project-owned coverage adapter retains DQS for unaffected vertices and uses
matrix skinning only for vertices influenced by hidden bones. It is applied as
a temporary component override; the Daz graph and body assets remain untouched.
Character Creation now emits a dependency-neutral pre-swap hook so clothing can
restore old material/bone indices before the meshes change in place.
The protected Male mesh uses
`/DeformerGraph/Deformers/DG_DualQuatSkin_Morph_Cloth`. The adapter is built from
the equivalent Daz DQS+morph graph, which is loaded read-only. Source-deformer
identity is explicit in the body registration so other component overrides
are not replaced accidentally.

## Authoring and runtime

- Add each garment once, with the body used when it was authored.
- Register supported meshes once under the Director's `Bodies`. Every garment
  generates a variant for each registered body, independent of gender identity.
- A third body requires its mesh and anatomy rules, not a new gender branch in
  the clothing code. Surface transport requires a compatible shared skeleton
  and body UV atlas; the compiler rejects inadequate correspondence.
- `bCoversGenitals` explicitly enables the body's genital coverage rules.
  Current Panty, Bikini and RagPants cover genitals. Bra, Shirt and FootWraps do
  not. Legacy gameplay coverage tags do not infer visual hiding.
- Female hides `Genesis9_GP_Torso`; Male hides `shaft_01` and `scrotum` branches.
  Fit exclusions remain independent of visual hiding.
- Material aliases translate reference-body slot names. A `None` target drops
  an inapplicable slot instead of hiding a different part of the body.
- Generated variant IDs are internal. ACF items, source meshes, authored IDs,
  Player, body meshes, skin weights and shared skeleton are preserved.
- Cross-body bindings reconstruct the reference shape in target render-index
  space using UV triangle correspondence. The GPU samples the actual animated
  target body and applies the existing bounded bidirectional surface transport.
- Same-body bindings retain their previous behavior. Runtime tracks the body
  asset as well as the component, releases stale ownership before rebinding,
  and reference-counts both section and bone visibility.

## Evidence and reproducible gates

Source project (untouched): `D:/Projects UE5/LustAsDeadlySin`.
Target: `D:/Projects UE5/NoShellForWinter`.
Live inspection: `http://127.0.0.1:8000/mcp`, ObjectTools `get_properties` on the
Director and SkeletalMeshTools `get_material_slots` / `get_bone_names` on Male.

Task baseline and original edited source files: `Saved/ClothingUnisexQA`.
`head.txt` records the initial commit; `git_before.txt` records pre-existing
changes. Do not attribute the rest of this dirty workspace to this update.

| Gate | Current result | Evidence |
|---|---|---|
| C++ / UHT editor build | PASS | `build_editor_8.log` |
| Expanded catalog compilation | PASS: 12/12; six Female bindings reused | `upgrade.json` |
| V5 streamed publication | PASS: two profiles, twelve variants | `upgrade.json` |
| Native contracts / third-body expansion | PASS: both tests, zero warnings | `Saved/Migration/EFClothingMorphV51/NativeAutomation_20260920_194237/Report/index.json` and `NativeAutomation_20260920_194331/Report/index.json` |
| Blueprint compile | PASS: Player and six clothing item Blueprints | `Saved/ClothingMorphV4QA/BlueprintCompile.json` |
| Real ACF equip, body swaps, ownership, morphs and movement in PIE | PASS: 28 checks | `PIE/result.json` and `pie_runner.log` |
| Visual QA | PASS for the six current garments on both bodies and the two tested moving morph combinations | `PIE/*.png`; `PIE/visual_review.json` |
| Game Development build | FAIL: pre-existing Calysto editor-only API usage | `build_game.log`, `distribution_runner.log` |
| Cook and packaged validation | PENDING: prerequisite Game build failed | No current package claimed |
| Protected invariant rehash | PASS: 5,260 files unchanged | `protected_check.log`, `protected_before.json` / `protected_after.json` |

Frederick remains unresolved as an independent asset path in the existing
migration evidence. This update does not reference or write a Frederick asset.

Reusable commands are in `Tools/ClothingMorphV5`: `Upgrade-UnisexCatalog58.py`,
`Run-UnisexPIE58.ps1`, `Capture-UnisexBaseline.py`, and the native runner's
`-TestPath EF.ClothingMorph.Unisex.BodyVariants` option. The personal skill
`ef-clothing-unisex-workbench` documents diagnosis and the validation workflow.

PIE coverage: each garment is equipped through ACF, switched Female/Male/Female,
and unequipped. The overlap fixture temporarily changes the real ACF chest
component to the Panty source (two lower items share one normal equipment slot),
then verifies two/one/zero coverage owners without saving assets or changing slot
rules. Morph/movement fixtures equip Shirt+Pants: Female uses Breasts Large=1.85
and Body Voluptuous=1; Male uses Body Heavy=1 and Proportion Chest Size=0.5.
Both move more than 10 cm while the fitted garments remain active. This is
bounded evidence for current content, not a guarantee for arbitrary imported
meshes, all possible morph combinations, or future LODs.

## Distribution blocker outside this update

The protected Game build wrapper was executed after the 28-check PIE PASS.
`EFClothingMorphRuntime` compiled, but the game build failed on three existing
Calysto errors: `EFCalystoContentCollisionContract.cpp:62` (`GetBoolMetaData`),
the same file at line 99 (`GetMetaData`), and
`ProjectCalystoDormantController.cpp:32` (`UClass::ClassGeneratedBy`). These APIs
are stripped from non-editor builds. Both files already appear as untracked
work in the task's `git_before.txt` (lines 334 and 377); this update does not edit
them. Their runtime identity/bounds contracts need a cooked representation,
rather than merely removing the checks. Cook/package was not started after the
failed prerequisite. Overall release acceptance remains PENDING.
