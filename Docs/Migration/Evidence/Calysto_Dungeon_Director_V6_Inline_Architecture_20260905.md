# Calysto V6 inline Architecture and materials — 2026-09-05

Status: **Implemented; native/editor validation PASS. Manual gameplay and
visual acceptance PENDING.** No performance or packaged-release claim is made.

Writable target: `D:/Projects UE5/NoShellForWinter` (UE 5.8).
Read-only source, not touched: `D:/Projects UE5/LustAsDeadlySin`.
Snapshot: working tree based on `f8d07043f7b5cab30dd72f43c7f1c29e93392306`;
pre-existing unrelated changes preserved. No new commit created.

## Authoring delivered

The sole Director configuration remains:

`/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy`

- `Dungeon Styles > [Style] > Style Materials`: Floor, Wall and Roof.
- `Dungeon Styles > [Style] > Style Architecture`: structural meshes, door
  parts, native start/end schema, ramps, wall lights and decoration arrays.
- `Room Themes > [Theme] > Room Materials`: independent Floor, Wall and Roof
  overrides or `Inherit Style`.
- `Room Themes > [Theme] > Room Architecture`: Wall Bottom/Middle/Top, Floor,
  Corner Bottom/Middle/Top and Roof, with weights and native transform controls.
- World-spawn catalog entries expose `Placement Zone` and `Position Jitter Cm`.
  Existing pickups, enemies and chests default to Floor; wall torches default
  to Wall Middle. The default positional variation is 2 cm. Chest contents are
  inventory items, not world placement candidates.

`Calysto Room Type` is removed from authoring. The adapter synthesizes native
room-schema objects transiently from each inline theme. The saved Director has
no dependency on `DA_RoomForge` or `DA_RoomShrine`. Material, mesh, Blueprint and
baked level-instance payload references remain normal content references; they
are not separate room/style configuration documents.

The one-time upgrade preserves existing weights, layout, catalogs, budgets and
decals, fills new Architecture fields from inspected native defaults, and sets
the requested colors. Native weighted empty decoration entries are intentional
and preserved; they must not be replaced with invented props.

| Profile | Floor / Wall / Roof |
| --- | --- |
| General Style | Existing `MI_GreyTiles` |
| Forge | Project-owned instancing-safe `MI_Template_BaseOrange` |
| Shrine | Existing `/Game/FullSample/DemoRoom/Materials/MI_Display_Blue` |

The original Engine orange instance returned false for both
`MATUSAGE_INSTANCED_STATIC_MESHES` and `MATUSAGE_NANITE`. A shared project-owned
Material Instance retains its look and enables both usage overrides at:

`/EFProcedural/Calysto/Internal/Materials/Architecture/MI_Template_BaseOrange`

No Engine parent, vendor material or Calysto source asset was changed. No MID is
created per room. Runtime is still async-preloaded and applies resolved slots
through the transient Director adapter.

## Runtime corrections

- Only themed rooms enter the native theme-decoration branch. Unthemed rooms
  still participate in base geometry but no longer feed a branch that reads
  eight properties from a null RoomType.
- Native PCG metadata uses supported `FSoftObjectPath` values to already-resident
  transient objects, retained for the floor. UE 5.8 rejects Object-typed PCG
  attributes; the attempted Object implementation was removed after testing.
- Architecture participates in immutable configuration hashes. Transient
  object addresses/paths are not used as configuration identity.
- Root graph clones expose native placement streams without saving the vendor
  graphs. Floor items keep existing navigation-validated anchors; non-floor
  choices use matching room/zone candidates, static surface traces and normal,
  occupancy and spacing validation. These non-floor placements still need
  gameplay/visual QA.
- Existing test behavior is retained: one eligible room is guaranteed a theme
  when a valid enabled theme set exists; remaining rooms use the independent
  25% presence roll. Start, End and protected roles stay unthemed. This means
  the aggregate themed fraction with the test guarantee is above 25%.
- Existing floor-identity stamping/travel safeguards remain. No claim that the
  reported HUB bounce or Floor 2 regression is visually verified in this pass.

## Evidence

Evidence paths below are relative to the writable target, not the source.

| Gate | Result | Evidence |
| --- | --- | --- |
| Protected cold Editor build | PASS, 22.41 s | `Saved/Migration/CalystoDungeonDirectorV6/InlineArchitectureEditorBuild.log` |
| Native tests | PASS, 38/38, no test warnings/errors or Content mutation | `Saved/Migration/CalystoDungeonDirectorV6/NativeAutomation_20260905_inline_architecture_02/StrictSummary.json` |
| Blueprint compile, including DoorToLevel | PASS, 30/30, no saves, clean Editor exit | `Saved/Migration/CalystoDungeonDirectorV6/BlueprintCompile_20260905_inline_architecture/StrictSummary.json` |
| Exact policy upgrade and forbidden room-DA dependency audit | PASS | `Saved/Migration/CalystoDungeonDirectorV6/InlineArchitectureUpgrade.json` |
| Read-only policy/dependency validation | PASS results; see process note below | `Saved/Migration/CalystoDungeonDirectorV6/CreateDirectorPolicyV6.json`, `ValidateCookClosureV6.json` |
| 13 protected Calysto/map/door hashes | PASS vs retained pre-change baseline | `Saved/Migration/CalystoDungeonDirectorV6/InlineArchitectureProtectedAfter.json` vs `Saved/CalystoV6/ProtectedBaselinePolicyCore.json` |
| Broader protected invariants | No new drift; historical Phase 0 comparison still FAIL | `Saved/Migration/CalystoDungeonDirectorV6/InlineArchitectureInvariantsAfter.json` |

The broader invariant check has the exact same 145 mismatch records and
authoritative asset hashes as
`Saved/Migration/Evidence/ProtectedInvariantVerification.json` from
2026-09-04 12:43:27 UTC. These pre-existing mismatches were not repaired or
silently re-baselined. Daz plugin files match the baseline.

Policy bytes after the upgrade:
`F08324D234872CC6BDD7A12F3D5DBEEE3BF03502CBA5FA32187CFBF7AB029323`.

The first native run failed and is retained as `_01`; `_02` is the corrected
passing run. A Python commandlet produced passing authoring/dependency results
but crashed inside Python during shutdown. That process is **not** a clean PASS.
The combined normal-Editor authoring retry also emitted passing results but
failed after requesting exit, during subsequent Python/toolset initialization.
Its strict process receipt correctly remains FAIL at
`Saved/Migration/CalystoDungeonDirectorV6/InlineAuthoring_20260905_clean_exit/StrictSummary.json`.
That auxiliary Python lifecycle issue is unresolved and must not be confused
with a successful process gate. The separate 30-Blueprint gate exited cleanly.

Normal interactive Editor was reopened with the protected launcher at
2026-09-05 08:29 UTC. Startup completed, map check reported zero errors/warnings,
and the native MCP endpoint `http://127.0.0.1:8000/mcp` responded successfully.
After fresh tool discovery, `EditorToolset.EditorAppToolset.IsPIERunning`
returned false, and `OpenEditorForAsset` opened the exact V6 Director for the
user. No PIE session or asset save was requested during this final handoff.

Only V6 policy Content is present under `_Game/Data/CalystoDungeon`; active
Config and EFProcedural/EFLevelFlow/EFProjectSystems source scans find no old
V3/V4/V5 Calysto API/path references. Earlier legacy retirement was not rerun;
no assets were deleted in this revision. Actual IoStore/package audit remains
PENDING.

## Manual QA handoff

1. Open the V6 Director. Check inline Architecture and material controls in a
   Style and both Themes; no RoomForge/RoomShrine configuration DA is required.
2. PIE from HUB and use the real DoorToLevel. Confirm generation finishes and
   the player remains in the dungeon.
3. Find the guaranteed eligible themed room: orange Forge or blue Shrine.
   Check each wall, Floor and Roof and the boundary with the grey general
   dungeon. At least one theme is guaranteed, not both theme types every floor.
4. Use the generated floor door and confirm Floor 2, without a HUB bounce.
5. Check inherited vs overridden slots, content isolation, wall torches,
   Floor pickups/chests, then opt a suitable prop into a non-floor zone.
6. Check existing budgeted blood decals on allowed surfaces. Shrine remains
   blocked by its authored decal profile. No new blood content was imported.

PIE traversal, screenshots/visual QA, non-floor placement in gameplay, load-time
P95/hitches, the 25-floor soak, actual cook, Development/Shipping packages and
RealisticBlood distribution entitlement are **PENDING**. The user explicitly
reserved gameplay automation and visual testing for manual QA.
