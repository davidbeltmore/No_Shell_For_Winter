# Calysto Dungeon Director V5 — Primary Data Asset cutover evidence

Date: `2026-08-31`

## Executive status

Status: `DEVELOPMENT_FINALSTRICT_VALIDATED / MANUAL_QA_READY`.

V5 is being cut over from a rejected normalized DataTable prototype to one
V4-style Primary Data Asset. This document is the evidence ledger for the new
authoring model. It deliberately does not promote build, automation, editor,
PIE, cook, package, smoke, determinism, or visual results produced for the
rejected DataTable prototype.

The active Development build, strict PIE traversal, Blueprint compile, fresh
cook/package, packaged 1-10 DoorToLevel smoke, and native material proof have
now been executed against the exact class and object below. Shipping and the
separate deterministic A/B release gates remain open; they are not implied by
the Development result.

## 2026-09-01 live DoorToLevel repair evidence

The DoorToLevel destination was correct. Its real ACF interaction reached
`/Game/Procedural/Maps/DungeonGeneration`, then the Calysto subsystem returned
to HUB because the configured V5 authority did not yet exist. This was a
cutover-completeness issue, not a DoorToLevel or map-routing defect.

The missing singleton was created through the restricted, create-once factory:

`/Game/_Game/Data/CalystoDungeon/V5/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy`

The create-once result was recorded in the live Editor log at
`2026-09-01 04:30 UTC`. The current read-only validation receipt is
`Saved/Migration/CalystoDungeonDirectorV5/CreateDirectorPolicyV5.json`.
Together, they establish the following verified facts:

- exactly one new Content package was created and saved: the V5 `DA_` asset;
- the protected V4 source remained clean and retained SHA-256
  `C6DDB0A100012108F170BA8F566E17D1EFF60A8FAF3C724904FE091B028739A1`;
- native V5 validation and source-parity validation passed;
- the V5 document contains 78 semantic records and a 32-reference
  `CalystoFloorV5` cook bundle closure;
- the saved V5 package SHA-256 is
  `FBE788802A23207035EF510967C3B49743D4CD42F7B39DD4D85438A10B2EA862`.

The immediate validation-only repeat completed at `2026-09-01 04:34 UTC` in
`validate_existing_read_only` mode with `asset_mutations=[]`,
`asset_saves=[]`, and `save_api_calls=0`.

An in-editor PIE regression then proved the production interaction path:

1. V5 compiled once with gameplay hash
   `9013B934D0EC1D8A9EB41E9F60251E81F7C31376359D4F79E3E56DF8C6AB62D7`.
2. ACF selected the real `DoorToLevel` actor and dispatched `Interact`.
3. Calysto accepted Floor 1 in `DungeonGeneration`.
4. The runtime remained in `DungeonGeneration`; it did not emit the former
   missing-policy or HUB-return failure.

The temporary local automation channel used to invoke the existing create-once
script was disabled after the test. PIE was stopped, and both V4 and V5 policy
assets were clean. This is a focused HUB-to-dungeon regression pass only; it
does not close the full Floors 1-10, package, cook, or visual gates below.

## Exact authorities

The protected V4 rollback source is unchanged in role:

`/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4'/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy'`

The sole intended active V5 authority is:

`/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV5Asset'/Game/_Game/Data/CalystoDungeon/V5/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy'`

The active class is `UEFCalystoDungeonDirectorPolicyV5Asset`, a
`UPrimaryDataAsset`; it is not a `UDataTable`. V4 is an immutable migration
input and explicit rollback option only. V5 may compile a transient V4
compatibility IR, but it must never load the V4 asset as an implicit authority
or fallback.

The former `DT_CalystoDungeonDirectorPolicy` package may remain byte-preserved
for audit while the cutover is completed. It is a rejected prototype: it is not
an active authoring surface, authority, fallback, or permitted cooked Director
policy package.

## V4 audit and V5 design decision

The V4 policy has a mature gameplay hierarchy and rollback value, but authoring
requires moving through several nested arrays to compare safety limits, styles,
themes, category settings, catalogs, and overrides. The normalized DataTable
prototype improved flat scanning but introduced a second, less familiar editing
model that did not meet the required V4-like workflow.

The V5 decision is therefore one document, not one table:

- preserve the familiar V4-style Details hierarchy;
- keep safety ceilings, validated sizes, styles, themes, and nested catalogs
  together in a single Data Asset;
- add explicit V5 identity, migration provenance, deterministic hash schema,
  validation, and cook-closure diagnostics;
- retain transient V4 IR compilation strictly as a runtime compatibility
  boundary;
- preserve V3/V4 assets as protected legacy/rollback inputs without allowing
  an automatic fallback.

## Active V5 authoring model

The V5 Data Asset has English editor-facing categories and labels:

| Details category | Purpose |
|---|---|
| `01 Identity` | Schema, compiler/runtime-generator versions, policy ID, identity-hash schema, and V4 migration provenance. |
| `02 Technical Safety` | Calysto hard ceilings and the validated dungeon-size list. |
| `03 Styles` | The complete V4-shaped style profiles and their nested controls. |
| `04 Themes` | The complete V4-shaped theme profiles and their nested catalogs. |
| `05 Visual Materials` | Director-owned global Dungeon Material Floor/Wall/Roof assignments and fixed Forge/Shrine Room Theme overrides. |
| `06 Cook Validation` | Derived, persisted soft references used to audit the `CalystoFloorV5` asset bundle. |

`Get V5 Semantic Record Count` retains the migration diagnostic of 78
compiler-visible records (`1` policy, `6` profiles, `42` categories, and `29`
unique content records). This is not a literal DataTable-row contract and does
not restrict a future valid extension; validation, canonical semantics, and
cook closure remain authoritative.

The V5 Data Asset is initialized from the frozen V4 source only at creation.
Subsequent script runs are validation-only and must report zero measured asset
mutations and zero asset saves. The V4 source must never be edited, normalized,
or resaved by this process.

## Deterministic runtime boundary

V5 compiles the Data Asset into a transient, validated
`UEFCalystoDungeonDirectorPolicyV4` compatibility IR. Runtime systems consume
that transient IR, not the V4 package. The V5 identity-hash schema is `2`
(`CaseFolded`); legacy V4 rollback remains schema `1` (`Legacy`). Case folding
applies only to canonical identity, deterministic RNG, and ordering tokens; it
does not rewrite authored soft-object paths.

Gameplay and authoring hashes remain distinct. A gameplay-affecting edit must
change the gameplay hash. Editorial metadata may affect only the authoring hash
when the native contract explicitly supports it.

## 2026-09-03 V5 visual-material authority

The V5 Director now owns Calysto visual inputs without creating a second asset
or modifying a vendor package. The Details panel presents one global `Dungeon
Material` group with `Floor Material`, `Wall Material`, and `Roof Material`,
followed by exactly two fixed `Room Theme Material Overrides`: `Forge` and
`Shrine`. Each Room Theme slot can independently inherit the global Dungeon
Material or override it.

At runtime, the Director compiles the visual choices into an immutable plan
whose canonical hash participates in the V5 gameplay hash. Before travel, the
plan preloads every active global and Room Theme material, plus both vendor Room
Type assets. The PCG adapter then validates the exact Calysto schemas, clones
`DA_DungeonMaterial` and `DA_RoomTheme` transiently, writes the Director's
values, enables the transient actor's three global material flags, and only
then calls Calysto's one `GetPiecesShape` boundary. No material instance is
created or swapped after PCG begins, so the Director remains authoritative and
late material hitches fail closed rather than falling back to vendor values.

The first controlled asset upgrade is performed only by
`Tools/Migration/Configure-CalystoDungeonDirectorV5VisualMaterials.py`. It
seeds the existing V5 policy with Calysto's live baseline material
`MI_Stone_Wall_21`, saves only the project-owned V5 Data Asset when those new
fields are empty, and records protected hashes for `BP_MassiveDungeon`,
`DA_DungeonMaterial`, and `DA_RoomTheme`.

## 2026-09-04 Development FinalStrict evidence

The active V5 authority was validated with the current material plan:

- Global Floor, Wall, and Roof: `MI_Stone_Wall_21`.
- Forge Floor, Wall, and Roof overrides: enabled and set to
  `MI_Stone_Wall_21`.
- Shrine Floor, Wall, and Roof overrides: enabled and set to
  `MI_Stone_Wall_21`.

This is a current baseline only; those nine values are authorable from the
single V5 Data Asset. A disabled Forge or Shrine slot explicitly inherits the
corresponding global material.

Verified evidence:

- Editor build after the native visual read-back verifier:
  `Saved/Migration/CalystoDungeonDirectorV5/Build_20260904_055600.stdout.log`.
- Game Development build:
  `Saved/Migration/CalystoDungeonDirectorV5/GameBuild_20260904_060600.stdout.log`.
- Seven native V5 policy tests, all PASS:
  `Saved/Migration/CalystoDungeonDirectorV5/NativeAutomation_20260904_004628/StrictSummary.json`.
- Thirty project-owned V5/door Blueprints compiled without saving assets:
  `Saved/Migration/CalystoDungeonDirectorV5/BlueprintCompile_20260904_060400/StrictSummary.json`.
- Strict editor traversal: twelve generations plus real ACF DoorToLevel travel
  from floors 1 through 10, with twelve native material proofs:
  `Saved/Migration/CalystoDungeonDirectorV5/TraversalPIE_20260904_060000/StrictSummary.json`.
- Fresh Development archive and strict cook closure:
  `Saved/Migration/CalystoDungeonDirectorV5/PackageValidation_20260904_061000/StrictSummary.json`.
- Packaged Development smoke: ten floors, nine real DoorToLevel interactions,
  no HUB return, all checks PASS:
  `Saved/Migration/CalystoDungeonDirectorV5/PackagedSmoke_20260904_061500/StrictSummary.json`.
- Independent native visual-material postflight: all ten engine-log material
  plans match the Director fields, use transient clones, and occur before PCG:
  `Saved/Migration/CalystoDungeonDirectorV5/PackagedRuns/V5VisualFinal_20260904_0615/V5VisualMaterialPostflight.json`.
- Packaged visual capture:
  `Saved/Migration/CalystoDungeonDirectorV5/PackagedRuns/V5VisualCapture_20260904_0617/Screenshot.png`.

The exact Development package is
`Saved/Artifacts/PendingValidation/Development_20260904_060600_V5`.
Its immutable contract binds the current PCG adapter source, smoke runner, and
game binary hashes. The native material verifier emits one
`PASS V5_VISUAL_APPLIED` record after clone read-back and before each
`PCGComplete`; the packaged postflight recorded exactly ten such records.

## Cutover validation ledger

| Gate | Status | Required evidence |
|---|---|---|
| Exact active class/object and one active V5 package | `PASS` | Live Editor confirmed `UEFCalystoDungeonDirectorPolicyV5Asset` at the exact `DA_` object on 2026-09-01. |
| V4 migration-input integrity | `PASS` | Create-once receipt verified the exact source SHA-256 and clean V4 package on 2026-09-01. |
| Create-once V5 authoring | `PASS` | `Create-CalystoDungeonDirectorPolicyV5.py` created and saved only the exact V5 Data Asset on 2026-09-01. |
| Read-only V5 authoring validation | `PASS` | 2026-09-01 validation-only rerun reported no mutations, saves, or save calls. |
| Cook-bundle closure | `PASS` | Development FinalStrict archive independently proved the complete V5 bundle in manifest and IoStore/Pak. |
| V5 visual-material authoring and transient clone application | `PASS` | Strict PIE and packaged 1-10 runs both recorded exact native clone read-back for global/Forge/Shrine materials. |
| Editor/Game Development builds | `PASS` | Fresh post-verifier Editor and Development game build logs are recorded above. |
| Game Shipping build | `PENDING` | Must be rebuilt and validated as a separate release target. |
| V5 native automation | `PASS` | Exactly 7 strict Primary Data Asset tests passed. |
| V4 legacy control | `PENDING` | Exactly 23 strict schema-1 rollback tests after the cutover. |
| Blueprint compile and live PIE traversal | `PASS` | Thirty scoped Blueprints are up-to-date and strict New/Replay/Reroll plus Floors 1-10 traversal passed. |
| Visual QA | `PARTIAL_PASS` | A packaged scene capture and exact ten-floor native material read-back passed; manual variant inspection remains useful whenever the Director's material selections change. |
| Development FinalStrict package/smoke | `PASS` | Fresh archive, strict validator, and natural 1-10 packaged DoorToLevel smoke passed. |
| Development deterministic A/B | `PENDING` | Run a separate A/B receipt comparison when release determinism is being certified. |
| Shipping FinalStrict package/smoke/determinism | `PENDING` | Fresh archive, normal travel proof, and rejected debug-override evidence. |
| Protected-invariant re-hash | `PENDING` | Post-authoring, post-PIE, and post-package records for Calysto, V4, ACFU, DazToUnreal, Player, Female, Multiple, Male, and Frederick. |

Historical results that name
`/Game/_Game/Data/CalystoDungeon/V5/DT_CalystoDungeonDirectorPolicy` are
`NOT_APPLICABLE_TO_ACTIVE_V5_DATA_ASSET`. They may remain archived for
provenance, but they cannot satisfy any row in this ledger.

## Required closeout sequence

1. Verify the writable target, dirty worktree, protected V4 hash, and live
   Editor state through the read-only MCP preflight.
2. Build the active V5 Data Asset runtime/editor classes.
3. Create the exact `DA_CalystoDungeonDirectorPolicy` once, then run its
   validation-only authoring pass.
4. Record live class/object, Details hierarchy, dirty states, bundle closure,
   and protected hashes.
5. Run the native V5 and V4 automation gates, Blueprint compile, PIE, and
   visual QA.
6. Produce fresh Development and Shipping archives, run FinalStrict,
   packaged smoke/determinism, and repeat the protected-invariant hashes.

Until every row is backed by fresh evidence for the exact `DA_` object, V5 is
not a closed delivery. No step authorizes editing or resaving `BP_MassiveDungeon`,
the immutable V4 Data Asset, or any protected Calysto vendor package.
