# TattooShop, Locked chest and Calysto repair — 2026-08-11

## Scope and protection

- Target only: `D:\Projects UE5\NoShellForWinter`.
- Source `D:\Projects UE5\LustAsDeadlySin` was not written or launched.
- Marketplace ACFU, Engine `DazToUnreal`, Calysto vendor DataAssets and `/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon` were not edited.
- `BP_MassiveDungeon.uasset` stayed byte-identical throughout the work: length `407722`, SHA-256 `47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B`.

## Implemented

### TattooShop

- Each manual tattoo now owns a stable GUID, exclusive component, exclusive MID, texture source, color, opacity, transform, layer order and enabled state.
- `BeginEdit`, `Preview`, `Commit`, `Cancel` and `Delete` operate only on the selected GUID and use an independent pre-edit snapshot.
- Dynamic-material creation unwraps a MID parent to a valid material or MIC before creating the new isolated MID.
- Pawn and visible compatible mesh resolution are map-independent and rehydrate after possession, mesh replacement and travel.
- Versioned GameInstance/SaveGame persistence supports `/Game` textures and safe runtime-PNG filenames. A missing runtime file disables only its own record.
- Manual tattoo components use the `ProjectManualTattoo` tag and remain separate from the automatic SkinnedDecal overlay.
- `WBP_TattooCustomization` now gates parameter reads with `IsValid`; its final SHA-256 is `8B1BDE461786A4B5DFB7CC590D1277BCF8C15B824D7B32FD7DBC72B7895F787C`.
- StorySelection forwards `Period` to Character Creation and hides/restores its own UI while Character Creation is active. No global TattooShop hotkey was added.

### Locked chest

- `AProjectLockedWorldItem` exposes `FProjectChestLootEntry`: `UACFItem` subclass, quantity and optional `ItemIndex` override.
- Loot initialization is authority-only, one-time and independently validates every entry through ACF item data.
- The original ACF gather interaction is replayed once after a successful lockpick. ACF storage transfer retains weight/slot overflow and the chest is not destroyed while content remains.
- Lockpick state, initialization state and storage component participate in the existing SaveGame contract.
- `/Game/_Game/Lockpicking/Locked` kept its public path and components. It is configured with `ACFHealthPotionBP x3`, `bDestroyOnGather=false`; final SHA-256 `EA0377E02FBFF6BA76B21272CCB1503EBEE589EDF8AD267663975930FAA80063`.
- No asset named `BP_Item` exists in target or read-only source, so a real loadable ACF item class was used.

### Calysto Dungeon Master

- V2 spawner presets now accept canonical `{SpawnerClass, Weight}` arrays with 0–64 unique, loadable, non-abstract `AActor` classes; zero weight disables an entry and empty presets are valid.
- The five old slots remain only behind explicit migration compatibility. V2 authored arrays are authoritative.
- Only the transient dungeon clone receives the canonical `ST_Spawner` reconstruction. Vendor DataAssets and `BP_MassiveDungeon` remain untouched.
- Official bounds are X/Y `10–30`, Z `1`, density `0–0.5`; vendor Blueprint differences are diagnostic only.
- The generation context freezes plan, seed and hash once. One 30-second watchdog covers generation, PCG completion, navigation and ready notification.
- On timeout/failure the PCG component is cancelled and cleaned, the same frozen plan is retried once, then flow returns to the recorded origin or HUB fallback with typed code/message/hash/attempt state.
- EFLevelFlow consumes the typed public snapshot rather than waiting blindly for 900 polls.
- V2 spawner table final SHA-256: `A3A777E0D43A5108CBF3D8EC50319DEA6D175E8BC5F581ACC21B71E0DE417023`.

## Validation evidence

- Mandatory cold editor build through `Tools/Migration/Build-NoShellForWinterEditor58.ps1`: **PASS**; Daz editor receipt guard: **PASS**.
- Directed project-owned Blueprint compilation, including 14 TattooShop Blueprints and `Locked`: **PASS**; no project-owned compile errors. One unrelated ACFU compass warning remained.
- `Saved/Automation/Final-CalystoV2/index.json`: 7 tests completed, 0 failed, 1 with warning. Covers deterministic channels/seeds, authored tables, clamps, invalid policies, Monte Carlo and schemas.
- `Saved/Automation/Final-DungeonHarness/index.json`: 2/2 **PASS**.
- `Saved/Automation/Final-TattooRuntimeTextures/index.json`: 1/1 **PASS**.
- HUB TattooShop visible QA: `HUB-TattooShop.png`, `HUB-TattooShop-Add.png`, `HUB-Tattoo-Red-Accepted.png`. Existing and selected tattoo layers remained visually independent.
- StorySelection visible QA: `Final-StorySelection-Period.png` and `Final-StorySelection-TattooEdit.png`; `Period` opens Character Creation without the StorySelection UI covering it.
- Live DungeonGeneration PIE log: plan `3365C7A7B8045162D8A4B138F82141E3`, `Compact28`, 28x28x1, density 0.3, five effective spawner entries; `GenerateLocal` exactly once, PCG finished, runtime NavMesh ready and `Dungeon runtime ready` reached. Screenshot: `Saved/Automation/Final-Calysto-Live.png`.
- Session regex check for `Accessed None`, `Blueprint Runtime Error`, invalid MID parent and Calysto runtime errors: empty.
- Live HUB chest: public actor retained `Object Mesh`, `StorageComponent`, `RootComp` and `ProjectLockpickableComponent`. The existing saved storage (`LockPick x20`) was restored without adding configured loot again, demonstrating the no-duplicate travel/load gate.
- Full Windows cook with Daz and the Daz bridge enabled: **PASS**, `6713/6713` packages, `0 error(s)`, 44 pre-existing/content warnings. Log: `Saved/Logs/NoShellForWinter.log`.

## Pending gates

- **PENDING — packaged executable:** `BuildCookRun` stops before cook because the precompiled Engine `DazToUnreal` plugin has no `ModuleRules` available to the UE 5.8 game target. The project has a guarded workaround only for the Editor target. The plugin was not disabled and the editor receipt remains repaired.
- **PENDING — destructive fresh-save chest matrix:** a fresh population plus full/partial pickup and lockpick failure/success matrix was not forced because it would require deleting or replacing the user's current SaveGame. The current persisted chest was inspected read-only.
- **PENDING — exhaustive TattooShop matrix:** automated three-tattoo cancel/delete assertions, `/Game/FullSample/Test`, save/load of every transform field and combined SkinnedDecal screenshot still need a dedicated isolated-save test fixture. HUB, StorySelection and DungeonGeneration live behavior were exercised.
- **PENDING — forced PCG callback loss:** the watchdog/retry/return paths compile and are covered structurally by harness/policy tests, but a live fault-injected missing callback was not run against the user's session.

## Final protected hashes

| Invariant | Length | SHA-256 |
|---|---:|---|
| `/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon` | 407722 | `47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B` |
| `/Game/FullSample/Player` | 124709 | `E7EDE80A927A34004014F497141097AC64597AD826C9EE85B0077EE7EA33891D` |
| `/Game/DazToUnreal/Female/Female` | 62494999 | `8C333C85F218B9858CB3BF67E7B9B15ABB071A08689917A90A18965A5A75AF13` |
| `/Game/DazToUnreal/Male/Male` | 170736396 | `AD03E3417CE42997EAEBF1B28189692A62BC2126CF707FA75F8C373587DD629C` |
| `/Game/DazToUnreal/Multiple/Multiple` | 15185847 | `3FC4E53877C2EA61A766761E12095FA5F9AF48993A2ECE6CE131CFCCA6BF9583` |
| Engine `DazToUnreal.uplugin` | 1375 | `66BFFDCDFFBD8DA16057C49E5625EBACA430911E762BA0534F1CCAEDE673469F` |
| Marketplace `AscentCombatFramework.uplugin` | 10270 | `CABE4B8538745D720D67EBC40B85CB3889035F2356DA82319D69E33570F50D73` |

No target file containing `Frederick` was discoverable by filename, so that named invariant remains `PENDING` until its canonical package path is supplied or resolved through a live asset reference.
