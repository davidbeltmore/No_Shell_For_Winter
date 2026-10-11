# NPC outfits

## Scope and frozen acceptance budget (2026-10-11)

Target: `D:/Projects UE5/NoShellForWinter`; source remains read-only. Initial
snapshot `086fbd423569ab433c0b03393191d636ba881847`, clean branch
`feature/efclothing-v4-multicloth`. UE 5.8 target editor PID 12972, MCP
`http://127.0.0.1:8000/mcp`, `EditorAppToolset.IsPIERunning=false` at preflight.

Roster: 20 archetypes / 40 Male/Female variants. Body catalog: enabled NPC bodies
`Male.01`, `Male.02`, `Female.01`. Pilot: SkillGuy service and MeleeEnemy, then
eligible roster after pilot validation. Existing nonhumanoid/player exclusions
remain in `Tools/NPC/Roster.json`.

User accepted the proposed budget before measurement: global provisional 60 FPS
(16.67 ms/frame); at 25 NPCs incremental dressed-minus-undressed CPU <=2 ms,
GPU <=3 ms, frame p95 increment <=4 ms, warmed group outfit completion <=2 s.
Groups 1, 10, 25; 50 diagnostic only. Cold loading separate. Same scene, bodies,
seed, camera, resolution and animation conditions. Record 30–45 s steady capture,
spawn peak, readiness, memory/render resources where available and teardown.
No acceptance threshold may be changed after seeing results.

## Authorities and implementation in progress

- Native ACF owns inventory/items. EFEquipment coordinates destinations and
  reservations. Explicit `CreateForNPC` uses shared operations without the player
  discovery timer, UI notification, carry projection or native death adapter.
- `UProjectNPCOutfit` owns stable ID and garment/destination references.
  `FProjectNPCArchetype.Outfits`: empty disables integration, one entry fixes it,
  multiple positive weights use appearance seed xor a separate outfit salt.
  `UProjectNPCOutfitComponent.OverrideChoices` supplies per-instance selection.
- `UProjectNPCOutfitSubsystem` retains the small roster, admits one pawn/frame
  and up to four initializing outfits. This bounds initial attachment; it is not
  yet proof of a global binding-stream budget or measured crowd capacity.
- Appearance resolution calls the outfit queue only after applying body/morphs.
  Native equipment initialization and ALS idle precede equip. The same appearance
  record stores outfit ID/version plus the existing native inventory envelope.
  A versioned saved record takes precedence, including an emptied inventory.
- EFClothingMorph remains the fit/coverage authority. Only selected owners receive
  its runtime and equipment bridge. Global non-player enable remains false.
- Failures are explicit and bounded (20 s initial readiness). Initial grant failure
  attempts inventory rollback. No whole-character visibility suppression is added.

Live preflight: `Saved/NPCOutfits/inspect.json`. Protection baseline:
`Saved/NPCOutfits/Baseline/before.json` (7,643 protected files, 72.39 s).
The loaded settings confirm `bEnableForNonPlayerPawns=false`,
`MorphSyncIntervalSeconds=0`, `MaximumConcurrentBindingLoads=2`.

## Gates

| Gate | Status | Evidence |
|---|---|---|
| Target/editor/clean initial branch | PASS | MCP and initial Git/process inspection above |
| Scope and budget frozen | PASS | User accepted before implementation measurement |
| Protected baseline | PASS | before.json, initial commit above |
| Shared NPC equipment / FullRag pilot | PENDING | Implementation in progress |
| Editor/Game build and Blueprint compile | PENDING | Not yet run |
| Persistence and functional PIE | PENDING | Not yet run |
| Visual fit, movement, morphs, body swap | PENDING | Not yet run |
| Performance 1/10/25/50 | PENDING | Not yet run |
| All eligible roster audit | PENDING | Not yet authored |
| Cook and packaged QA | PENDING | Not yet run |
| Final protection, skills, GitHub publication | PENDING | Not yet run |

## Work preservation

The initial editor had an unsaved `/Game/_Game/Hub/HUB` although Git was clean.
The user chose to save it personally. A subsequent read-only inspection confirmed
no dirty packages and Git showed `Content/_Game/Hub/HUB.umap` modified. This is
user work, not an outfit change; preserve it when committing/publishing.

The external `A:/Skills Codex/ef-clothing-unisex-workbench/SKILL.md` now correctly
documents RagFootWrap's live `ItemSlot.Armor.Feet`; skill validation passed.
Garment leaf destinations are Chest, Legs, Feet; no garment asset was modified.

## Pilot evidence (2026-10-11)

Editor build PASS: `Saved/NPCDAZ/outfits-editor-build-process.json` (304.473 s).
After adding the native fixture, one fixture-only type-deduction compile error
was corrected; final incremental build PASS in `outfits-editor-build3-process.json`
(16.086 s). Daz receipt guards passed. Protected comparison PASS: all 7,643
files unchanged (`Saved/NPCOutfits/Baseline/after.json`).

Authoring PASS: `Saved/NPCOutfits/author.json`: 4 configured pilot variants,
36 intentionally unconfigured until rollout. Audit/Blueprint compile PASS:
40 variants, zero errors, 11.921 s (`Saved/NPCOutfits/audit.json`).
An inherited duplicate Ammo slot prevented initial rule initialization. Shared
slot normalization now removes duplicates before testing tag ancestry, preserving
weapons and all unique native destinations. Failed receipts remain in Saved.

Four-variant PIE PASS (27.36 s): SkillGuy/MeleeEnemy x Male/Female, exactly three
equipped Rag entries with quantity one, 3/3 Ready and no clothing issues.
The reviewed screenshot shows all three pieces on each fixture. This establishes
basic presentation; close movement/morph/coverage QA remains PENDING.
First-use readiness included 10.5–10.9 s on three fixtures and 0.572 s on one;
these are not a warmed crowd benchmark.

`Saved/NPCOutfits/Native/pilot_male02_01`: PASS for Male.02 body selection,
three rendered pieces, native death adapter absent, shirt unequip preserved by
reinitialization, inventory count unchanged, zero pending initialization after
destruction. Readiness 1.893 s. Screenshot reviewed: garment presence/basic fit
PASS; 2-second capture is functional evidence, not a performance sample.

Real disk round trip PASS in `Saved/NPCOutfits/Save` (6.656 s): two service NPCs,
unequip shirt -> save via EFEquipmentSaveLibrary -> re-equip -> load; exact saved
GUID/count/equipment/body/morph equality and zero duplicate GUIDs. Loot removal,
destroy/recreate, weighted-choice tests and cold packaged restoration remain
PENDING. Four existing user saves are guarded; restoration after the previous
session verified all four hashes and recovered ProjectIntimacy.sav exactly.
