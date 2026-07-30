# UE 5.8 Game Features and ACF broken-reference repair

Date: 2026-07-19 America/Bogota

## Scope

- Reference: legacy private build (read-only; identity omitted)
- Target: NoShellForWinter
- Report: missing `GameFeatureData` Asset Manager registration plus eleven missing packages referenced by FullSample assets.

## Changes

- Added the UE 5.8 `GameFeatureData` primary-asset scan entry to `Config/DefaultGame.ini`.
- Added a project-level class redirect:
  - `/Script/CharacterController.ACFOverlaySetDataAsset`
  - `/Script/CharacterController.ACFAnimsetDataAsset`
- Kept the package redirect for `AP_GhostSamurai_Execution02`, whose old and new export names match.
- Added target-owned compatibility assets at the historical object paths where package redirects could not preserve a changed export name:
  - `/Game/AnimationLibrary/AnimBP/ALS_N_AO`
  - `/Game/AnimationLibrary/Montages/Sources/Protector_Defense_Hit_Broken`
  - `/Game/_Temp/Ishti/Material/MI_UnitColorTemp_Green`
- Removed seven dangling TTS/facial-animation references from:
  - `/Game/FullSample/Integrations/ATSIntegrations/Graphs/Dialogues/ADS_ACFGuy_Finale`
  - `/Game/FullSample/Integrations/ATSIntegrations/Graphs/Dialogues/ADS_ACFGuy_LearnACF`
  - `/Game/FullSample/Integrations/ATSIntegrations/Graphs/Dialogues/ADS_ACFGuy_LearnCombat2`
- Pre-change copies of the three dialogue assets are under `Saved/Migration/BrokenReferenceRepair_20260719/Before`.

## Evidence

- Cold build: `NoShellForWinterEditor Win64 Development` — `Result: Succeeded`.
- Live Asset Registry:
  - `ACF_PlayerOverlays` loads as `ACFAnimsetDataAsset`.
  - Its neutral aim dependency resolves to `/Game/AnimationLibrary/AnimBP/ALS_N_AO`.
  - `ACF_DefenseBroken_AM` resolves `Protector_Defense_Hit_Broken`.
  - `ACF_GhostSamurai_Execution02_Montage` resolves `AP_GhostSamurai_Execution02`.
  - `Arrow` resolves `MI_UnitColorTemp_Green`.
  - The three dialogue graphs no longer list the missing TTS/facial packages.
- Clean editor restart log: zero matches for the reported `GameFeatureData`, missing-package, class-load, or failed-import errors.
- HUB PIE:
  - `No blueprints needed recompiling`
  - `PIE: Server logged in`
  - zero matches for the reported errors and for `LoadErrors: Error`.
- Source read-only verification: `PASS`.
- Protected invariant receipt: `Saved/Migration/BrokenReferenceRepair_20260719/FINAL_ProtectedInvariants.json`.
  - `ACFU_4_3_5`: 5043 expected/current files, 0 mismatches, `PASS`.
  - `DazToUnreal_5_8_0_491`: 213 expected/current files, 0 mismatches, `PASS`.
  - Overall receipt remains `FAIL` because of 69 pre-existing target Daz/Player baseline differences unrelated to this repair.

## Remaining gates

- Visual gameplay review of the substituted neutral aim offset and green arrow material: `PENDING`.
- Full cook and packaged validation for this repair: `PENDING`.
