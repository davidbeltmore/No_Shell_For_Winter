# Generator-independent procedural chests

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.3. Starting HEAD:
`1114c8435e8a767e6c193c3eaccfb22de61eada3`. Chest implementation and release
validation are complete. Historical invariant drift and the real Calysto
population coverage limitation remain explicitly recorded below.
The reference project `D:/Projects UE5/LustAsDeadlySin` was not opened or written.

## Ownership and editor entry points

For a chest placed in a level, select the actor itself in the Outliner and use
**Details > Procedural Chest > Procedural Mode**. Profile, Chest Rarity,
Override Content and Seed appear in the same section. Stable Identity is under
the advanced disclosure. These are the existing component properties, exposed
by the project editor module through native property handles; there is no second
configuration or Construction Script synchronization. The locked child inherits
the same actor Details layout. Validate Configuration also appears here.

Open `/Game/_Game/Items/Chests/BP_EquipmentDeposit` or its locked child
`/Game/_Game/Items/Chests/BP_EquipmentLockedChest`. Select `ProceduralChest`
in Components, then use Details > Chest > Procedural Mode. It defaults off.
The same inherited component exposes per-instance settings on placed chests.

`StorageComponent` remains the only ACF vendor/storage inventory. Its native
parent property is typed as `UACFStorageComponent`, while the project replaces
the subobject class with `UProjectChestInventoryComponent`. The separate named
SCS authoring component makes controls discoverable without replacing the native
parent or adding another inventory. It never generates in Construction Script.

Profiles live in `/Game/_Game/Data/Equipment/Chests`: `DA_Chest_Wealth`
(tradable metal, 1–3 selections), `DA_Chest_Armament` (swords/bows, 1–2 selections,
70% Common / 30% Rare), and `DA_Chest_Ammunition` (arrows, 1–3 selections,
5–15 units each). Wealth and Ammunition use 100% Common. The project-owned
`DT_ChestCatalog` preserves the five original ACF catalog rows and adds the
existing Arrow class with `Item.Ammunition`. The original ItemsDB is unchanged.
Wealth currently means tradable items; it does not mint native wallet currency.

The old `UProjectChestProfile` and inventory flags remain a compatibility path
for existing authored rules. New content uses `UProjectChestLootProfile`.
Do not enable both authoring paths. A frozen external manifest is exclusive and
takes precedence over both, retaining the existing Calysto transition contract.

## Content contract

- Chest Rarity classifies the chest; it does not force an item rarity.
- Profile supplies a reusable content type and filters. Override Content uses
  the complete local settings instead of modifying the shared profile.
- Content Type is a data identifier. Categories, excluded categories and row
  allow/exclude lists determine eligibility; no C++ type switch exists.
- Fixed Selections or the inclusive minimum/maximum controls how many item
  selections occur. Units Per Stack controls the units in each selection.
  Native ACF may merge repeated classes or split a combined count into stacks.
- Item Rarities are exact percentages, each finite in [0,100], totaling 100
  within 0.000001. Their effective probability is the authored percentage;
  the selector does not silently normalize, remove empty tiers or substitute.
- Category filters match child tags. Rarity tags match exactly. Duplicate
  rows of one class in the same tier are deduplicated. Conflicting eligible
  rarity assignments for one class reject configuration.
  Item rarity here is the catalog row's Rarity tag. Generation does not rewrite
  item-class defaults or native UI presentation metadata.
- Without duplicates, each positive tier must support the maximum possible
  selections. This deliberately conservative rule guarantees all sampled
  sequences are feasible while preserving the advertised rarity percentages.
- Missing catalog/classes, empty positive tiers, invalid percentages, unknown
  row filters and impossible native stack bounds reject before publication.
  Zero selections is intentional empty content and still marks generation done.

Validate Configuration checks the actual catalog without touching inventory.
Preview Contents uses the same selector as runtime. Seed plus Stable Identity
reproduces classes and quantities, not newly created inventory GUIDs. Placed
instances derive the persistent identity from their editor actor GUID; runtime
spawners must supply a stable nonzero GUID. Actor labels/names and global RNG
never choose contents. Tier and candidate ordering is canonical.

## Opening and persistence

The existing Blueprint interaction gate calls PrepareInteraction. EnsureContents
also checks the lock directly. It generates only an authoritative, ungenerated,
empty inventory after unlocking. Failed lockpick attempts do not generate.
Reopening an emptied generated chest does not refill it. Changing configuration
does not clear stored items. Enabling procedural mode on a populated runtime
inventory rejects generation and retains the existing items.

The native SaveGame generated/lock flags remain authoritative. The equipment
world-storage snapshot subsystem retains bounded native serialized component
records across streaming and travel, keyed by map plus stable GUID. It has no
independent item inventory. A world save includes unloaded records in the same
`UEFEquipmentSaveGame` slot. Whole-world load replaces the session snapshot map.
Native item/fragment serialization and GUID-based equipment transfers remain
unchanged. Actor-name automatic ALS reload is disabled for this chest family;
explicit whole-world ALS restoration remains supported.

## Maintenance

Create a Project Chest Loot Profile Data Asset to add a content type. Choose the
existing ACF ItemGenerationSlot table, categories, optional exact row filters,
selection/stack bounds and percentages. Add eligible items to that table with
valid item classes and category/rarity tags. Add rarity gameplay tags through
the project tag source `Config/Tags/ChestContent.ini` (for example a new
`Rarity.*` tag), then add a matching catalog row and percentage entry to the
profile. Keep percentages totaling 100 and validate every positive tier.
No Blueprint duplication or generator change is required.

For a placed chest: select its `ProceduralChest` component, enable Procedural
Mode, choose Chest Rarity and Profile. To author a local variant, enable Override
Content and fill its complete settings (including Catalog and Item Rarities).
Use Selection Range to switch between Fixed Selections and Min/Max Selections.
Min/Max Units Per Stack are independent. Expand Chest > Reproduction for Seed
and Stable Identity. Use Validate Configuration before playing. Changing a
profile does not reset already generated contents in an existing save.

For new runtime spawners, assign the component's Profile, Seed and persistent
StableIdentity before the first interaction, preferably before FinishSpawning.
Retain that identity when recreating the same chest. Duplicate live identities
are rejected. Use a new identity only for an intentionally new chest.

After intentional Blueprint changes, run
`Tools/Equipment/Validate-ChestCookClosure.py` with the protected Editor Python
launcher. It runs the existing zero-mutation runtime Blueprint validator before
the preserved V6 closure validator. This renews current hashes only after real
class/asset validation and retains immutable creator history. Never edit receipt
hashes manually or rerun the broad chest-family bootstrap over authored overrides.

`FProjectChestResolvedEntry` in `Storage/ProjectChestContent.h` is the generic
manifest contract. The two Calysto bridges adapt to it. Do not include Calysto
or Dungeon Architect headers from the generic chest content/authoring code.
Do not call generation from construction or introduce a second inventory.
Other gameplay features still depend on EFProcedural at the module level during
the transition. This change proves chest operation in a world without a dungeon
generator; it does not remove the game's existing Calysto module dependencies.

## Current evidence

Artifacts: `Saved/EquipmentQA/ProceduralChests/`.
Initial dirty HUB was saved through native MCP; the user also confirmed saving.
`Before/HUB-disk.umap` and `Before/HUB-live-preserved.umap` preserve both states.
Existing modified chest assets and residual redirectors were retained.

| Gate | Status | Evidence |
|---|---|---|
| Initial live target, dirty packages | PASS | MCP ReadEditorContext, UE 5.8.3, HUB, no PIE |
| Initial protected snapshot | CAPTURED | protected_before.json, 7,642 files; not a comparison |
| Editor build | PASS, intermediate tree | build03.log; earlier build01/02 failures retained |
| Latest Editor build | PASS | build08.log, Daz receipt repaired; earlier compile failures retained |
| First Game build | PASS, intermediate tree | game01.log; superseded by final build |
| Final Game build | PASS | game03.log: Development, 49.57 seconds, Daz receipt repaired |
| Blueprint authoring/compile and Details visual | PASS | author.json; ProceduralChestsPIE02/details-deposit.png and details-locked-focused.png, actual images inspected |
| Initial deterministic/statistical native suites | PASS, scope before added real-profile stack tests | Native01/Report/index.json, two exact tests, zero warnings, exit 0; 25,065 rare selections / 100,000 |
| Native storage/persistence/lock gates | PASS, initial core | EF.Chests.Lifecycle.NativeStorage in Native01 |
| Final native suites | PASS | Native05/Report/index.json: three exact suites, zero errors/warnings, exit 0; real profiles, 100,000 rarity/count/stack trials and placed map identities |
| External resolved-content contract | PASS | Native05/Report/index.json: all three suites; native BeginPlay, manual-stock suppression, both procedural switches enabled, exactly-once manifest verification and emptied external chest |
| Generator-free real PIE | PASS | ProceduralChestsPIE02/result.json, 30 checks; profiles, no BeginPlay generation, exact reopening identity, empty chest, fresh session |
| Preserved HUB | PASS | identities.json: all 32 actors retain placements/tags/manual stock/lock settings; four chests retain mode off |
| Real travel and disk reload | PASS | ProceduralChestTravel01/result.json, 28 checks: source unload, save while absent, return, mutate and disk reload restore exact partial/empty contents |
| Existing transfers/satchels/real lockpicking | PASS | StorageProceduralChests02/result.json: 56 checks, real native interaction/cooldown/success/save/load; bag.png visually reviewed; seven user saves preserved |
| Final cook and package | PASS | package05.log; A:/NoShellForWinterChests/Archive_05; UAT exit 0, cook 169.12 seconds, 0 errors/45 warnings, Daz/config restoration verified |
| Packaged chest execution | PASS | Packaged_ChestsNative01: three exact suites, zero test errors/warnings, exit 0; actual cooked map generates all three profiles and retains GUIDs |
| Packaged equipment regression | PASS | Packaged_ChestsEquipment01: two exact suites, zero test errors/warnings, exit 0 |
| Packaged rendered inventory | PASS within stated scope | Packaged_ChestsVisual01: current image inspected, native save/load/drop/pickup and contained GUID checks, exit 0; stats-panel overlap noted below |
| Final live editor handoff | PASS | editor-final-context.json: correct UE 5.8.3 target, HUB, no PIE, no dirty packages; editor-final-details.png shows ProceduralChest selected and Procedural Mode off |
| Task protected comparison after cook | PASS | protected_postpackage.json: 7,642 unchanged files; calysto_postpackage.json: 13 unchanged assets |
| Historical Phase0 invariant gate | FAIL, pre-existing drift | protected_historical.json: historical ACFU/Daz asset/authoritative asset differences; task baseline remains unchanged; no rebaseline |

`protected_phase1.json` compares all 7,642 task-protected files with no differences.
The first visual fixture failed on an unavailable Python close-editor API after
capturing the deposit. It is retained as FAIL and superseded by PIE02's clean
exit and result. `ProceduralChestsPIE02/user_save_after.json` verifies/restores
the seven existing user saves; test activity does not replace user saves.

Prior equipment closure and rollback evidence is historical; it does not
validate this implementation.

Native02 passed both native selection/lifecycle suites, including the real
profiles and 100,000 ammunition stack trials. Its added map test failed because
the launcher opened HUB. Native03's relative filesystem path was rejected by
the map loader. The runner now explicitly loads the long package name before
Automation; Native04 passes all three suites. Both earlier failures are retained.
StorageProceduralChests01 reached 29 successful checks but timed out after a
long visual-review wait. It is FAIL, not evidence for the remaining lockpick/save
checks. StorageProceduralChests02 passes all 56 checks with its own reviewed image
and fresh marker.

Implementation commits: `fb08e12` (content and persistence), `ab3b64c`
(authoring and assets), `bf3a5ac8` (validation fixtures). The asset commit includes the user's previously saved
HUB/chest edits, preserved by the before snapshots; these are not attributed to
the implementation. The residual `/Game/_Game/Chest` redirectors and prior
`ChestMoveRollback.md` are left as found.

Release preflight history: package01 rejected stale Blueprint closure hashes;
package02 rejected the failed intermediate closure receipt. The existing
read-only runtime-asset validator and closure validator then passed, preserving
their append-only history and making no asset changes. Package03 timed out during
equipment validator startup while reading the AssetRegistry cache. The protected
launcher now accepts NoAssetRegistryCache, and packaging uses it only for that
read-only equipment preflight. No cache file was deleted and no validation was
disabled. Package04 is the fresh retry.

The required clothing compiler reused all 12 rows and republished its generated
`/EFClothingMorph/_Internal/Compiled/V4/DA_EFClothingFitRegistry` (5,760 to 5,780
bytes). Its catalog equality and protected-input gates passed; no clothing source
or binding asset was edited. This generated output is retained for consistency
with the certified package. See `clothing-certification.json`.

`ProceduralChestCalysto01` completed 39 travel/equipment checks through real
committed floors 1 and 2 and HUB return. Its final extra coverage assertion failed:
the current authored population produced zero chest actors. This run remains
FAIL for real dungeon-chest coverage, not a claim that generated chests were
observed. The unchanged authoring was preserved. Native05 instead validates the
exact `ConfigureResolvedLoot`/`FinalizeAndVerifyResolvedLoot` transition contract
directly with conflicting manual and procedural inputs. Its current-code Editor
build is build08; Game rebuild is game03. Archive_04 passed cook/package before
this extra test was added; Archive_05 is the fresh final-test package.

## Durable release evidence and limits

`Docs/Migration/Evidence/ProceduralChests/manifest.json` binds the copied receipts,
screenshots and current source/assets to SHA-256 hashes. `Saved` retains the full
attempt history and logs. `packaging-contract.json` records the final archive and
immutable UAT log. The packaged executable SHA-256 is
`43EFC4F300F5F6934FD318B1E2575CCFD157CD5D45322D226DC17A0441ADE3CE`.
The evidence-local `.gitattributes` preserves original line endings. All 50
manifest artifacts were verified byte-exact in Git's staging index, including
the explicitly tracked packaged image/result under the otherwise ignored Saved
subdirectory. The manifest's commit identifies the source/tool snapshot before
this documentation-only commit.

The packaged visual gate verifies the original menu, native item grid, distinct
Outerwear/Underwear columns, bag control, rendered outfit and readable weight.
The selected sword's MeleeDamage label overlaps its value in the untouched native
stats panel; whole-inventory typography is not certified as flawless. No inventory
widget or item-class presentation defaults were changed by this chest task.
The cook's existing vendor/DAZ/MetaHuman warnings remain in the full log, not
silently discarded. Historical evidence is used only for provenance/comparison.

The two Blueprint Details captures are linked here:
[Deposit](Evidence/ProceduralChests/ProceduralChestsPIE02/details-deposit.png) and
[Locked chest](Evidence/ProceduralChests/ProceduralChestsPIE02/details-locked-focused.png).
`Tools/Equipment/Open-ProceduralChestDetails.ps1` can reopen a closed editor with
the deposit's ProceduralChest component selected, without running PIE or saving
an asset. In an already open editor, use the Components/Details steps above.
