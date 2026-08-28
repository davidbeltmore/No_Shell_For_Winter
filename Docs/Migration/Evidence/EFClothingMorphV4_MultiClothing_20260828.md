# EF Clothing Morph V4 Multi-Clothing Evidence — 2026-08-28

## Scope

- Target: `D:/Projects UE5/NoShellForWinter` on Unreal Engine 5.8.2.
- Source project was not opened or modified.
- Project-owned implementation: `Plugins/EFClothingMorph`.
- Real fixtures:
  - `/Game/DazToUnreal/UnderWearBra/UnderWearBra`
  - `/Game/DazToUnreal/UnderWearPanty/UnderWearPanty`
  - `/Game/DazToUnreal/Female/Female`
- Stable Director path: `/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`.
- Fixture checkpoint before V4 code: commit `cf424f8` on `feature/efclothing-v4-multicloth`.

## Root cause repaired

V3 validated and refreshed the complete catalog as one transaction. An enabled draft with an empty `GarmentId` made global policy validation fail, so a newly added Bra entry could leave the previously valid Panty in visible passthrough. The row action also resolved its ID and then discarded it, rebuilding the full catalog. The registry's runtime cache noticed only array-count changes, so a one-for-one binding replacement could remain stale.

V4 makes identity, compilation, publication and runtime state independent per clothing entry:

- The public Director displays one English `Clothes` array.
- `Clothing Name` is editable and is generated only when missing after both meshes are assigned.
- Manual names are preserved; collisions receive deterministic `_2`, `_3` suffixes.
- Disabled or incomplete entries are harmless drafts.
- Runtime binding lookup uses `Clothing Name + Clothing Mesh + Body Mesh`.
- Cache invalidation includes binding identity, build GUID and source/body paths.
- `Update This Clothing` refreshes only the selected entry.
- `Refresh All` publishes successful rows without removing unrelated valid bindings.
- Strict cook/package certification still requires every enabled entry to be valid and fresh.
- Each equipped `USkeletalMeshComponent` owns its own producer, offsets, state and restoration snapshot.

No generated Skeletal Mesh is used. Both fixtures render their exact original source meshes; generated V4 assets contain immutable surface correspondence only.

## Director and compiler

- Director migration receipt: `Saved/ClothingMorphV4QA/director_migration_20260828_152122.json`.
  - Schema: 5.
  - Director ID: `EFClothingMorphV4`.
  - Clothes: `UnderWearBra_Female`, `UnderWearPanty_Female`.
  - Bra name generated automatically; Panty name preserved.
- Final strict compiler receipt: `Saved/ClothingMorphV4QA/compiler_receipt_20260828_154653.json`.
  - Compiler: 28.
  - Binding schema: 8.
  - Enabled/valid/published/native bindings: `2/2/2/2`.
  - Generated profiles: 0.
  - Reused fresh bindings: 2.
  - Draft/invalid/stale rows: `0/0/0`.
  - Protected inputs unchanged: PASS.

## Builds and package

- `NoShellForWinterEditor Win64 Development`: PASS.
- Daz editor receipt repair: PASS.
- `NoShellForWinter Win64 Development`: PASS.
- Daz game receipt repair: PASS.
- Blueprint compile without saving: PASS for Player, Bra and Panty.
  - Receipt: `Saved/ClothingMorphV4QA/BlueprintCompile_20260828.json`.
- Fresh Development cook/package: PASS.
  - Archive: `Saved/Migration/CalystoDungeonDirectorV4/Packages/Development_20260828_154724`.
  - Packaging contract: `CalystoV4PackagingContract.json` inside that archive.
- Packaged D3D12/SM6 HUB startup: PASS.
  - Receipt: `Saved/ClothingMorphV4QA/PackagedStartup_20260828_155252/Summary.json`.

## Real visible PIE QA

Final passing run:

`Saved/ClothingMorphV4QA/RuntimeSmoke_20260828_153841`

Results:

- `UE58_EF_CLOTHING_MORPH_V4_RUNTIME_SMOKE_PASS`.
- Real ACF pickup and equipment route used; no direct mesh assignment.
- Bra and Panty equipped simultaneously.
- Final runtime: `managed=2 ready=2 warming=0 passthrough=0 issues=0`.
- Exact source mesh retained for both clothes; no fitted mesh and no `EF_AutoFit` mesh swap.
- Three real unequip/re-equip cycles per clothing entry.
- Per-component Skin Gap and Surface Volume sequence `0 -> 0.2 -> 0 cm`: PASS.
- Cross-clothing offset isolation: PASS.
- Earlier clothing remained visible and Ready while the later clothing was tested: PASS.
- HUB Character Creation absent on every observed PIE tick: PASS.
- Eight 1600x900 gameplay screenshots recorded: front, back, inferior and moving right view for each fixture while both clothes were active.
- Protected hashes and worktree state unchanged during the run: PASS.
- Manual visual review by the user during the real gameplay run: PASS; user reported that the combined result looked perfect.

The first QA attempt exposed an ACF reflection limitation where acquired inventory structs omitted `item_class`; the harness was corrected to preserve the real pickup's GUID-to-class evidence. A later run reached the final Panty cycle but exceeded an undersized 660-second controller limit. Neither event was a clothing runtime failure. The final run completed all gates.

## Protected invariants

The final compiler receipt hashes and confirms byte-identical before/after state for:

- Player
- Female
- Male
- Multiple
- shared `SK_UEFN_Mannequin` skeleton
- original UnderWearBra
- original UnderWearPanty
- Director during compilation

DazToUnreal, ACF Ultimate, Engine/Marketplace plugins, body meshes, shared skeleton, morph targets and skin weights were not edited by V4.

## Acceptance boundary

This evidence certifies the two current DAZ Female clothing fixtures and their real LOD0 bindings, simultaneous runtime operation, live per-component controls, lifecycle restoration, visible gameplay, cook and packaged startup. New clothes remain data-driven but require an entry and a successful binding update before strict packaging. V4 deliberately leaves an invalid new entry local instead of disabling already valid clothes.
