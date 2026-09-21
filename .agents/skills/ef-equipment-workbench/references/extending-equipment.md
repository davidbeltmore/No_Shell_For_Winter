# Extending equipment without rebuilding the inventory design

## New clothing or accessory

Author the ACF item's native `ItemInfo.ItemSlots` as alternatives. A garment has
no gender filter. The common ACFArmorBP parent already supplies the managed-player
veto; accessories can derive from UEFEquipmentAccessory. The legacy identifiers
`ItemSlot.Ring` and `ItemSlot.Ring2` now represent generic accessory destinations.
Use either/both as alternatives for charms, belts, ornaments or any other authored
body location. Their displayed names are Accessory 1 and Accessory 2.

Add an EquipmentRulesFragment only for additional reservations or container
policy. For a whole-body underwear item, put Underwear Legs in native ItemSlots
and Underwear Torso in its rule profile. Do not create a second inventory item.
For an ordinary shirt, add no Underwear reservation. Put visual attachment,
fitting, coverage and layers in the ClothingMorph Director, not slot rules.

Check conflicts in both directions and native-call veto. Confirm the losing
item's original GUID/count remains in the root inventory and that effects are
removed once. Re-run the native vertical test after changing transaction logic.

## New visible slot

1. Register a leaf gameplay tag; no equip destination may be an ancestor of another.
2. Add label, icon, column and ordering data to DA_EquipmentSlots. Keep UI labels English.
3. Add the tag to ACF_CharacterEquipment_WB's editable OuterwearSlots or AdditionalSlots
   defaults. The native adapter invokes the original Blueprint InitList factory,
   so it automatically reuses the ACF icon shape and input behavior. The authoring
   bootstrap initializes these arrays only when empty; it preserves later edits.
4. For the outer column, retain the five-slot viewport. Adding destinations grows
   scroll content, not the viewport. New permanent right-column entries require
   an explicit product-layout decision; the accepted initial right column has three.
5. Add class-specific migration data for moved existing items. Do not globally
   redirect Chest to underwear or rename the serialized accessory tags for labels.
6. Check empty, occupied and reserved tooltips, native drag/drop, wheel scrolling,
   GUID owner selection and full-body preview in the actual ACF menu.

## New satchel tier

Author a unique non-stackable item, native destination ItemSlot.Satchel, and a
container rule profile with capacity in original weight and reduction in [0, .5].
Test empty/exact/full capacity, fractional weights, extraction while overloaded,
carrying several bags, replacing the equipped bag, nesting rejection and filled
bag drop/save/load. Only the actually equipped bag reduces its contents.
Do not reconstruct instance-bearing contents from item classes or inventory indices.

## UI edit points

- ACF_Inventory_WB owns the item grid, equipment host, item description/comparison
  widths, currency and aggregate weight display.
- ACF_CharacterEquipment_WB owns the front renderer and both equipment columns.
  Outerwear viewport is 400 units and native icon size is 80 units. Its native
  parent binds behavior; it does not overwrite Designer sizing or colors at runtime.
- ACF_EquipmentIcon_WB owns global slot shape, button, image and focus styling.
- ACF_ItemButtonInterface_WB retains shared selection/item/index contracts and
  supplies the native drag adapter. Its icon child remains in the original hierarchy.
- WBP_SatchelWindow / WBP_SatchelContents / WBP_SatchelTransfer under the equipment
  Global folder expose the bag dialog, container lists and transfer rows.

Do not restore the rejected gray equipment text-grid panel. Designer assets,
registry icons and item thumbnails are editable independently. Updating native
transaction code must not regenerate their visual trees.

## Persistence and scale

Keep BaseCarryCapacity separate from the native root admission limit. Restore
derived reservations from actual native equipment after resolving conflicts.
Container serialization belongs to the bag's GUID and fragment payload, independent
of the visual actor. Upgrade versioned payloads only after integrity validation;
publish converted saves only after an exact backup and conservation checks.

Consult Docs/Migration/EFEquipment.md for what is implemented and verified.
These extension contracts do not certify pending save migration, commerce,
travel/death or packaged-game gates.
