# EF Clothing Morph V3 - Evidence - 2026-08-28

## Status

`CORE PIPELINE, FINAL PIE, PACKAGE, AND PACKAGED STARTUP PASS; EXPANDED AND MANUAL GATES PENDING`

V3's native-first catalog, binding compiler, Editor/Game builds, allowlisted
Blueprint compile, visible HUB PIE smoke, Development cook/package, and packaged
D3D12/SM6 HUB startup have passed for the single Female fixture. Native mesh
authoring operations, final live Director UI inspection, Golden Palace section
visibility/restore, Story Selection behavior, geometric zero-penetration,
multi-garment scale, and performance remain qualified `PENDING` items.

Workspace: `D:/Projects UE5/NoShellForWinter`

Branch: `feature/efclothing-v3-native-first`

Rollback checkpoint: `cef0af32bab5114c0144dc03631caaf333bf1143`
(`checkpoint(clothing): preserve V2 before native-first V3`)

Tag: `efclothing-v2-native-first-checkpoint-20260828`

Implementation commit: `65c7ec9`
(`feat(clothing): replace generated fit with native-first V3`)

## Implemented contract

- Plugin version: `3.0.0`.
- One public Director:
  `/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`.
- Director schema/ID: `4` / `EFClothingMorphV3`.
- One enabled fixture: `UnderWearPanty_Female`.
- Exact source:
  `/Game/DazToUnreal/UnderWearPanty/UnderWearPanty`.
- Exact body surface: `/Game/DazToUnreal/Female/Female`.
- Internal registry:
  `/EFClothingMorph/_Internal/Compiled/V3/DA_EFClothingFitRegistry`.
- Binding compiler/schema: `27` / `7`.
- Active registry payload: one native-source binding, zero fit profiles, and no
  fitted Skeletal Mesh.
- Runtime values are authored per garment and excluded from the compile
  fingerprint.
- V3 base clearance and compiled reserve are both `0.0 cm`; this is recorded in
  the final compiler receipts rather than inferred from runtime defaults.
- The source mesh remains authoritative; V3 does not assign a replacement mesh,
  `EF_AutoFit`, leader pose, or replacement skin-weight profile.
- `Genesis9_GP_Torso` is the data-defined Golden Palace body-section exclusion.
  Its live hide/restore behavior is not promoted to PASS without a dedicated
  runtime assertion.
- The seven V26 generated assets remain present for rollback. They are inactive
  and excluded from the final V3 cook; permanent deletion remains `PENDING`.

## Gate summary

| Gate | Status | Evidence |
|---|---|---|
| Target workspace and rollback point | PASS | Branch and checkpoint above |
| Public V3 Director migration | PASS | `Saved/ClothingMorphV3QA/director_migration_20260828_052830.json` |
| Static English public UI/source audit | PASS | Targeted scan of `Plugins/EFClothingMorph/Source` and `Tools/ClothingMorphV3` found no Spanish public labels or tool text |
| Final live Director UI inspection | PENDING | Unreal MCP/editor unavailable at evidence close |
| Binding-only catalog compile | PASS | `Saved/ClothingMorphV3QA/compiler_receipt_20260828_064139.json` |
| Zero hidden clearance/reserve contract | PASS | LOD pair fields in `compiler_receipt_20260828_063610.json` and final `064139` receipt are both `0.0 cm` |
| Automated V3 orphan-binding cleanup | PASS | `compiler_receipt_20260828_063511.json` audited and deleted only the unreferenced legacy `F07...` V3 binding; final compile retains `6C87...` |
| C++ Refresh Binding cleanup integration | PASS (qualified) | The common editor gate compiled and the later fresh PIE passed; its positive stale-binding deletion branch was not separately forced |
| V26 runtime and cook isolation | PASS | Active registry uses V3 only; `DefaultGame.ini` always-cooks V3; final manifest contains no V26 compiled path |
| Permanent V26 asset deletion | PENDING | Seven V26 files remain intentionally retained; live Asset Registry deletion audit unavailable |
| Protected inputs during Director migration | PASS | Migration receipt reports `protected_inputs_unchanged=true` |
| Protected inputs during final catalog compile | PASS | Final compiler receipt reports `protected_inputs_unchanged=true` |
| UE 5.8 Editor Development build | PASS | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; final `NoShellForWinterEditor.target` timestamp `2026-08-28 06:58:39 -05:00` |
| UE 5.8 Game Development/package build | PASS | Final Development package archive `Development_20260828_064206` |
| Allowlisted Blueprint compile | PASS | `Saved/ClothingMorphV3QA/V3BlueprintCompile_20260828_0543.log`: `0 errors`, `0 warnings`, `0 blueprints that failed to load` |
| Visible D3D12/SM6 HUB PIE smoke | PASS | `Saved/ClothingMorphV3QA/RuntimeSmoke_20260828_065156/Summary.json` |
| Gameplay screenshot capture | PASS | Four 1600x900 captures in the final runtime-smoke directory |
| Human visual acceptance / zero visible clipping | PENDING | Final receipt explicitly records `PENDING_HUMAN_REVIEW`; screenshots are evidence, not an automated geometric verdict |
| HUB Character Creation startup regression | PASS | Absent from all 430 observations in the final HUB PIE receipt |
| Story Selection automatic Character Creation | PENDING | HUB-only smoke records `NOT_TESTED_NO_CLAIM` |
| Native Offset/Create Shell operation, Undo, save/reopen | PENDING | Implementation exists, but the final evidence set did not run geometry-writing authoring buttons |
| Golden Palace section hide and restore | PENDING | Data/code path exists; final smoke did not assert section visibility transitions |
| Multi-garment, full morph/LOD/Chaos, and performance certification | PENDING | Final smoke exercised one Female garment at LOD0 in Idle/walk only |
| Live Unreal MCP inspection at evidence close | PENDING | Editor/server unavailable at `http://127.0.0.1:8000/mcp` |
| Cook and Development package | PASS | `Saved/Migration/CalystoDungeonDirectorV4/Packages/Development_20260828_064206` |
| Packaged HUB startup | PASS | `Saved/ClothingMorphV3QA/PackagedStartup_20260828_065110/Summary.json` |
| Read-only source-project comparison | PENDING | `D:/Projects UE5/LustAsDeadlySin` was not present on this machine |
| Historical global protected baseline | PENDING | Final receipt still contains 90 pre-existing mismatches; exact V3 before/after mismatch-set delta is zero |

## Catalog compiler evidence

Final pre-package receipt:
`Saved/ClothingMorphV3QA/compiler_receipt_20260828_064139.json`

Supporting zero-clearance receipt:
`Saved/ClothingMorphV3QA/compiler_receipt_20260828_063610.json`

Result: `UE58_EF_CLOTHING_MORPH_V3_CATALOG_COMPILE_PASS`.

Measured catalog equality:

```text
enabled rows = 1
compiled rows = 1
valid bindings = 1
registry native bindings = 1
registry fit profiles = 0
reused fresh rows = 1
protected inputs unchanged = true
```

The final active binding is:

`/EFClothingMorph/_Internal/Compiled/V3/DA_UnderWearPanty_Female_1D108A5B_Female_1FCC5E1D_EFV3Surface_6C87E7D8469CDE205F3D6582E1B7E6E6`

Its one certified LOD0/LOD0 pair records:

```text
garment render vertices = 4,466
body render vertices = 47,031
collision-only vertices = 3,684
preserve-upstream vertices = 782
witnesses = 20,470
invalid anchors = 0
base clearance = 0.0 cm
compiled reserve = 0.0 cm
fitted_mesh = empty
```

The command-line cleanup ran before the final compile. Receipt
`compiler_receipt_20260828_063511.json` records an Asset Registry/editor
reference audit of the obsolete `F07...` V3 binding, no blocking referencers,
successful deletion, and no remaining orphan. The same bounded cleanup policy
is integrated into the C++ editor gate used by **Refresh Binding** and pre-PIE;
that code compiled and a later fresh pre-PIE gate passed. The positive deletion
branch of the C++ implementation and a direct live button click were not
separately forced, so those narrower claims remain qualified.

## Runtime PIE evidence

Summary:
`Saved/ClothingMorphV3QA/RuntimeSmoke_20260828_065156/Summary.json`

Result: `UE58_EF_CLOTHING_MORPH_V3_RUNTIME_SMOKE_PASS`.

The harness used the real ACF world pickup/equip path on `/Game/_Game/Hub/HUB`
in a visible D3D12/SM6 editor session. It recorded:

- `PASS_EXACT_SOURCE_GARMENT_READY_NO_FITTED_NO_EF_AUTOFIT`.
- `PASS_EXACT_SOURCE_READY_NEVER_HIDDEN_WHILE_EXPECTED` across 338 visibility
  samples, all in `Ready`.
- `PASS_CLEARANCE_AND_INFLATE_0_TO_0_2_TO_0_READY_NO_SWAP`; both per-component
  controls changed independently from `0.0 -> 0.2 -> 0.0 cm`, with no mesh swap
  or recompile.
- `PASS_3_REAL_ACF_UNEQUIP_REEQUIP_READY_CYCLES` with the original item GUID and
  source mesh preserved.
- Idle and walk coverage, including measured walk velocity of `225 cm/s`.
- Character Creation absent from all 430 observed HUB ticks.
- No assets saved, protected assets unchanged, Git state unchanged, clean
  editor self-exit, and zero critical log matches.

Captured views:

- `underwearpanty_female_01_front_idle.png`
- `underwearpanty_female_02_back_idle.png`
- `underwearpanty_female_03_inferior_idle.png`
- `underwearpanty_female_04_right_walk.png`

These captures and state samples establish that the exact source remained
visible and `Ready` for the tested sequence. The final receipt deliberately
records `visual_review=PENDING_HUMAN_REVIEW`. The smoke did not use GPU geometry
readback and therefore does not claim triangle-level zero penetration, full
animation/morph coverage, performance certification, or a solution inside the
explicitly excluded crotch anatomy domain.

## Blueprint and build evidence

Blueprint log: `Saved/ClothingMorphV3QA/V3BlueprintCompile_20260828_0543.log`.

The extensionless allowlist compiled exactly:

- `/Game/FullSample/Player.Player`
- `/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar.BP_TSChar`
- `/Game/_Game/Clothes/Panty.Panty`

The commandlet summary was:

```text
Compiling Completed with 0 errors and 0 warnings and 0 blueprints that failed to load.
```

The project-owned UE 5.8 build wrappers were used so the DazToUnreal receipt
contract remained intact. The final Editor target was rebuilt after the common
C++ refresh/cleanup change. The Development Game build wrapper passed before
the package; AutomationTool then cooked, staged, packaged, and archived that
validated target.

## Cook/package and packaged runtime evidence

The project-owned Asset Manager configuration contains the UE 5.8
`GameFeatureData` rule with `CookRule=AlwaysCook`. HUB is explicitly listed in
`MapsToCook`, and the V3 compiled root is explicitly always-cooked. V26 is not
an always-cook root.

The final invocation completed cook, stage, Pak/IoStore, package, and archive:

`Saved/Migration/CalystoDungeonDirectorV4/Packages/Development_20260828_064206`

`Saved/Logs/UnrealPak.log` records 5,096 input packages and 8,089 total IoStore
chunks. `Manifest_UFSFiles_Win64.txt` contains the V3 registry and exact active
`6C87...` binding. It contains neither the obsolete exact `F07...` V3 binding
nor a `/Compiled/V26/` path.

Packaged startup receipt:

`Saved/ClothingMorphV3QA/PackagedStartup_20260828_065110/Summary.json`

Result: `UE58_EF_CLOTHING_MORPH_V3_PACKAGED_STARTUP_PASS`.

The archived executable launched `/Game/_Game/Hub/HUB` under D3D12/SM6,
reported `map_loaded=true`, remained alive for the ten-second observation
window, and produced zero critical log matches. The harness then closed only
the process it started (`OWNED_PROCESS_EXITED`). The receipt does not contain a
numeric map-load duration. This is a startup/package gate, not a packaged
garment-equip or packaged visual certificate.

## Integrity qualification

The migration, compiler, and final runtime smoke each recorded unchanged
protected inputs within their own before/after window. This is a `PASS` for the
V3 delta and covers Female, Male, Multiple, UnderWearPanty, Player, and the
configured compatibility reference within those operation windows.

It does not erase the older global baseline debt. The pre-V3 receipt
`Saved/ClothingMorphV3QA/Protected_PreBuild_20260828_045731.json` and final
receipt `Saved/ClothingMorphV3QA/Protected_Final_20260828_0654.json` both report
90 mismatches against `Phase0_Target_Invariant_Hashes.json`. An exact comparison
of their mismatch records produced zero added, removed, or changed entries:

```text
V3 protected before/after mismatch-set delta = 0: PASS
Historical project-wide baseline equality = 90 mismatches: PENDING
DazToUnreal baseline set = 213/213 files, 0 mismatches: PASS
```

The final global rehash result itself remains `FAIL` because the 90 historical
mismatches still exist. It must not be described as absolute protected-baseline
equality; the supported claim is strictly that V3 introduced no new delta.
