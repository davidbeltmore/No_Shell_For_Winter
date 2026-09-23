# ACF storage rework

Target: NoShellForWinter, UE 5.8 / installed ACFU 4.4.2. Fresh-save validation;
existing player saves are not migrated or deleted. Starting commit: c3c0521.

## Context and UI correction

`UEFEquipmentStoragePage` identifies deposit versus bag operations. The existing
ACF buy/sell panels receive their owning widget context. Deposit actions use
GUID-preserving storage transfers; bag actions use the equipment coordinator;
merchant actions use native prices and the verified settlement backend without
the provisional confirmation window. Full bags are rejected before settlement.
Failures use native ACF notifications in English.

`WBP_EquipmentBag` is a project-owned copy of `ACF_Deposit_WB`, with its native
panels and navigation plus bag selection and effective/raw weight information.
The original inventory layout only changes its bag action label and target.
`BuildInventoryUI` no longer generates the discarded textual bag/sale widgets.

Evidence (local artifacts under `Saved/EquipmentQA`):

| Gate | Result | Evidence |
|---|---|---|
| Editor build | PASS | `StorageRework/build13.log` |
| Native widget compile/publication | PASS | `StorageRework/author_ui.json`, `author-ui05.log` |
| Deposit opened through native ACF interaction; GUID round trip, unchanged currency; bag transfer/full sale rejection | PASS, 12 assertions | `StorageContext04/result.json`, `editor.log` |
| Native equipment and policy suites | PASS, 2 tests, no test warnings | `NativeAutomation_Storage01/Report/index.json` |
| Protected ACFU/Daz/Player/body/Frederick comparison | PASS, 7,642 files unchanged | `StorageRework/protected_phase1.json` |
| Bag visual review | PASS, user accepted 2026-09-23 | `StorageUI06/bag-slate.png`, native MCP Slate capture |
| Starter bag, repeated refresh, hidden duplicate deposit titles | PASS | Individual assertions in `StorageUI06/result.json` and `StorageFull03/result.json` |
| Native mouse/keyboard actions | PENDING | Entry-point assertions do not certify every input path |
| Final Game build, cook and packaged run | PENDING | Must run on the final tree |

Earlier fixture attempts remain recorded: StorageContext01 used an unavailable
Python spawn function; 02 bypassed native interaction setup and logged an unset
VendorComp; 03 selected the locked rather than unlocked fixture. StorageContext04
corrects those fixture issues and has no Blueprint runtime errors in the storage
widgets. A passing transfer fixture is not a complete UI or release gate.

The approved bag page keeps the native full-screen canvas, tabs and footer.
Compact metadata is inside its black body; no technical GUID or persistent
sale-error strip is displayed. Nested storage panel titles are collapsed in
both deposit and bag contexts. A full black backing prevents HUD bleed-through.
`BP_StarterBag` (Travel Bag) reuses the 10 kg/10% profile. Local player possession
initializes equipment before opening any menu; fresh-game initialization grants
one unequipped bag, while restoration stays authoritative. A session guard and
existing-bag check prevent repeated grants.

StorageUI06's later logic continuation was interrupted by a PIE restart and
invalidated its widget reference; its visual and preceding assertions remain
separate evidence, not an overall test pass. StorageFull03 exposed stale fixture
references after editor property setters reran chest construction. The corrected
fixture resolves SCS components again after configuration; full chest gates are
still PENDING until that run completes.

## Native chest family (in progress)

The project children `BP_EquipmentDeposit` and `BP_EquipmentLockedChest` inherit
the installed native deposit. `UProjectChestInventoryComponent` replaces its
vendor storage subobject with a compatible native subclass. The locked child
composes the existing lockpick component and gates both interaction paths.
Native deposit continuation is local; it is replayed once after unlocking.

The HUB replacement preserves the saved user map, transforms, tags, folders,
manual stock and applicable lock settings. `StorageRework/replacement.json`
records the exact three actor substitutions. Original disk/live HUB snapshots
and the two Director catalog backups are retained outside Content. The two
catalogs only remap old chest class paths; Director authoring validation passes.
Generation, persistence, legacy retirement and final chest gates remain PENDING.

Pre-existing dirty travel/save work is retained separately. It is not silently
included in this subsystem's commits or claimed as validated by these gates.
