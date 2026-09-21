# EFClothingMorph V5 - Evidence - 2026-08-31

## Scope

Project-owned final upgrade of the clothing fit/coverage system. V5 keeps the
working V4.5 Director and GPU surface bindings as rollback data while adding
soft per-garment streaming, declarative material coverage, semantic layers,
body/garment Data Assets, runtime appearance events and bounded fallbacks.

Source project `D:/Projects UE5/LustAsDeadlySin` was not opened, hydrated,
modified or resaved. Marketplace ACFU and Engine DazToUnreal were not edited.

## Implementation gates

| Gate | Result | Evidence |
| --- | --- | --- |
| Protected editor build wrapper | PASS | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; final incremental build completed in 32.6 seconds and repaired/validated the Daz-enabled editor receipt. |
| Native V5 contracts | PASS | `EF.ClothingMorph.V5.Contracts`: succeeded 1, warnings 0, failed 0, not run 0. Report: `Saved/Migration/EFClothingMorphV5/NativeAutomation_20260831_073621/Report/index.json`. |
| Bounded native runner | PASS | `Tools/ClothingMorphV5/Run-EFClothingMorphV5NativeAutomation58.ps1`; 75-second default timeout, 120-second hard maximum; completed in 43.01 seconds. |
| Live UE 5.8 manifest | PASS | `/Game/_Game/Data/EFClothingMorph/V5/DA_EFClothingSystemManifest`: schema 1, system `EFClothingMorphV5`, one body profile, six garment definitions, soft V5 registry reference. |
| Live V5 registry | PASS | `/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingFitRegistry`: six soft streamable LOD0 bindings; legacy hard profile/source arrays empty. |
| Managed coverage materials | PASS | Six deterministic project-owned child MICs generated and saved under `/Game/_Game/Generated/EFClothingMorph/V5/Materials`; every child has `bOverride_BlendMode=true` and `BLEND_Opaque`, retaining its original garment MIC as parent. |
| HUB PIE lifecycle smoke | PASS (limited scope) | One in-viewport PIE session only, `/Game/_Game/Hub/HUB`, approximately 43 seconds total; clean start and stop. No second automatic PIE was run. |
| Full clothing/tattoo visual acceptance | PENDING | The short smoke did not exercise a deterministic close-up character-creator/equipment transition. User-owned manual visual QA is required. |
| Cook and packaged validation | PENDING | Not run in this bounded implementation pass. Remains a release gate. |

No Blueprint asset was modified by V5, so a V5-specific Blueprint compile gate
is not applicable. C++/UHT and native contract compilation are covered by the
editor build above.

On the first automation/editor bootstrap, six expected load warnings precede
creation of the six managed MICs. The same run then saves all six children,
reports `applied 6 safe clothing material override(s)` and finishes with the V5
sync current. Later editor startup reports V4 fresh and V5 current without
recreating assets. This is intentional authoring synchronization, so the
native runner can modify a checkout whose V5 generated assets do not yet exist.

The general editor startup still reports the pre-existing project-wide missing
`GameFeatureData` Asset Manager scan rule. It is unrelated to EFClothingMorph
and did not enter the V5 test result, but a completely clean project startup
gate remains `PENDING` until that configuration issue is handled separately.

## Managed opaque material set

- UnderWearBra: `MI_EF_UnderWearBra_G9BaseBra_Bra_Heart_4e36c88d`
- UnderWearPanty: `MI_EF_UnderWearPanty_G9BaseBikini_Undies_Heart_e587c541`
- Bikini: `MI_EF_Bikini_TainaCatwalkBikiniBottom_Cords_3684e97b`
- RagShirt: `MI_EF_RagShirt_SYDFR9RagShirtG9_Shirt_85d34440`
- RagPants: `MI_EF_RagPants_SYDFR9RagPantsG9_Pants_6bc35595`
- RagFootWraps: `MI_EF_RagFootWraps_SYDFR9RagFootWrapsG9_Wraps_855e3931`

The child assignment isolates each garment from shared source MICs. Changing a
row to `PreserveMasked` or `AllowTranslucent` restores the authored blend mode
on the managed child; protected/direct source materials are not edited.

## Runtime safety and compatibility

- Manifest, registry metadata and payloads are loaded through soft references.
- Payload path, identity, schema, exact LOD and deterministic content hash are
  validated before use.
- At most two binding loads run concurrently; each has a five-second timeout.
- Retry delay is bounded to 0.5, 1, 2, 4 and then 8 seconds.
- Missing/failed payloads keep the garment visible through passthrough.
- A missing V5 garment/LOD requests its V4 binding lazily once; a healthy V5
  startup does not eagerly load the V4 registry.
- Appearance changes are event-driven when the Character Creation hook is
  enabled. The watchdog defaults to two seconds and accepts zero for fully
  event-driven integrations.
- Layer occupancy adds clearance only for overlapping named regions. Empty
  occupancy data preserves V4.5 behavior.

## Protected invariants

Recomputed after the final implementation and asset sync:

| Protected item | Bytes | SHA-256 | Result |
| --- | ---: | --- | --- |
| `Content/FullSample/Player.uasset` | 124709 | `E7EDE80A927A34004014F497141097AC64597AD826C9EE85B0077EE7EA33891D` | PASS |
| `Content/DazToUnreal/Female/Female.uasset` | 62494999 | `8C333C85F218B9858CB3BF67E7B9B15ABB071A08689917A90A18965A5A75AF13` | PASS |
| `Content/DazToUnreal/Multiple/Multiple.uasset` | 15185847 | `3FC4E53877C2EA61A766761E12095FA5F9AF48993A2ECE6CE131CFCCA6BF9583` | PASS |
| `Content/DazToUnreal/Male/Male.uasset` | 170736396 | `AD03E3417CE42997EAEBF1B28189692A62BC2126CF707FA75F8C373587DD629C` | PASS |
| ACFU 4.3.5 descriptor | 10270 | `CABE4B8538745D720D67EBC40B85CB3889035F2356DA82319D69E33570F50D73` | PASS |
| DazToUnreal 5.8.0.491 descriptor | 1375 | `66BFFDCDFFBD8DA16057C49E5625EBACA430911E762BA0534F1CCAEDE673469F` | PASS |

The project assets above are also absent from the Git dirty set. Frederick has
no authoritative standalone target asset path in the current evidence index,
so its independent file-hash gate remains `PENDING`; V5 does not reference or
write a Frederick asset.

## Final editor state

- Target project reopened through
  `Tools/Migration/Launch-NoShellForWinterEditor58.ps1`.
- DazToUnreal and EFCharacterCreationDazBridge remain enabled in the editor
  receipt.
- Unreal MCP health check passed.
- PIE running: `false`.
- Generated manifest, registry, definitions, body profile and managed MICs are
  saved (`is_dirty=false`).

## Manual acceptance requested

Open the character creator in HUB, equip each current garment, apply a visible
chest/body tattoo and inspect the covered regions at close range. Expected:
tattoo and skin never render through RagShirt or the other opaque garments;
uncovered skin remains unchanged; garment morph fitting still follows the
current Female body. Record any failure with the garment name and screenshot.
