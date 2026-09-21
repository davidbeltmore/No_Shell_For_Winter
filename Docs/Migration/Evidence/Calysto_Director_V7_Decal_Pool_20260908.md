# Calysto V7 pooled decals and G5 correction - 2026-09-08

Status: **implemented source; full integration PENDING**. Parent owns builds, native execution, Editor and assets. Scope is one unversioned project-owned pool plus an explicit five-leaf migration correction. Native selected-decals capability guards remain intact. No source-game, vendor, asset, configuration, random-selection or acceptance-ledger changes were made by this subtask.

## API and ownership

`AEFCalystoDecalPoolOwner` preallocates exactly 24 `UDecalComponent` objects on one transient actor. `Begin(Token, Configuration, NativeAdapter, ReservedProposals, LoadedResources)` reads completed native observation; it does not generate or advance a transaction. Typed proposals carry stable reservation/room/opportunity/variant IDs, surface, exact transform/size/normal and actual component/instance support.

`Begin` verifies the actual adapter's room/opportunity records, protected rooms, main route/door-clearance flags, room footprint corners, registered owned physical support, selected instance transform/bounds and one fixed support trace per proposal. Missing support or payload rejects the batch. Selected Style and resolved Theme capacities enforce shared floor/surface limits and maximum one per room; initial authored values remain 8 active, Floor 3, Wall 4, Roof 1. There is no probability roll, replacement, position search or capacity truncation in the pool.

Every selected material/color/normal dependency must have an exact explicit already-loaded binding; the pool retains shared UObject references. Deferred-decal material domain is required and dynamic material instances are rejected. It does not prove that arbitrary supplied material texture parameters consume those declared textures: actual allowed RealisticBlood/material closure must be verified before capability activation.

`Begin` stages all components hidden/inactive. `Verify` checks sealed proposal identity, exact realized components/materials/transforms/fade settings, ownership and physical supports. The proposal hash serializes full numeric values and token identity; it is a lease identity, never a random-domain seed. `ActivateCommitted(Token)` requires verification and permits camera-driven presentation. Only the transaction owner should call it after successful commit.

`Tick` observes actual local player camera locations (bounded to four). `UpdateViewLocations` hides decals beyond their authored world distance; an empty view list hides every decal. Screen-size fade uses `UDecalComponent::SetFadeScreenSize`. `Release` is token-scoped and idempotent; it hides/deactivates slots and clears every material, selected resource and native adapter lease. Release evidence distinguishes 24 reusable allocated slots from zero active leases. EndPlay also clears the pool. No stain owns an actor, MID or timer.

## G5 mathematics and migration

V6 `UEFCalystoDecalPoolComponentV6::AcquireDecal` (`EFCalystoDecalPoolV6.cpp:530-537`) computes, for its validated positive distances:

`threshold = max(poolFadeScreenSize, clamp(max(Size.Y,Size.Z) * .5 * (1/CullDistanceCm + 1/FadeStartDistanceCm), .0001, .25))`.

UE5.8 `Renderer/Private/DecalRenderingShared.cpp` additionally applies projected radius, distance, FOV, viewport width and renderer scale. This was a size-dependent screen approximation, not exact distance fade or hard distance culling. The existing shared blood master binds texture alpha times `BloodOpacity` to opacity and has no DecalColor expression; setting component color cannot honestly implement distance opacity on that material.

The master contract permits accurately named screen fade plus real distance culling. `Mapper.decals` retains all five original 2500 cm values in hidden `WITH_EDITORONLY_DATA` `FEFCalystoDecals.SourceFadeStartDistanceCm`. Each row records `MATHEMATICAL_CORRECTION` and `RETIRED_DISTANCE_APPROXIMATION` with the formula above. `FadeScreenSize=.01` preserves the previous native pool default; existing 4000 cm `CullDistanceCm` now drives actual camera-distance visibility. No numeric copy of 2500 into screen fade or claim of identical old opacity is made. Existing native complete export exposes archived metadata on demand, but it is neither editable nor Blueprint-visible gameplay UI.

## Checks and exact evidence

- Pure importer command `python Tools/Migration/Test-CalystoDirectorImporter58.py -v`: **13 tests PASS, .251 seconds, exit 0**. Five actual archived profiles independently assert exact 2500 retention, 4000 cull, .01 screen threshold, unchanged source input and five unique correction rows. Existing archive comparisons preserve every unrelated migrated field.
- Parent reports `BuildDecalOwnerNames` **PASS**, 14.92 seconds. This compiles the pool; it predates the new G5 metadata. Subsequent `BuildDecalSchema` parent reports **PASS**, 58.38 seconds, with restored protected receipt. Formal build evidence belongs to the parent-owned build log/exit records.
- `Saved/Migration/CalystoDungeonDirectorV7/Native_DecalPool_20260908/StrictSummary.json`: **FAIL**, two exact tests, 47.611968 seconds, launcher exit **1**. Each test recorded `PCG RegisterOrUpdateExecutionSource: Component has invalid bounds`; no behavioral assertion error appears in the report. Strict receipt SHA-256 `77abe7bb6079edc58efed4bc5421e390f6174e3f50ec0415094e6f4c4679b602`; `Report/index.json` SHA-256 `838a59d50cd573ecfccc6a3039dc77768af149c3df86f3bbbc21e945727088a9`. This receipt remains failed.
- Corrected fixture now registers a real noncolliding input `UBoxComponent` before PCG registration. UE5.8 `PCGHelpers::GetActorBounds` intentionally omits generated components; generated physical supports alone do not define valid input bounds. No runtime guard or log suppression changed.
- Subsequent parent-owned `Native_DecalSchema_20260908/StrictSummary.json`: **PASS**, all three exact expected tests (the two pool tests plus `CompleteFieldExport`), no failures, 48.249075 seconds, launcher exit **0**. Strict SHA-256 `23cc22b32f9c9496aacc2b24b2225fe68555945e7442ffdc08da823155f6891f`; `Report/index.json` SHA-256 `4247fe23fcb7ac77deedf6ec77131881996a48691ccfbd1c82dd20db1cd7d616`. This verifies the corrected fixture and new G5 metadata assertions, without changing the original failed receipt or proving full-floor integration.
- Focused native tests: `Director.Decals.StagingCullingAndRelease` and `Director.Decals.ReservationRejectionIsAtomic`. They exercise actual registered decal components and physical support traces in an isolated game world, hidden staging, explicit commit, distance boundary/multiple/no cameras, exact payload tampering, changed supports, protected-room/capacity/resource rejection, release and component reuse. They invoke the same private production work after supplying typed native observation; actual full-adapter selection and live transaction wiring remain **PENDING**.
- Existing `Director.Editor.CompleteFieldExport` additionally checks hidden fade metadata roundtrip and property flags; the subsequent three-test receipt above executes those new assertions.

## Stable source fingerprints

Paths are relative to `D:/Projects UE5/NoShellForWinter`. Parent pre-change snapshot is `Saved/Migration/CalystoDungeonDirectorV7/Resume_20260908/SourceBefore.zip`. Fingerprints below include the fixture correction and new G5 metadata; later G1/G3 edits to shared authoring/import files require new hashes.

| Path | SHA-256 |
| --- | --- |
| `Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Public/Calysto/EFCalystoDecalPool.h` | `cfef07217584f4bbc711ac93b8b184a043ed6b0bf7216a0039032c1b57af62c2` |
| `Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/Calysto/EFCalystoDecalPool.cpp` | `57ecff4fa02cd2e1bf9192130f83be98394354ff94a4ccc4e70495a34d564caf` |
| `Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/Tests/EFCalystoDecalPoolTests.cpp` | `7afa7c36892ca33b51d6b5301c51d66d34ee5a91e75949e6a1753ddb88d882b2` |
| `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDirectorTypes.h` | `b780e487e375e7c219eaeaba59174d675916adb5323d184eb91f06d3d6a2da3f` |
| `Plugins/EFProcedural/Source/EFProceduralEditor/Private/Calysto/EFCalystoDirectorEditorLibrary.cpp` | `c4ac491b325c1d8aa8f810feda9e38d85393231caa6b6a479753db6e768a0215` |
| `Tools/Migration/Import-CalystoDungeonDirector58.py` | `3308899f1bc941f7c1c35c0fa0f18ed57f6182fa4a6758cbf06347fea7beafc4` |
| `Tools/Migration/Test-CalystoDirectorImporter58.py` | `3b582fe2d8826247b7a02103c0edd18a8895a1971b3e03157513c8fb6265a2a7` |

Next: parent stages the current schema, verifies the selected material closure and builds actual bounded surface reservations before transaction integration. Live decal rendering, cancellation through the real floor transaction, retention soak, cook/package/entitlement remain separate gates. The frozen 31 original acceptance criteria are unchanged; helper counts do not increase completion.
