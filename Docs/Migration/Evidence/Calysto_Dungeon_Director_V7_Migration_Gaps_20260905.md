# Director V7 authoring migration audit — 2026-09-05

Status: **PENDING**. This field-mapping audit and importer repair is not gameplay parity or authorization to retire V6. The candidate remains subject to the [V7 implementation contract](../Calysto_Dungeon_Director_V7.md) and [timed validation gates](../../../.agents/skills/calysto-dungeon-master/references/validation-gates.md).

All work is confined to D:\Projects UE5\NoShellForWinter. The source game, vendor content and protected character assets remain untouched. The importer saves only the exact new candidate package through Unreal Editor APIs.

## Evidence and current gate

| Item | Evidence / state |
|---|---|
| Immutable input | /Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy |
| Input class | /Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset |
| Input package SHA-256 | F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323 |
| Candidate | /Game/_Game/Data/CalystoDungeon/DA_CalystoDungeonDirector |
| Candidate class | /Script/EFProceduralRuntime.EFCalystoDungeonDirectorAsset |
| Importer | Tools/Migration/Import-CalystoDungeonDirector58.py |
| Native exporter | UEFCalystoDirectorEditorLibrary::ExportAuthoredField |
| First editor import | Failed on omitted default SelectionWeight; zero saved packages and unchanged V6 package hash |
| Repair | Recursive source and target native export includes default-valued nested fields |
| Local checks | Python syntax and bounded synthetic mapper tests passed; no asset/world operations |
| Complete editor import | PENDING until the repaired importer produces its actual receipt |
| Idempotence | PENDING until a second unchanged invocation records zero saves |
| Runtime behavior / authority / retirement | PENDING independently of authoring validity and mapping coverage |

The initial export identifies Styles [0] Standard, [1] Compact, [2] Branching and Themes [0] Forge, [1] Shrine. It contains 34 category groups and 108 actor/inventory entry occurrences. That export used delta serialization for nested structures; it establishes those observed arrays but is not a complete field-coverage gate. Use the repaired full export for final counts and review.

## Retained mappings

MAPPED means a source value has a native destination. It does not establish an implemented or verified runtime consumer. Every mapping row separately records runtime verification as PENDING.

| Source | Native preservation |
|---|---|
| Profile/entry IDs, labels, enable state, weights | Persisted GUID identities, independent labels, enable state and unchanged relative weights |
| Layout endpoints and room bounds | Inline distributions and native room bounds; mathematical changes are recorded separately |
| DungeonMaterials Floor/Wall/Roof references | Exact Style soft references |
| Theme RoomMaterials modes and references | Independent Floor/Wall/Roof override modes; inherited inactive references retained |
| Floor/Wall/Roof, DoorFrame, Door, RampTop/Bottom, WallDoor | Mesh/class references, weights, fixed transforms and doorway wall/frame/door transforms |
| Style optional objects and all eight Theme architecture zones | Inline native opportunities, explicit mesh/actor/baked-PCG/Empty payloads, weights, rotation and variation |
| Inactive architecture union references | Retained in native union properties; only the active payload contributes reachable dependencies |
| Wall lights | Classes, weights and variation; common per-Style zone/jitter retained |
| Categories | Explicit one-time gameplay-role/budget mapping; Inherit/Extend/Replace/Block stay distinct |
| Actor catalogs | IDs, labels, classes, weights, rarity including Winter, explicit archetype/gender/lifecycle, threat cost, depth, cooldown, capacity and placement |
| Blocked entries | Complete authored rows retained as disabled entries; Extend disables the matching inherited identity without hiding an irreversible removal flag |
| Chest contents | Inventory classes, weights, rarity, depth, cooldown, capacity and graveyard eligibility |
| Threat and floor budgets | Exact authored endpoints and Enemy/Food/Chest/Loot/Special Event/total actor capacities |
| Level offsets | Style values retained; observed Styles all use [-2, 2], also applied to Theme entries |
| Decals | Modes, chance, capacities, size endpoints, cull distance, material/texture references, weights and intersected profile/variant surface masks |
| Performance fields with direct equivalents | Room identity quantization, decal pool capacity and maximum room records |

Initial Floor/Wall/Roof material references remain:

- Grey Style: /Game/Calysto/Dungeon/Material/MI_GreyTiles.MI_GreyTiles.
- Forge: /EFProcedural/Calysto/Internal/Materials/Architecture/MI_Template_BaseOrange.MI_Template_BaseOrange.
- Shrine: /Game/FullSample/DemoRoom/Materials/MI_Display_Blue.MI_Display_Blue.

These are reference-preservation observations. Actual generated assignments, Nanite/instancing compatibility, shared-boundary ownership, appearance and packaged dependencies still require runtime/visual validation.

## Intentional corrections

| Correction | Recorded meaning |
|---|---|
| Old “PERT” ranges | Preserve Minimum/Mode/Maximum under an accurately named continuous triangular law. Archive Shape; do not claim old contraction behavior was preserved. |
| Presence/threat progression | Preserve exact endpoints, convert fraction chances to percentages, use linear interpolation and archive Tau. |
| Rarity Nothing | Remove the implicit second absence roll. Preserve four old numeric tier weights, add explicit Winter relative weight 0.01, normalize populated eligible tiers. |
| Counts | Use an explicit fixed conditional amount from the authored positive minimum. Capacity is not reinterpreted as a random maximum. Feasibility must precede selection. |
| Theme guarantee | One guaranteed eligible room plus independent additional-room trials. Observed source chance 0.25 becomes 25%. |
| Extend | Old authored chance/count/rarity choices use explicit override switches; inherited floor capacity remains separate. |
| Start/End class fields | Archive references; progression identity stays owned by native/travel integration. |
| Recovery/adaptation defaults | Four total attempts, one shared 30-second deadline, adaptation disabled. |
| Permanent summaries | Archive derived rows; ordinary authoring stays clean and diagnostics remain on demand. |

The 100,000-trial native probability suite is a separate gate. It does not prove the realized gameplay distribution, feasibility conditioning, materials or catalog spawning.

## Exact unsupported field families

For this observed source, Styles[i] means i = 0, 1, 2; RoomThemes[j] means j = 0, 1. The machine receipt enumerates every actual leaf path and value.

| Source paths | Why PENDING |
|---|---|
| Styles[i].Traits.Mystery, .Danger, .Safe, .Abundance, .ClothingInfluence | No complete explicit trait-input/modifier contract. Branching has nonzero Mystery. |
| RoomThemes[j].Traits.Mystery, .Danger, .Safe, .Abundance, .ClothingInfluence | Forge Danger/Abundance and Shrine Mystery/Safe require supported bounded behavior. |
| Styles[i].Lighting.Mode | V6 mode selects hidden base height/spacing and variation. Intensity alone does not preserve Warm/Balanced behavior; Standard authors Warm. |
| Styles[i].Decoration.Density, .MaximumDecorationsPerRoom | Native optional architecture is retained, but these separate density/capacity controls have no implemented equivalent. |
| RoomThemes[j].Description | Authored prose archived; no direct profile description field yet. |
| RoomThemes[j].PreviewColor.R, .G, .B, .A | Preview colors archived; they do not control actual materials. |
| Styles[i].Decals.FadeStartDistanceCm; RoomThemes[j].Decals.FadeStartDistanceCm | World-distance fading is not screen-size fading; no invented conversion is applied. |
| PerformanceAndSafety.ValidatedDungeonSizes[k] | Current native range validation does not preserve an arbitrary sparse size allowlist. |
| PerformanceAndSafety.HardCeilings.MaximumEnemies, .MaximumLooseFood, .MaximumChests, .MaximumLootActors, .MaximumSpecialEvents, .MaximumDirectorActors | Old safety limits remain archived separately from copied Style budgets; resolve their runtime relationship explicitly. |

Additional conditional gaps receive exact indices when encountered:

- Unknown category IDs require explicit role/budget mappings.
- Mixed actor/inventory arrays require a supported typed split.
- Heterogeneous per-entry wall-light placement cannot collapse into one shared setting.
- Different Style level-offset bounds require explicit Theme-entry inheritance.
- Different Style room-theme chances require a supported per-Style override to preserve them.
- Every newly encountered source leaf without an explicit mapping becomes PENDING.

All exact values remain in the immutable full source archive. A partial candidate can be authoring-valid while these gaps keep complete migration false.

## Review and advancement

The importer has a 60-second budget, validates the exact target editor/version, refuses dirty source/target packages, and hashes V6 package sidecars before and after. It performs no authority switch, cook or deletion.

Before changing the target, it stages a transient candidate, runs native authoring validation, exports it completely and compares every requested mapped value against the native result. Ignored fields, enum fallback, dropped array entries and altered mapped values reject staging. This proves authoring retention only.

Review these artifacts after an editor invocation:

1. SourceV6_Complete_[package hash].json: immutable raw and parsed source fields.
2. CandidateV7_[authored fingerprint]_[mapping fingerprint].json: mapped values, complete native candidate export and mapping records.
3. ImportDirector.json: exact source-to-target rows, corrections, PENDING leaves, hashes, roundtrip result and saved packages.
4. A second unchanged invocation: identical authored fingerprint, unchanged source hashes and no saved packages.

Keep authoring migration, runtime consumers, real traversal, realized probability, visual QA, cook, packaged validation and V6 retirement as independent gates.
