# EF Clothing Morph V4 — Body Visibility / Fit Surface Evidence

## Scope

This record covers the independent per-clothing controls added to the V4 Director:

- **Body Sections to Hide in Gameplay**: visual body material visibility only.
- **Body Sections Excluded from Fit**: solver geometry only.

No Daz body mesh, clothing mesh, skin weights, shared skeleton, `Player`, or ACFU asset was edited for this correction.

## Bikini contract

Live Director asset:

`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`

`UnderWearBikini_Female` was verified with:

```text
Body Sections to Hide in Gameplay = []
Body Sections Excluded from Fit   = [Genesis9_GP_Torso]
```

The matching V4 binding is compiler 28 / schema 8 and uses the exact source/body key:

```text
UnderWearBikini_Female
| /Game/DazToUnreal/Bikini/Bikini
| /Game/DazToUnreal/Female/Female
```

## Runtime proof

Cold build:

```text
Tools/Migration/Build-NoShellForWinterEditor58.ps1
NoShellForWinterEditor build and Daz receipt repair: PASS
```

Visible D3D12/SM6 HUB PIE smoke:

`Saved/ClothingMorphV4QA/RuntimeSmoke_20260828_202221/`

For `UnderWearBikini_Female`:

- exact source mesh remained visible;
- state reached `Ready`, not `Passthrough`;
- runtime clearance and inflate changed `0 -> 0.2 -> 0 cm` without a mesh swap;
- idle and real walk checks passed;
- three real ACF unequip/re-equip cycles passed;
- every body material section was checked at every body LOD;
- `Genesis9_GP_Torso` was `shown_by_lod=[true]` while it was listed only as a fit-exclusion owner.

The same isolated smoke cannot certify the Panty/Bra pickup route yet because the current HUB contains no token-matched real ACF world-item fixture for those classes. That fixture limitation is **PENDING**; it is not a runtime binding or visibility failure. The Bikini interaction route is real ACF pickup/equip and completed cleanly.

## Registry routing correction

The first clean-session smoke found that the V4 registry existed but runtime settings still defaulted to the obsolete V3 registry. The corrective source is:

`Plugins/EFClothingMorph/Source/EFClothingMorphRuntime/Public/EFClothingMorphV2Settings.h`

Its default registry now points to:

`/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry`

`Config/DefaultGame.ini` also retains the matching V4 runtime and packaging entries. This makes Bikini/Panty/Bra resolve independently after a clean restart instead of relying on a previous editor session.
