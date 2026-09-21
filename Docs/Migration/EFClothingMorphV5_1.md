# EF Clothing Morph V5.1

## Outcome

V5.1 restores the V4.5 authoring experience: the project has exactly one
public clothing database to maintain:

`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`

Adding, removing or changing a garment is done only in the Director. The
editor bridge derives the runtime catalog, surface registry and body profile,
applies the garment material policy, and saves those outputs automatically.
The previous public V5 mirror under `/Game/_Game/Data/EFClothingMorph/V5` was
retired after Asset Registry validation confirmed that it had no external
referencers.

## Why the Director remains a Data Asset

The Director is a `UPrimaryDataAsset` whose `Garments` property is an array of
`FEFClothingGarmentRow`; that row type derives from `FTableRowBase`. In other
words, it is the project's clothing table even though Unreal labels the asset
as a Data Asset rather than a literal `UDataTable`.

Keeping this class and path is intentional. It preserves the V4.5 grouped row
interface, per-row actions, automatic garment IDs, editor change notifications,
Blueprint references and save compatibility. Replacing it with a literal
`UDataTable` would require a destructive asset/class migration and would lose
the interface the V5.1 request is meant to restore.

## V4.5-style interface

The normal row workflow is shown first and in the same order as V4.5:

1. Clothing Setup
2. Live Fit
3. Fit Surface
4. Advanced Mesh Edit
5. Body Hiding
6. Notes

The new V5 controls remain available without dominating daily authoring:

- `Advanced Layering` is last and collapsed by default.
- `Advanced Material Exceptions` is last and collapsed by default.
- `ForceOpaque` is the default for every new row, so opaque tattoo/skin
  coverage requires no additional author action.

The Director details panel identifies itself as
`EF Clothing Morph V5.1 - Single Clothing Table` and explains that compiled
assets are internal and automatic.

## Internal generated outputs

These assets are implementation details and are not additional databases for
the user to manage:

- `/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingSystemManifest`
- `/EFClothingMorph/_Internal/Compiled/V5/DA_EFClothingFitRegistry`
- `/EFClothingMorph/_Internal/Compiled/V5/BodyProfiles/*`
- `/EFClothingMorph/_Internal/Compiled/V5/Garments/*`
- `/Game/_Game/Generated/EFClothingMorph/V5/Materials/*`

Manifest, definition and body-profile properties are read-only in the editor.
Director, manifest and registry soft paths are configuration-only and no
longer appear as editable Project Settings fields. The managed material
instances stay under `Generated`; they are outputs, not garment records.

The runtime still validates the generated manifest and soft registry before
using streamed bindings. This keeps the V5 safety, bounded loading and V4
fallback behavior without exposing its mirrors as authoring surfaces.

## Compatibility migration

The serialized Director remains schema `5` with Director ID
`EFClothingMorphV4`. V5.1 is a product/editor revision, not an incompatible
save schema.

During the first V5.1 synchronization, only untouched legacy layering defaults
are materialized:

- UnderWearBra_Female: `Underwear`
- UnderWearPanty_Female: `Underwear`
- UnderWearBikini_Female: `Underwear`
- RagShirt, RagPants and RagFootWraps: `Base`

Explicit author choices are never overwritten. All six current rows retain
`ForceOpaque`.

## Bounded validation

- Protected editor build: use
  `Tools/Migration/Build-NoShellForWinterEditor58.ps1`.
- Native contract gate: use
  `Tools/ClothingMorphV5/Run-EFClothingMorphV5NativeAutomation58.ps1`.
- Native automation default timeout: 75 seconds; hard maximum: 120 seconds.
- PIE is not required for a table/editor-only revision. Use one short manual
  visual session when validating tattoo/skin coverage.
- Cook and package remain release gates rather than edit-loop gates.

The contract test is `EF.ClothingMorph.V51.Contracts`. It verifies the stable
Director path, hidden generated paths/settings, `FTableRowBase` row schema,
automatic opaque defaults, internal manifest/profile/definition references and
soft registry publication.
