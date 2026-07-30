# Phase 4 — Altar and StorySelection native repair

Date: 2026-07-13

## Scope

- Restore the UE 5.7 Altar presentation without importing the obsolete
  `ACFBaseInteractableBP` parent.
- Preserve the current UE 5.8 ACF Ultimate integration through the
  project-owned `AProjectInnerDoctrineAltar` adapter.
- Restore the six project Material Instances whose
  `/OneClickMaterials/MasterMaterials/M_BasicMaster` parent was stripped.
- Do not migrate or modify Calysto. The user will install its native UE 5.8
  Fab release.

## AssetTools restore

- Reference: read-only legacy private build; identity intentionally omitted.
- Target: NoShellForWinter.
- Restore method: detached UE 5.7 harness plus legacy AssetTools migration,
  explicit overwrite, with a pre-overwrite backup.
- Restored packages: 22 total.
  - 16 packages in the exact Altar visual dependency closure.
  - 6 project Material Instances that use `M_BasicMaster`.
  - 0 Calysto packages.
- Receipt:
  `Saved/Migration/Phase4/NativeWorldRestore/20260713_213434/NativeWorldRestore57Receipt.json`.
- Backup:
  `Saved/Migration/Phase4/NativeWorldRestore/20260713_213434/TargetBackupBefore57Restore`.

## Native Altar reconstruction

`AProjectInnerDoctrineAltar` now recreates the required hierarchy:

- `Sphere`: radius 32, Query Only, Pawn object type, source-equivalent Custom
  overlap responses.
- `StaticMesh`: altar base at relative `(0, 0, -30)`.
- `Book`: relative `(0, 0, 80)`, yaw `-90`.
- `StaticMesh1`: relative `(0, 6, 80)`, scale `(1, 1.0125, 0.7725)`.
- `OnInteractedByPawn` continues to open the project-owned Inner Doctrine
  exchange menu.

## UE 5.8 validation

- Editor build: PASS (`NoShellForWinterEditor`, Win64 Development).
- Altar Blueprint compile: PASS (`BS_UP_TO_DATE`).
- All three StaticMesh/material pairs: PASS.
- All component transforms: PASS.
- Interaction sphere radius: PASS.
- Six OneClick parent references after UE 5.8 resave/reload: PASS.
- Calysto packages in restore manifest: 0.
- Structural receipt:
  `Saved/Migration/Phase4/NativeWorldRestore/20260713_213434/NativeWorldValidate58Receipt.json`.
- Protected invariants: PASS for ACFU 4.3.5, DazToUnreal 5.8, and target DAZ
  assets.
- Protected-invariant receipt:
  `Saved/Migration/Phase4/NativeWorldRestore/20260713_213434/ProtectedInvariantsAfter.json`.

## Visual/runtime QA

- Final PIE run: PASS.
- Runtime report:
  `Saved/QA/AltarStoryFinalQA/20260713_221800/report.json`.
- HUB Altar presentation: PASS. The actor is present at `(-530, 140, 30)`
  with all three visible source meshes and exact transforms/materials.
- ACF interaction contract: PASS. `CanBeInteracted=true`; the Sphere is Query
  Only, Pawn object type, radius 32, and all eight audited engine channels are
  Overlap.
- Exchange interaction: PASS. Invoking the real `OnInteractedByPawn` event
  created and displayed
  `WBP_ProjectInnerDoctrineExchangeMenuGlobal`.
- StorySelection Stone_Wall rendering: PASS. All six runtime room meshes use
  `Stone_Wall`, its `M_BasicMaster` parent survives UE 5.8 load/resave, and the
  world-grid checkerboard is gone in the captured PIE frame.
- Screenshots:
  - `Saved/QA/AltarStoryFinalQA/20260713_221800/01_hub_altar_before_interaction.png`
  - `Saved/QA/AltarStoryFinalQA/20260713_221800/02_hub_altar_after_interaction.png`
  - `Saved/QA/AltarStoryFinalQA/20260713_221800/03_storyselection_pie.png`
- Calysto modified by this repair: false. Its native UE 5.8 Fab installation
  remains a separate user-owned step.
