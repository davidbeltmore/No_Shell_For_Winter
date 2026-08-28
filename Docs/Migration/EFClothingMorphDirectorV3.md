# EF Clothing Morph V3 - Native-First Director

## Purpose

EF Clothing Morph V3 keeps Unreal Engine as the source of truth for garment
geometry, skin weights, morph targets, Chaos Cloth data, and normal animation.
It adds a late, non-destructive surface guard to reduce skin clipping without
replacing the garment with a generated fitted mesh.

The only public authoring asset is:

`/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`

It is an `EF Clothing Morph Director V3` Primary Data Asset with schema `4` and
Director ID `EFClothingMorphV3`. Its single **Garments** array is the complete
catalog. There are no parallel public garment tables or per-garment tuning
assets to maintain.

## Native-first contract

- **Editable Garment Mesh** is the exact Skeletal Mesh equipped by gameplay.
- V3 does not assign a fitted replacement mesh, `EF_AutoFit` profile, leader
  pose, or replacement skin weights.
- The garment follows its existing Unreal/DAZ deformation path first. V3 then
  composes a late collision-only surface constraint over that output.
- Runtime tuning changes only scalar parameters on the live component. It does
  not rebuild, swap, save, or intentionally hide the mesh.
- If a valid binding cannot run, V3 restores the upstream deformer and leaves
  the original garment visible in `Passthrough`. `GetDebugSummary()` records the
  reason; disappearance is not the intended fallback.
- Female, Male, Multiple, Player, the protected body/compatibility meshes, and
  the shared `USkeleton` are not authoring targets for this system. The garment
  selected as **Editable Garment Mesh** remains the deliberate exception.

The binding-only runtime cache is internal:

`/EFClothingMorph/_Internal/Compiled/V3`

It currently contains one registry and one surface binding for the enabled
garment/body pair. It contains no fitted Skeletal Mesh and no generated fit
profile. Do not edit or equip internal assets manually.

The seven V26 generated assets remain in the project unchanged as rollback
material. They are inactive: the V3 registry and runtime do not reference them,
and the current cook configuration includes only the V3 compiled root. The
final package contains the active `6C87...` V3 binding and registry, with no
legacy `F07...` binding or V26 compiled path. Permanent V26 deletion remains
`PENDING` until a live Unreal Asset Registry audit can confirm deletion safety.

## Director fields

Every garment index contains all of its own controls.

### Garment

- **Use as Garment** enables this entry.
- **Garment ID / Index** is the stable runtime, compiler, and binding identity.
  Array order is organizational only.
- **Editable Garment Mesh** is the authoritative mesh that may be edited and
  saved with Unreal Engine's native tools.
- **Reference Body Mesh** is the exact visible DAZ body used as the collision
  surface, such as Female or Male. Never use Multiple as the visible surface.
- **Open Editable Mesh** opens the source in the native Skeletal Mesh Editor.
- **Refresh Binding** rebuilds stale internal binding data without replacing
  the garment or changing a body or skeleton.

Both the editor refresh gate and the command-line compiler audit stale assets
only inside the internal V3 binding folder. They delete an obsolete binding
only when Unreal reference checks find no consumer. The command-line receipt
`compiler_receipt_20260828_063511.json` records removal of the obsolete
`F07...` V3 binding; the final `064139` compile retained only the active
`6C87...` binding. The C++ gate was rebuilt and exercised in a later fresh PIE
run, but its positive stale-binding deletion branch was not separately forced.
Neither cleanup path targets V26 or public assets.

### Runtime Fit (Immediate, Per Garment)

- **Skin Clearance (cm, Runtime)** adds outward clearance from the final
  animated skin. It is per garment, immediate, and does not add thickness.
- **Surface Inflate (cm, Runtime)** moves the rendered garment surface outward
  along the animated body normal. It is per garment, immediate, and does not
  create vertices, an inner layer, or boundary walls.

Both runtime values can be changed from `0.0` to another value and back while
the garment remains equipped and `Ready`. They are deliberately excluded from
the binding fingerprint, so they require no compile or binding refresh. Start
with small values such as `0.02` to `0.10 cm`; use larger values only after
checking the silhouette in motion.

V3 has no hidden base clearance or compiler reserve. The final compiler receipt
records both as `0.0 cm`. At `0.0 cm`, the authored silhouette passes through
unchanged wherever it is already outside the skin; the unilateral guard moves
only points that would penetrate the animated body. Every intentional visible
gap therefore comes from this garment's Director value or its per-component
runtime override.

The component API exposes the same non-destructive controls:

```text
SetGarmentClearanceOffsetCm(GarmentComponent, ClearanceCm)
ClearGarmentClearanceOffsetCm(GarmentComponent)
SetGarmentInflateCm(GarmentComponent, InflateCm)
ClearGarmentInflateCm(GarmentComponent)
```

An API override applies only to the supplied component. Clearing it returns to
that garment entry's Director value.

### Native UE Offset (Explicit Mesh Edit)

This group is an authoring operation, not a live slider. **Offset Type**,
**Distance (cm)**, **Steps**, **Offset Boundaries**, **Smoothing Per Step**, and
**Reproject After Smoothing** do nothing until **Apply Native Offset to Editable
Mesh** is pressed.

The implementation uses Unreal Geometry Scripting against LOD0, opens an Undo
transaction, preserves materials, checks the skeleton, vertex/index counts,
skin attributes, morph names, and skin-weight profiles, and never auto-saves.
It attempts transactional rollback when an integrity check fails; if rollback
cannot complete safely, close the editor without saving the source asset.

The final evidence set did not execute this topology/geometry-writing button.
Actual Native Offset application, Undo, save/reopen, and dependency integrity
therefore remain `PENDING` manual authoring gates.

You may instead edit the source directly with Unreal Engine's native Skeletal
Mesh tools. After any saved geometry or topology edit, return to the Director
and press **Refresh Binding** before Play. Runtime clearance and inflate changes
do not require this step.

### Body Coverage

- **Body Sections to Exclude** lists body material-slot names to hide while
  this garment is equipped and to exclude from surface candidates.
- **Covered Body Regions** stores optional gameplay tags and does not deform
  geometry.

`UnderWearPanty_Female` declares `Genesis9_GP_Torso`. This is the intended
Golden Palace exclusion: the covered auxiliary body section is excluded from
fit candidates and should be hidden while the garment is equipped, then have
its prior visibility restored on unequip. The Female asset is not modified.
The final smoke did not explicitly assert section visibility and restoration,
so that runtime behavior remains `PENDING` even though the data and code path
are implemented. The ambiguous crotch domain is not claimed as geometrically
solved by V3.

### Real Geometry (Advanced)

**Create Shell on Editable Mesh...** is an explicit topology-changing source
edit. It uses **Surface Inflate** as the requested real shell thickness. It is
not required for anti-clipping and is refused automatically when the garment
has morph targets or Chaos Cloth data that would become topology-stale. It is
transactional and never auto-saves.

Prefer runtime **Surface Inflate** for visual thickness. Create a real shell
only when the garment genuinely needs inner/outer geometry and boundary walls,
and review all dependent morph, weight, and Cloth data afterward. Actual shell
creation and rollback were not executed in the final evidence set and remain
`PENDING` manual authoring gates.

## Adding or editing a garment

1. Add an element to **Garments**.
2. Assign a unique, stable **Garment ID / Index**.
3. Assign the original Skeletal Mesh to **Editable Garment Mesh**.
4. Assign its exact visible DAZ **Reference Body Mesh**.
5. Add body-section exclusions only where the garment really covers auxiliary
   anatomy.
6. Leave runtime clearance and inflate at `0.0 cm` for the first comparison.
7. Enable **Use as Garment** and save the Director.
8. Press **Refresh Binding**, or run the catalog compiler with the editor
   closed:

```powershell
Tools\ClothingMorphV3\Compile-EFClothingMorphV3Catalog58.ps1
```

9. Equip the garment through its real gameplay path and verify `Ready` in Idle
   and locomotion before increasing runtime values.

The compiler fingerprints the saved source mesh and reference body, generates
only binding data, validates exact render topology and LOD pairs, and publishes
the registry atomically. A stale or invalid row does not authorize any write to
the source mesh, body, Player, or shared skeleton.

## Current certified scope and limitations

- The current real fixture is `UnderWearPanty_Female` against Female, with one
  certified LOD0/LOD0 binding. Male uses the same architecture but has no V3
  fixture certification yet.
- The final visible HUB smoke at
  `RuntimeSmoke_20260828_065156/Summary.json` proves exact-source ACF equip,
  338 continuous `Ready` visibility samples, immediate per-garment
  clearance/inflate changes from `0.0 -> 0.2 -> 0.0 cm`, Idle/walk coverage,
  and three real ACF unequip/re-equip cycles.
- That smoke is not a per-triangle zero-intersection certificate and did not
  use the retired V26 GPU readback. Full morph ranges, all LOD transitions,
  extreme animation, Chaos Cloth interaction, multi-garment scale, and runtime
  performance certification remain `PENDING`.
- Loose cloth, self-collision, and cloth-vs-cloth remain Chaos responsibilities.
- The Golden Palace/crotch area is explicitly excluded rather than solved.
- Four exact-source gameplay captures were produced, but the receipt records
  `PENDING_HUMAN_REVIEW`. Automated zero-clipping and final user visual
  acceptance are not claimed.
- HUB Character Creation remained absent for all 430 observations in the final
  smoke. Story Selection automatic Character Creation behavior was outside the
  HUB-only run and remains `PENDING`.
- Static source metadata is in English. A final live Director UI inspection is
  `PENDING` because Unreal MCP/editor access was unavailable at evidence close.
- Native Offset and Create Shell operation/Undo/save validation remain
  `PENDING`; their implementation contract is not equivalent to an executed
  authoring test.
- Development cook, stage, Pak/IoStore, archive, and a D3D12/SM6 packaged HUB
  startup passed. The final package contains 5,096 packages and 8,089 IoStore
  chunks; its manifest contains the active V3 registry and `6C87...` binding,
  with no legacy `F07...` binding or V26 compiled path.
