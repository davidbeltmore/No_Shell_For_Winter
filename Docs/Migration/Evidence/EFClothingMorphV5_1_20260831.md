# EFClothingMorph V5.1 - Evidence - 2026-08-31

## Scope

V5.1 converts the V5 clothing workflow back to one public authoring table with
an interface close to V4.5. The target project only was modified. The source
project `D:/Projects UE5/LustAsDeadlySin`, Marketplace ACFU and Engine
DazToUnreal were not edited or resaved.

## Authoring boundary

| Gate | Result | Evidence |
| --- | --- | --- |
| One public clothing table | PASS | `/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector` is the sole public authoring source and is the only clothing asset left open in the editor. |
| Old public V5 mirror | PASS | Live Asset Registry search under `/Game/_Game/Data/EFClothingMorph/V5` returns zero assets. Its closed eight-asset generated cluster had no external referencers before retirement. |
| Internal outputs | PASS | Live search returns nine saved assets under `/EFClothingMorph/_Internal/Compiled/V5`: manifest, registry, one body profile and six garment definitions. |
| Read-only generated data | PASS | Generated manifest, profile and garment properties are `VisibleAnywhere`; settings soft paths are `Config` without `CPF_Edit`. |
| V4.5-style UI | PASS (structural) | Normal groups retain V4.5 order; V5.1 layering/material exceptions are last and collapsed. The live Director editor displays the V5.1 single-table help panel. |
| Opaque default | PASS | All six current rows use `ForceOpaque`; the row schema default is also `ForceOpaque`. |
| Layer migration | PASS | First sync materialized three untouched underwear rows; explicit/non-underwear rows were preserved. Reopen reported `migrated-rows=0`. |

The public V5 mirror was generated during the earlier V5.0 pass and was
untracked. Seven files were retired through Unreal AssetTools. One already
unregistered orphan was moved, rather than destroyed, to:

`Saved/Migration/EFClothingMorphV51/LegacyPublicV5Backup/DA_EFGarment_UnderWearPanty_Female.uasset`

This backup is not visible to Asset Registry or runtime and can be removed
after the V5.1 release is accepted.

## Build and automation

| Gate | Result | Evidence |
| --- | --- | --- |
| Protected UE 5.8 editor build | PASS | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; UHT plus 22 build actions completed successfully in 33.33 seconds; Daz-enabled receipt guard passed. |
| Native V5.1 contracts | PASS | `EF.ClothingMorph.V51.Contracts`: succeeded 1, warnings 0, failed 0, not run 0. |
| Bounded runner | PASS | `Tools/ClothingMorphV5/Run-EFClothingMorphV5NativeAutomation58.ps1 -TimeoutSeconds 75`; completed in 48.84 seconds. |
| Automation report | PASS | `Saved/Migration/EFClothingMorphV51/NativeAutomation_20260831_082014/Report/index.json`; test duration 0.007846 seconds, exit code 0, `GIsCriticalError=0`. |
| First publication | PASS | `created=8 updated=1 saved=9 drafts=0 migrated-rows=3`, producing the missing internal manifest/profile/definitions while updating the registry. |
| Reopen idempotence | PASS | Current editor log: `already current`, `created=0 updated=0 saved=0 drafts=0 migrated-rows=0`. |
| Dirty generated assets | PASS | Director and all nine active internal V5 assets report `is_dirty=false`. |
| PIE | NOT RUN | V5.1 changes the authoring boundary/UI and generated paths; no automatic PIE was needed. Live MCP reports `IsPIERunning=false`. |
| Manual close-up tattoo/skin QA | PENDING | User-owned acceptance session requested. |
| Cook and packaged validation | PENDING | Separate release gates; not run in this bounded pass. |

Current clothing startup is clean: V4 reports `FRESH` with six valid garments
and V5.1 reports the single-table sync already current. There are no
EFClothingMorph-specific warnings or errors in the current editor log.

## Protected invariants

| Protected item | Bytes | SHA-256 | Result |
| --- | ---: | --- | --- |
| `Content/FullSample/Player.uasset` | 124709 | `E7EDE80A927A34004014F497141097AC64597AD826C9EE85B0077EE7EA33891D` | PASS |
| `Content/DazToUnreal/Female/Female.uasset` | 62494999 | `8C333C85F218B9858CB3BF67E7B9B15ABB071A08689917A90A18965A5A75AF13` | PASS |
| `Content/DazToUnreal/Multiple/Multiple.uasset` | 15185847 | `3FC4E53877C2EA61A766761E12095FA5F9AF48993A2ECE6CE131CFCCA6BF9583` | PASS |
| `Content/DazToUnreal/Male/Male.uasset` | 170736396 | `AD03E3417CE42997EAEBF1B28189692A62BC2126CF707FA75F8C373587DD629C` | PASS |
| ACFU 4.3.5 descriptor | 10270 | `CABE4B8538745D720D67EBC40B85CB3889035F2356DA82319D69E33570F50D73` | PASS |
| DazToUnreal 5.8.0.491 descriptor | 1375 | `66BFFDCDFFBD8DA16057C49E5625EBACA430911E762BA0534F1CCAEDE673469F` | PASS |

The four protected target assets are absent from the Git dirty set. ACFU and
DazToUnreal descriptors retain the expected hashes, versions and locations.
Frederick still has no independent authoritative file path in the evidence
index; this change neither references nor writes a Frederick asset.

## Unrelated project findings

The editor and automation logs still report the pre-existing missing
`GameFeatureData` Asset Manager rule. The current editor log also contains
pre-existing ACF/gameplay-tag warnings, an external HTTP/server failure and two
stale MCP session errors caused by reconnecting after editor restart. The live
MCP session is healthy. None entered the V5.1 automation result.

## Manual acceptance

In the already-open Director, inspect that the interface feels like V4.5 and
add or modify one garment row. Expected behavior:

1. Only the Director is edited.
2. `ForceOpaque` is already selected unless an explicit exception is authored.
3. Internal outputs update automatically and the public V5 folder remains
   empty in Asset Registry.
4. In one short manual HUB/character-creator session, clothing covers skin and
   tattoos while uncovered skin and garment fitting remain unchanged.
