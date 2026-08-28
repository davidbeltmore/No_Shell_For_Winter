# EF Clothing Morph V4 — Multi-Clothing Director

V4 keeps one public control asset:

`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`

Open it and add one element to **Clothes** for every clothing mesh/body pair. The visible interface is intentionally simple and entirely in English.

## Adding clothing

1. Add an element under **Clothes**.
2. Assign **Clothing Mesh**.
3. Assign **Body Mesh** (`Female` for the current certified fixtures).
4. Leave **Clothing Name** empty if an automatic `<ClothingMesh>_<BodyMesh>` name is desired, or type a unique name manually.
5. Enable **Use This Clothing**.
6. Press **Update This Clothing**.

An unfinished element is a draft. It cannot disable, remove or invalidate clothes that are already working. Several clothing components may be equipped simultaneously; each resolves its own binding and runtime controls.

## Live Fit

- **Skin Gap (cm)** adds non-destructive space between this clothing and the final animated skin.
- **Surface Volume (cm)** moves this clothing's rendered surface outward without changing topology.

Both values are per clothing entry and update at runtime. They do not require recompilation and cannot alter another equipped clothing component.

## Advanced Mesh Edit

The Native Offset and Create Shell controls are explicit Unreal Engine mesh edits. Changing their settings alone does nothing. These actions are optional and are not required for the V4 runtime constraint. After a deliberate topology edit, press **Update This Clothing** to rebuild only that entry's fit data.

## Publication rules

- Editor/PIE refresh is tolerant: valid clothes remain published if another row is a draft or fails.
- The row button updates only the selected Clothing Name.
- Cook/package certification is strict: every enabled row must have one fresh compiler-28/schema-8 binding.
- V4 never creates or swaps to a fitted Skeletal Mesh. The exact **Clothing Mesh** remains authoritative and visible.
- Body meshes, shared skeleton, morphs and skin weights are read-only inputs.

## Current fixtures

- `UnderWearBra_Female`
- `UnderWearPanty_Female`

Both are certified against Female LOD0 and have been tested simultaneously through the real ACF pickup/equipment path. See `Docs/Migration/Evidence/EFClothingMorphV4_MultiClothing_20260828.md`.

## Tools

```powershell
Tools\ClothingMorphV4\Migrate-EFClothingMorphDirectorV4.ps1
Tools\ClothingMorphV4\Compile-EFClothingMorphV4Catalog58.ps1
Tools\ClothingMorphV4\Run-EFClothingMorphV4RuntimeSmokePIE58.ps1
Tools\ClothingMorphV4\Test-EFClothingMorphV4PackagedStartup58.ps1
```
