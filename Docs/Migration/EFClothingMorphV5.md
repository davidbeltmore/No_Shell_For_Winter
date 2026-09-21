# EF Clothing Morph V5

> Historical V5.0 design record. The active authoring workflow is V5.1 and is
> documented in `Docs/Migration/EFClothingMorphV5_1.md`. V5.1 retains this
> runtime architecture but exposes only the stable Director as authoring data.

## Outcome

V5 is a parallel, reversible upgrade over the certified V4 surface solver. It
keeps `/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector` as the stable
authoring database and keeps every V4 binding untouched as rollback data.

The V5 layer adds:

- soft, per-LOD binding metadata and on-demand payload loading;
- version-neutral runtime/settings/component APIs;
- semantic garment layers and region occupancy;
- declarative material coverage with `ForceOpaque` as the safe default;
- body profiles and per-garment definitions mirrored from the Director;
- direct equipment/appearance notifications plus a bounded watchdog;
- deterministic generated paths and stable binding identities.

At runtime the canonical manifest is loaded and validated before the V5
registry is accepted. Authored lower-layer occupancy contributes bounded
clearance only where named regions overlap; rows without occupancy preserve the
certified V4.5 fit unchanged.

## Authoring contract

Adding or changing an enabled garment row triggers one debounced editor
transaction. The transaction:

1. applies the row's material policy;
2. validates or refreshes its existing V4 GPU surface binding;
3. mirrors body/garment metadata into V5 Data Assets;
4. publishes only soft binding references in the V5 registry;
5. updates and saves the V5 system manifest.

The source garment, body, skeleton, skin weights, ACFU and DazToUnreal plugins
are never replaced by this process.

## Generated project-owned assets

- `/Game/_Game/Data/EFClothingMorph/V5/DA_EFClothingSystemManifest`
- `/Game/_Game/Data/EFClothingMorph/V5/BodyProfiles/*`
- `/Game/_Game/Data/EFClothingMorph/V5/Garments/*`
- `/Game/_Game/Generated/EFClothingMorph/V5/Materials/*` when a direct or
  protected parent material requires a safe project-owned instance
- `/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingFitRegistry`

The last path is project-plugin content, not Marketplace content.

## Material policy

- `ForceOpaque`: explicit `BLEND_Opaque`; default for every new row.
- `PreserveMasked`: keeps a genuinely masked cutout, otherwise falls back to
  opaque.
- `AllowTranslucent`: explicit exception; it may reveal skin/tattoos and must be
  deliberately scoped by slot or material.

V5 does not modify a shared source material in place. Every project-owned
garment slot that needs a blend-mode decision receives a deterministic managed
child under `/Game/_Game/Generated/EFClothingMorph/V5/Materials`; only that
child is assigned to that garment and overridden. Exact-material exception
rules continue to match the managed child's original parent.

Changing from `ForceOpaque` to `PreserveMasked` or `AllowTranslucent` is
reversible. The managed child restores the authored masked/translucent blend
mode without changing the shared parent. Direct or protected materials are
never edited. The six source MIC overrides produced by the already-accepted
V4.5 fix remain historical project changes, but V5 introduces no new shared-MIC
mutation.

## Runtime and time budgets

- equipment changes: event-driven when Character Creation emits its appearance
  hook;
- missed equipment event watchdog: 2 seconds;
- player-pawn discovery fallback: 0.5 seconds;
- simultaneous binding loads: 2;
- binding load timeout: 5 seconds;
- failed load retry: exponential 0.5, 1, 2, 4, then 8 seconds;
- failure behavior: visible passthrough, never an invisible garment;
- unused V5 binding payloads: released after garment reconciliation;
- every resolved payload: schema, identity, exact LOD and deterministic content
  hash checked before runtime use;
- V4 registry: loaded on startup only when V5 is missing/invalid; when one
  equipped garment/LOD lacks V5 metadata, it is requested once and lazily for
  that compatibility case;
- equipment watchdog: accepts `0` for a fully event-driven integration;
- morph/surface pass interval: the configured V5 throttle is consumed only when
  explicitly greater than zero; the default remains every frame.

## Compatibility

The serialized Director identity remains schema 5 / `EFClothingMorphV4` on
purpose. The stable asset feeds both the rollback compiler and V5 mirror, which
avoids redirecting existing Blueprint or save references. New code should use
`UEFClothingRuntimeComponent`, `UEFClothingMorphSettings`,
`UEFClothingSystemManifest`, `UEFClothingDefinition` and
`UEFClothingBodyProfile`.

## Validation gates

- C++/UHT cold build through the project migration wrapper;
- V5 contract automation test;
- live manifest/registry inspection in UE 5.8;
- six migrated definitions and one body profile;
- all six current material slots evaluated by the declarative policy;
- one short HUB PIE smoke run, followed by manual close-up clothing/tattoo QA;
- cook and packaged validation remain separate release gates.

The reusable bounded native gate is
`Tools/ClothingMorphV5/Run-EFClothingMorphV5NativeAutomation58.ps1` (75 seconds
by default, 120 seconds hard maximum).
