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

### World pickup and prototype integration

- World authoring 01 stopped when inspection found an unconditional native Blueprint
  interaction continuation. World authoring 02 PASS: the project-owned copy retains
  its cue and interaction behavior, gated by `WasPickupCompleted`. Original ACFU
  content is unchanged. Evidence: `Saved/EquipmentQA/author_world_02.log` and
  `author_world_actors.json`; native graph inspection in `native_world_pickup_graph.txt`.
- The four prototype items now reference project-owned visual actor Blueprints and
  a simple world mesh. These remain temporary functional art; body/drop visual
  assessment is PENDING.
- NativeEquipment06/07 FAIL exposed a circular startup dependency when the native
  hard WorldItemClass config pointed to the project subclass. PreDefault did not
  resolve it. The original hard setting is retained; the game-world subsystem now
  resolves `UEFEquipmentSettings::WorldPickupClass` after class registration.
- NativeEquipment08 reached world execution but FAIL because the minimal test world
  had not registered its player controller. The fixture now performs the registration
  normally supplied by actor initialization and asserts native controller lookup.
- BuildEditor38 and NativeEquipment09 PASS. Full-bag drop/rejected admission/accepted
  pickup preserve bag and child GUIDs/counts; partial stack drops record their new
  GUID and conserve all eight units through pickup. Only one bag world entry is
  emitted. Evidence: `Saved/EquipmentQA/BuildEditor38/build.log` and
  `Saved/EquipmentQA/NativeAutomation_NativeEquipment09/Report/index.json`.
- PIE_15 PASS for the existing native menu and Store/Extract round trip after transfer
  receipts and commerce recovery changes. Screenshot: `PIE_15/satchel_after_receipts.png`.
  PIE_16 PASS adds the real player/Blueprint world-interaction path, followed by
  native wheel/drag/drop and Store/Extract regression. Screenshot:
  `Saved/EquipmentQA/PIE_16/satchel_world_round_trip.png`; assertions in `result.json`.
- Protected comparison PASS: all 5,260 baseline files unchanged, artifact
  `Saved/EquipmentQA/protected_world_result.json`.
- Rejected UI retirement PASS: 17 exact untracked task-created gray panel/slot
  packages were backed up and deleted through the editor after reference checks.
  Receipt and hashes: `Saved/EquipmentQA/RejectedUIRetirement01/receipt.json`.
  Original native widgets and user HUB changes are retained.

### Container load isolation and consumer audit

- BuildEditor39 and NativeEquipment10 PASS. Deserialization now stages contents
  before replacing live entries. A valid-checksum payload containing an equipped
  child is rejected while preserving the original live item object, GUID and count.
  Unsupported container schema versions cannot overwrite saved bytes or bypass
  validation through the runtime cache. Envelope size includes its 12-byte header.
- Evidence: `Saved/EquipmentQA/NativeAutomation_NativeEquipment10/Report/index.json`.
  World drop/Blueprint integration is checkpoint `61c80c9`.
- Read-only integration audit PASS: 16 project Blueprint candidates, exact native
  call owners/pins and node GUIDs in `Saved/EquipmentQA/IntegrationAudit/blueprint_calls.txt`.
  Save/load consumers include the shared save/load pages, ACFSaveGameActorBP,
  the Ultimate controller's test slot and the ATS traveled-player state. Storage
  widgets still call native movement and need aggregate admission adapters.
  This audit identifies integration work; it does not certify those paths yet.

### Versioned ALS record and file-publication foundation

- Container isolation/audit checkpoint: `1120016`.
- BuildEditor41/NativeEquipment11 PASS for a separate fractional BaseCarryCapacity,
  explicit saved equip intent, staged native root records, deterministic old-slot
  migration, conflict displacement, idempotent conversion and unsupported-version
  rejection. Overburdened root load reconstructs the actual equipped bag, Bikini
  reservations, and each contained GUID/count/class.
- BuildEditor40 first exposed a non-exported native FALSObjectData value constructor;
  the adapter uses its public fields and default constructor without patching ACF.
- BuildEditor42/NativeEquipment12 PASS also exercises a real isolated `.sav` fixture:
  exact original-byte backup, failure after staging without altering the original,
  validated publication, disk reload as UEFEquipmentSaveGame, stale-backup rejection,
  and verified recovery of the original native file. Test primary slots are removed;
  their immutable backups remain under `Saved/SaveGames/EFEquipmentBackups/EquipmentQA_*`.
- File publication verifies the active backend maps to the expected local path.
  Windows replacement uses ReplaceFileW; generic IFileManager replacement deletes
  the old target first and is unsuitable for this operation. Other save backends
  require a dedicated adapter. No universal atomicity claim is made.
- These are tested persistence building blocks. Menu save/load, native ALS callbacks,
  travel/death and packaged integration remain PENDING; the new slot format is not
  activated in the user-facing save flow yet. Evidence:
  `Saved/EquipmentQA/NativeAutomation_NativeEquipment12/Report/index.json`.

### ALS gameplay integration and actual level reload

- Foundation checkpoint: `947bf15`. BuildEditor45 and PIE_17 PASS for real local
  player save/load while overburdened, fractional capacity, full satchel contents
  and derived reservations. BuildEditor43/44 exposed and corrected TObjectPtr
  deduction and the editor module's explicit AscentSaveSystem link dependency.
- BuildEditor47 PASS. BuildEditor46 first rejected a TObjectPtr graph-node loop;
  corrected without modifying native plugins. `author_save_01.log` and
  `author_save_integration.json` PASS: seven native calls routed in five exact
  project Blueprints; two existing save/load UI callbacks now guard failed results.
  Save-page failures no longer run the original unconditional success popup.
- `UEFEquipmentSaveSubsystem` snapshots native ALS records on the game thread,
  verifies every saved inventory with satchels, publishes player/world slots and
  native metadata with backups and recovery, then acknowledges success. Native ALS
  still reloads the level and its actor/component records. The adapter restores
  equipment once native component callbacks finish. It keeps current-slot identity
  in its own subsystem because the native currentSaveSlot has no public setter.
- `AEFEquipmentWorldItem::GetComponentsToSave_Implementation` explicitly includes
  native storage. World inventory round trips verify GUID/count/class and saved
  fragment state before publication; missing container inventory contracts reject
  the save. Native NPC behavior and protected Player asset remain unchanged.
- PIE_18 PASS: 68 checks, actual HUB reopen with a full dropped satchel, exactly one
  restored pickup and identical bag/child GUIDs, player overload and fractional
  capacity, restored Bikini reservation, mouse-wheel/drag/drop/Store/Extract
  regression. Evidence: `Saved/EquipmentQA/PIE_18/result.json`, `editor.log`,
  `satchel_after_world_reload.png`. Main layout width evidence remains PIE_12.
- QA isolation finding: PIE_17's existing ProjectIntimacy subsystem changed
  `ProjectIntimacy.sav` on shutdown; its hash-only preflight cannot restore earlier
  bytes. ALS test slots and metadata were isolated. The launcher now copies the
  seven exact existing user save files before starting, waits for editor exit,
  preserves changed QA outputs, restores originals and verifies hashes. PIE_18
  recovery PASS in `user_save_after.json`. No claim that PIE_17 left every auxiliary
  save unchanged is made.
- Protected task comparison PASS: all 5,260 files unchanged, including native
  ACFU/Daz, Player and bodies; `protected_save_integration_result.json`.
- Actual save-button interaction, legacy UI migration explanation, existing floor
  travel/death, vendor/companion paths, Game/cook/package gates remain PENDING.

### Container travel, storage and vendor purchase

- Save integration checkpoint: `8fdfa28`. BuildEditor48/49/50/51 PASS during travel
  integration. The new portable test initially failed on fixture capacity and
  class-default equipment requirements, then on native fragment ownership. The
  fixture now uses authored Satchel 3/Bikini classes and the native admission
  fragment lifecycle. Failures remain recorded in EquipmentTravel01/02/03.
- Native travel EquipmentTravel04 PASS, all three exact tests, zero warnings or
  errors. Source world and items are destroyed and garbage-collected; destination
  restores eight unique items with saved instance data inside a full bag, all GUIDs,
  Bikini reservations, fractional base capacity and legitimate overload. Existing
  same-world rollback and portable companion regressions also PASS. Evidence:
  `Saved/Migration/CalystoDungeonDirectorV7/Native_EquipmentTravel04/Report/index.json`.
- `ProjectCalystoGameplaySnapshot` and `ProjectRunCompanionSubsystem` adapt the
  existing player inventory transport; dungeon generation is not changed. Profiles
  are SaveGame state, realized native equipment order is retained, and bounded byte
  payloads are distinct from general reflected-array limits. Player overload is
  valid; unextended companion capacity checks remain native.
- Container envelope v2 excludes only the native redundant world-local Item cache
  from tagged properties. The native ACF composition/item serializers remain in
  use, and v1 envelopes remain readable. This prevents obsolete object lookups and
  byte drift after world reconstruction without weakening canonical equality.
  Player/world save candidate records use the same archive adapter.
- BuildEditor52 FAIL identified a missing editor CraftingSystem link dependency;
  BuildEditor53 PASS after adding that explicit dependency. NativeEquipment14 PASS
  for both exact suites, including full-bag purchase cancellation, stale price,
  aggregate weight, injected settlement recovery, one-time currency transfer,
  storage round trips and filled-bag companion refusal. Evidence:
  `Saved/EquipmentQA/NativeAutomation_NativeEquipment14/Report/index.json`.
- Storage/trade Blueprint authoring PASS: seven calls in five existing widgets,
  retaining layout and pins; `author_storage_trade_01.log` and
  `author_storage_trade.json`. Actual UI confirmation remains PENDING. Task
  protected comparison PASS for all 5,260 baseline files in
  `protected_travel_storage_result.json`; separate Calysto asset comparison is
  recorded in `calysto_protected_posttravel.json`. Historical Phase0 invariants
  already differed before this work (`historical_protected_pretravel.json`);
  those pre-existing changes were not reverted or rebaselined.
- Real floor traversal, death, final Game/cook/package remain PENDING.
