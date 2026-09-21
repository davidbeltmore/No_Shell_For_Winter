# Layered equipment and satchels

## Execution baseline

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.2, ACF Ultimate 4.3.5.
Starting commit: `6a72c7fbdd1e00f2f51beb450d7456e2fd10b480`.
The initial tracked working tree was clean. Live MCP at `http://127.0.0.1:8000/mcp`
confirmed HUB, no PIE, and an unsaved HUB map (preserve before restart).
Protected task baseline: `Saved/EquipmentQA/Baseline/protected_before.json`,
5,260 files. Source project, Engine, Marketplace, Player and body assets are protected.

## Accepted behavior

Player only; single-player certification. All labels and authoring text are English.
Gender never restricts equipment. Female and Male are the current fit targets;
Non-binary creator changes and NPC fitting are outside this delivery.
Outerwear has existing slots plus Necklace, paired Upper Arms/Forearms/Gloves/Feet,
two generic accessory destinations and Satchel. Underwear has Torso and Legs. Conflicts automatically
unequip whole items without deleting them or remembering automatic restoration.
Bikini owns both underwear slots. RagShirt/Bra and RagPants/Panty may coexist.

One equipped satchel reduces only its own contents. Initial capacity/reduction
pairs are 10/10%, 20/20%, 30/30%, with a 50% reduction ceiling. No nested bags.
Contents must be extracted before use. Unequip/extraction may overburden without
new movement penalties; external weight-increasing additions must fit. Drop and
confirmed sale include the contents, respecting each item's restrictions.

## Ownership

EFEquipment Runtime owns slots, rule profiles, transactions, ACF container storage,
effective weight and UI models. EFEquipment Editor owns authoring and validation.
EFProjectSystems integrates lifecycle, travel, persistence and existing UI.
EFClothingMorph's Director remains the only garment-fit authoring database.
Reservations are derived GUID references, never synthetic ACF inventory entries.

## Gates

| Phase | Status | Required evidence |
|---|---|---|
| Baseline and consumers | IN_PROGRESS | MCP context, protected hashes, callsite audit |
| Vertical equipment/container slice | PENDING | Native contracts and actual ACF PIE operations |
| Slots and migration | PENDING | Authoring receipt, legacy save preservation |
| Fit and asynchronous loading | PENDING | Both bodies, movement, morphs, coverage, latency |
| Editable UI and maintenance | PENDING | WBP compile, actual UI screenshots, skill recipes |
| Release validation | PENDING | Editor/Game builds, cook, packaged execution |

The previous Game build failed in Calysto metadata/ClassGeneratedBy consumers.
Do not promote historical Editor/PIE evidence to current-tree release acceptance.
Each phase must record its source snapshot, artifacts and protected comparison.

## Current evidence (2026-09-21, uncommitted work snapshot)

- Editor build, runtime and authoring modules: PASS in `Saved/EquipmentQA/build_editor_09.log`.
- Authored registry, six garment destinations, armor parent, reusable rules and
  three satchels: PASS creation/compile/save in `Saved/EquipmentQA/author_foundation.json`.
- Actual ACF component automation: PASS, `EF.Equipment.Native.Vertical`, report
  `Saved/Migration/EFClothingMorphV51/NativeAutomation_EquipmentVertical01/Report/index.json`.
  Covers both directions of conflicts, whole Bikini displacement, direct-native
  veto, failures after displacement and native effects, recovery, stale previews,
  fractional capacity, live GUID transfers, no nesting, overloaded extraction and
  native container serialization. This is a transient native world test, not PIE
  or visual acceptance. Subsequent changes require the relevant tests again.
- Protected baseline comparison: PASS, 5,260 hashes unchanged, artifact
  `Saved/EquipmentQA/protected_foundation_result.txt` and
  `Saved/ClothingUnisexQA/protected_equipment_foundation.json`.
- UI consumer inventory: `Saved/EquipmentQA/blueprint_consumers.txt`.
- The modified HUB package contains pre-existing unsaved user work preserved
  before closing the editor. It must be excluded from equipment commits.
- A separate compile prerequisite qualified three `SurfaceLimit` calls in
  `EFCalystoDecalReservation.cpp` to resolve a unity-build ambiguity. No dungeon
  feature or behavior is part of this equipment change.

PIE, visual QA, migration backups, commerce, travel/death, Game build, cook and
packaged execution remain PENDING. The feature is not release-accepted.

## Native inventory presentation correction

The user rejected the initial gray text/button equipment panel shown in PIE_04.
That screenshot is a failed visual gate, irrespective of the earlier widget-count
assertions. The original ACF inventory, character equipment, equipment icon,
list inventory and weight assets were restored from HEAD before applying narrow
adapters again. The pre-existing HUB user changes were not reverted.

The revised contract is ten exterior icon slots: Head, Torso, Legs, Feet,
Necklace, Upper Arms, Forearms, Gloves, Accessory 1 and Accessory 2. Five native
80-unit slots fit the 400-unit scroll viewport; mouse wheel scrolling is local
to this column. The fixed right column contains Underwear Torso, Underwear Legs
and Satchel. Original weapons, consumables and character renderer are retained.
Serialized ItemSlot.Ring / Ring2 identifiers remain compatible; their displayed
meaning is generic accessories with no body-region restriction.

Editor build 18 passed (`Saved/EquipmentQA/build_editor_18.log`). Native UI
authoring completed in `author_ui_native_01.log`; a handled UMG variable-GUID
ensure exposed a missing precompile structural notification, corrected in source
for the next build. Visual, wheel input, drag/drop and full-body framing remain
PENDING until explicitly checked in the real menu.

- User approved the restored ACF presentation, then requested a wider character
  preview and narrower item card. Applied 420x600 preview, 660-unit equipment
  host and 240-unit description/comparison hosts in NativeUIAuthor04.
- Editor build 25: PASS (`Saved/EquipmentQA/build_editor_25.log`).
- PIE_08: ten native outer icons, three fixed icons, five-slot viewport and wheel
  down/up checks PASS. Drag/drop FAIL; no input acceptance inferred from appearance.
- PIE_07 crashed in the editor-only synthetic input helper: a layout-only
  FWidgetPath lacked virtual pointer entries. The helper now constructs the
  complete pointer path; PIE_08 wheel events ran successfully.
- NativeUIAuthor04 published a complete PASS receipt after package saves, but
  emitted handled widget-GUID ensures. The authoring helper now explicitly tracks
  added/removed widget GUIDs; another clean authoring run remains required.
- Protected comparison PASS again: 5,260 unchanged hashes, artifact
  `Saved/EquipmentQA/protected_native_ui_result.txt`.
- Maintenance skill version 2 includes command and extension references plus
  reusable Workbench and protected-baseline helpers. Skill/schema and PowerShell
  syntax validation PASS. AuthorUI and PIE workbench commands have been exercised;
  remaining command actions require their own current-tree execution evidence.

### Width correction and input evidence

- Editor build 27 PASS: `Saved/EquipmentQA/build_editor_27.log`.
- NativeUIAuthor05 PASS receipt, no Blueprint compile errors or widget-GUID
  handled ensures in its preserved `UnrealEditor.log`.
- PIE_12 PASS: 25 checks, real native Slate wheel down/up and Bra drag/drop
  displacing the complete Bikini. Visual evidence:
  `Saved/EquipmentQA/PIE_12/native_inventory_wider.png`. The original front
  full-body renderer has a wider allocation; item and comparison cards are
  narrower. Root Fill proportions are Inventory .50 / Equipment .36 / ItemInfo .14.
- Protected comparison after PIE_12 PASS: all 5,260 baseline files unchanged.
- NativeEquipment02 engine report: both `EF.Equipment` tests succeeded, no
  warnings. The reused clothing runner rejected the suite because it requires
  exactly one test. Added an equipment-specific runner; its wrapper gate requires
  a fresh execution. Historical report remains at
  `Saved/Migration/EFClothingMorphV51/NativeAutomation_NativeEquipment02/Report/index.json`.
- PIE_13 FAIL at synthetic Contents click; runtime opening/transfer UI remains
  PENDING. The fixture now routes hover before button down, matching SButton's
  click contract; build and another PIE run are required.
- Editor build 28 PASS: `Saved/EquipmentQA/BuildEditor28/build.log`.
- PIE_14 PASS: Contents click, real Store/Extract button clicks, original GUID
  ownership and effective-weight round trip. Screenshot
  `Saved/EquipmentQA/PIE_14/satchel_round_trip.png`; exact assertions in `result.json`.
  This resolves PIE_13's missing hover in the input fixture.
- NativeEquipment03 wrapper and both engine tests PASS:
  `Saved/EquipmentQA/NativeAutomation_NativeEquipment03/Report/index.json`.
- Compile prerequisite committed separately as `c8dac94`; equipment foundation,
  initial data and native adapters committed as `4d64bff`. These are development
  checkpoints, not release acceptance. The preserved HUB change is excluded.

### Transfer receipts and recovery

- BuildEditor29 and NativeEquipment04 PASS. Native vertical assertions now also
  cover corrupted-payload rejection without discarding live contents, split and
  partial-extraction GUID receipts, quantity conservation, a late persistence
  failure with rollback, and ACF's removal-before-veto alternative-slot behavior.
  Evidence: `Saved/EquipmentQA/BuildEditor29/build.log` and
  `Saved/EquipmentQA/NativeAutomation_NativeEquipment04/Report/index.json`.
- Adapter commit verification occurs before inventory notifications. Whole items
  keep their GUID; stateless splits get a new destination GUID. No implicit stack
  merging is performed by this transfer path. Stateful stacks require a whole
  transfer until a fragment-specific split contract exists.
- Protected comparison PASS: `Saved/EquipmentQA/protected_transfer_result.json`.
- Game build 01 FAIL in the two historical editor-only API consumers, with
  `Module.EFEquipmentRuntime.cpp` compiled successfully. Evidence:
  `Saved/EquipmentQA/build_game_01.log`. Game/cook/package acceptance stays FAIL/PENDING.

### Aggregate sale checks

- BuildEditor30 and NativeEquipment05 PASS. The native fixture exercises an actual
  ACF vendor, aggregate price/breakdown, cancellation, changed contents, an unsellable
  child, a non-droppable child, injected currency-settlement failure, restored
  balances/ownership, one-time confirmation, and rehosting the bag after transfer.
- Money settlement now participates in adapter verification before inventory
  notifications. Failure restores item lists and attempts verified wallet recovery;
  incomplete recovery is reported explicitly.
- Evidence: `Saved/EquipmentQA/BuildEditor30/build.log` and
  `Saved/EquipmentQA/NativeAutomation_NativeEquipment05/Report/index.json`.
  Actual vendor UI confirmation, world drop/pickup, save migration, travel/death
  and packaged checks remain PENDING.
