---
name: ef-equipment-workbench
description: Author, integrate, debug and validate NoShellForWinter layered ACF equipment, GUID reservations, satchel inventories, effective weight, equipment UI and save migration. Use when adding equipment slots, garments, exclusivity profiles or container variants, or diagnosing equip conflicts and inventory conservation.
metadata:
  version: "5"
---

# EF Equipment workbench

Use the target project `D:/Projects UE5/NoShellForWinter`. Invoke
`noshellforwinter-unreal-mcp` first and start with read-only editor context.
Read `Docs/Migration/EFEquipment.md` for current gates; this skill's presence
does not certify that pending migration, commerce or release gates have passed.
Keep player, meshes, ACFU and Daz protected as required by root `AGENTS.md`.

## Ownership and entry points

- `Plugins/EFEquipment`: native runtime, authoring and UI binding logic.
- `/Game/_Game/Data/Equipment/DA_EquipmentSlots`: equipment destinations.
- `UEFEquipmentRulesFragment::Profile`: additional reservations and satchel policy.
- `UEFEquipmentCoordinatorComponent`: serialized player operations, GUID lookup,
  derived slot views, weight projection, transfers and recovery.
- `UEFEquipmentFunctionLibrary`: existing ACF Blueprint call adapters. The
  inventory currently calls the item manager, so preserve `targetComponent`,
  `itemToUse`/`itemToDrop` and execution pins when replacing graph calls.
- `/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`: sole fitting,
  layering and coverage authoring authority. Use `ef-clothing-unisex-workbench`
  for those changes. Equipment exclusivity does not belong in the fitting layer.

Use English labels and messages. Do not add gender restrictions. NPCs and
companions retain their existing behavior; this feature is certified for the
single player only. Preserve all existing input mappings.

## New garment or exclusive piece

Read [extending-equipment.md](references/extending-equipment.md) for the concrete
data and Designer edit points when adding a garment, slot, accessory or satchel.

Set native `ItemInfo.ItemSlots` to alternative destinations. Keep simultaneous
reservations out of that array. Derive managed armor from the existing
`/Game/FullSample/Blueprints/Items/Armor/ACFArmorBP`, whose project native parent
provides the common veto. Managed accessories use `UEFEquipmentAccessory`.
Keep the native requirement chain; a fragment cannot veto native equip.

For exclusivity, create/reuse a `UEFEquipmentRuleProfile` and add a single
`UEFEquipmentRulesFragment` referencing it. For example, Bikini has primary
`ItemSlot.Underwear.Legs` and reserves `ItemSlot.Underwear.Torso`. A Torso plate
may reserve Underwear Torso while an ordinary shirt does not. Never create
phantom inventory entries for reservations.

Test both directions of displacement, including selecting/unequipping a reserved
slot. Confirm the displaced item remains in root inventory with the same GUID.
Test a direct native equip attempt and an already-equipped alternative-slot
attempt: ACF can unequip the old slot before reaching the common veto.

## New slot

Register a leaf gameplay tag and add label, icon, column and order to the registry.
No equipped destination may be an ancestor of another. Accessory 2 retains the compatible `ItemSlot.Ring2` identifier,
not a child of `ItemSlot.Ring`. Both accessory destinations accept any authored
body location; do not label them Rings or add anatomical restrictions.
Do not reinstate `ItemSlot.Armor` as a player destination. Add class-specific
migration rules when changing existing destinations; never globally redirect Chest.

Reuse `ACF_EquipmentIcon_WB` as the global slot visual edit point and the existing
`ACF_CharacterEquipment_WB` as the layout edit point. Keep the original character
renderer, item grid, weapons and consumables. Outerwear has ten icons in a
400-unit viewport (five native 80-unit slots visible), with mouse wheel scrolling.
The fixed right column has Underwear Torso, Underwear Legs and Satchel.
Keep manual Designer layout/styles intact; do not restore the rejected text rows.
Dynamic instances must use unique generated names, not a fixed widget name.

## Satchel variant and transfers

Use a unique, non-stackable ACF item at `ItemSlot.Satchel`, with a container rule
profile. Capacity uses original content weight. Reduction affects that content
only while the bag is actually equipped and must be in [0, 0.5]. Initial variants
are 10/10%, 20/20%, 30/30%. Bags cannot nest.

Contents live in `UEFContainerInventoryComponent`, linked to the bag GUID. Use
coordinator transfer APIs; native class-only movement can split/merge stacks and
discard instance state. Never persist inventory indices. Extract before use,
crafting or equipping. Unequip and extraction can overload; further admissions
that increase weight must fit. Do not introduce movement penalties.

Base carry capacity and effective weight are public gameplay values. Native root
capacity is an internal projection and may be negative while overburdened. Access
the native float through the validated adapter, not the truncating integer setter.
Do not use the projected native limit as the UI's total carry capacity.

## Persistence, trade and failure handling

Use `PreviewPurchase`/`ConfirmPurchase` for vendor admissions and the corresponding
sale APIs for outgoing sets. Quotes are single-use and revalidate contents, price,
wallets and aggregate weight. Storage widgets route through `MoveItemsFromInventory`
in the project function library: incoming player transfers use `TakeFromInventory`,
outgoing transfers use `GiveToInventory`. Filled bags are refused for pawn recipients;
ordinary world storage keeps the bag and all contained identities together.

The native travel snapshot preserves BaseCarryCapacity separately from projected
ACF capacity and restores actual equipment order through the coordinator. Do not
reintroduce raw-weight rejection for a legitimately overloaded player. Companion
capacity checks retain their native behavior. Test source-world destruction and GC,
not only a save/load within the same world.

Container envelope v2 omits ACF's redundant cached `FInventoryItem.Item` pointer;
native composition and item/fragment serialization remain authoritative. Read v1
through the same tagged-property skip, retaining GUID/count/state. Root and world
inventory candidate records use `FEFEquipmentSaveArchive` for the same reason.
Never remove arbitrary object references or bypass canonical equality to make a
travel test pass. Keep the 16 MiB aggregate container/travel limits.

The five existing project ALS consumers now route through `UEFEquipmentSaveLibrary`.
Use `UEFEquipmentSaveSubsystem` for current-slot identity and verified publication;
the native private current-slot string has no setter. `Author-SaveIntegration.py`
routes exact native call nodes without replacing the existing save pages. Re-run
it after intentional changes to those consumers; it is idempotent. Failure callbacks
must retain the page, report the actual error and never show the success popup.

`Launch-InventoryPIE.ps1` now waits for its editor, backs up each existing top-level
user `.sav`, preserves changed QA output and restores the exact pre-run bytes after
exit. This also covers independent game subsystems that save during PIE teardown.
Do not infer that an isolated ALS metadata slot isolates those other subsystems.
On interrupted QA, retain `UserSaveBackup` and verify its recovery receipt.

Keep ACF object/fragment serialization inside a versioned envelope. Validate before
publishing converted data and retain an exact old-save backup. Preserve GUIDs,
counts, classes and asset paths. Derive reservations after restore; don't serialize
them as truth. Resolve exclusive items first, then slot order and GUID. Displaced
items remain unequipped. Legitimately overloaded saves are valid.

Trade/drop acts on the bag plus its contents. Check every contained restriction,
use native vendor prices, show the breakdown, revalidate confirmation and consume
the quote once. Cancellation must change neither money nor ownership. Death and
travel carry contents with the bag; don't also emit separate content loot. Filled
bag transfer to companions requires an explicit unsupported-operation message.

Recovery is best effort. Test failures after displacement and after native effects.
Never promise full transaction atomicity. On failed recovery retain objects and
clear inconsistent effects/reservations with an English error message.

## Tools and QA

Read [commands-and-evidence.md](references/commands-and-evidence.md) when building,
authoring, capturing the menu or running the input fixtures. It contains the
reusable workbench entry point, fresh-baseline recipe and engine-specific API
details learned during implementation. Keep this reference and executable helpers
updated when an integration contract changes; do not preserve failed commands as
recommended recipes.

Use the protected Editor/Game build and Editor launch wrappers under
`Tools/Migration`; never leave receipts with Daz disabled. Author only exact
task-owned packages. `Tools/Equipment/Author-Foundation.py` is a bootstrap recipe,
not a general refresh command: review its defaults before running over authored
content. `Author-InventoryUI.py` preserves existing Designer trees.

Native automation lives under `EF.Equipment`. `Validate-InventoryPIE.py` opens
the real ACF menu with the real HUB player and retains it for visual review.
Use a fresh run directory via `Launch-InventoryPIE.ps1 -RunName <unique-name>`.
It writes no assets or save slots. Supply its visual completion marker only after
actually inspecting the UI. Keep logs, JSON and screenshots in `Saved/EquipmentQA`
and record the snapshot/commit and gate result in `Docs/Migration/EFEquipment.md`.

Validate both bodies, extreme morphs, movement, genital coverage, rapid changes
and late callbacks using the existing bridge without ForceReconcile. Measure
Ready latency at ordinary frequency and 30 FPS; compare against 0.5 seconds only
for comparable warmed scenarios. Record packaged cold loads separately.

After each subsystem, compile, run relevant tests, compare protected hashes and
make a small commit. Complete Editor/Game, Blueprint, PIE, visual, cook and packaged
gates on the final tree before release acceptance. Unexecuted checks stay PENDING.
