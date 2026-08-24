---
name: calysto-dungeon-master
description: Inspect, tune, extend, debug, and validate Dungeon Director V3 through the project-owned EFProcedural master plugin without editing Calysto core assets or BP_MassiveDungeon. Use for seeded runs, probabilistic intents, run ecology, population manifests, PCG anchors, replay/reroll/advance, End_Point doors, and the Dungeon Harness menu.
---

# Calysto Dungeon Director V3

Control Calysto only through `EFProcedural`, transient vendor-data clones, and
the single project-owned V3 policy. Treat `/Game/Calysto` and
`BP_MassiveDungeon` as protected vendor core: inspect deeply, never patch or
save them.

## Preflight

1. Work only in `D:/Projects UE5/NoShellForWinter`; the UE 5.7 source is read-only.
2. Invoke `$noshellforwinter-unreal-mcp`, list toolsets once, describe only the relevant toolsets, and keep the first pass read-only.
3. Record the disk SHA-256 and live dirty state of `/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon` and all protected Calysto sources.
4. Run `scripts/Test-CalystoProtectedAssets.ps1` with an explicit baseline under `Saved` before and after mutations.
5. Read [calysto-contracts.md](references/calysto-contracts.md) and [validation-gates.md](references/validation-gates.md) before changing generation, population, policy, or floor travel.

If Unreal MCP is unavailable while the Editor is open, use
`scripts/Invoke-UnrealMcpMetaTool.ps1`. If no live Editor exists, use the
protected launcher and keep live conclusions `PENDING`.

## Ownership

- `EFProceduralRuntime`: GameInstance run ecology, deterministic intent/manifest resolution, V3 policy, travel, replay, reroll, and recovery.
- `EFProceduralPCGRuntime`: Calysto reflection, transient clones, exactly one runtime PCG request, navigation readiness, population anchors, and materialization.
- `EFProceduralACFURuntime`: the generated ACF floor door.
- `EFProjectSystems`: thin `L > Dungeon Harness` commands plus the GameInstance-scoped, bounded player-outcome bridge; it must not own Director state or rolls.
- Policy authority: `/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy`.

Do not create a second dungeon bootstrap, fork `BP_MassiveDungeon`, reuse
`/Game/_Game/TheDungeon`, or add an editor-only Marketplace runtime dependency.

## Run and determinism contract

- A run exists only in the current `UGameInstance`; V3 has no SaveGame persistence.
- New run: Floor 1 / GenerationSerial 1 and fresh ecology.
- Replay: same floor, serial, resolved intent, realized manifest, seeds, ecology, and initial population.
- Reroll: same floor, serial +1, unchanged committed ecology, newly resolved intent/manifest.
- Advance: commit the previous eligible floor once, then floor +1 and serial +1.
- Retry: reuse the frozen intent/manifest without committing ecology twice.
- Debug jump: positive target floor, serial +1, neutral synthetic history, Development only.
- `GenerationSerial` is `1..2147483647`; exhaustion fails closed and requires a new run.

Every draw is derived from RunSeed, FloorNumber, GenerationSerial,
GeneratorVersion, PolicyHash, EcologyHash, DomainId, StableEntityId, and
DrawIndex. Candidate collections must have stable IDs and canonical sorting.
Never use global RNG, container iteration order, or navigation random-point APIs
for authoritative choices.

Use only the typed production APIs: `RequestStartNewRun`,
`RequestStartNewRunWithSeed`, `RequestReplayCurrentFloor`,
`RequestRerollCurrentFloor`, `RequestAdvanceFloor`, and the Development-only
`RequestTravelToFloor`. Pre-V3 aliases, forced presets, exact scalar overrides,
and weight getters are retired.

## V3 policy and safe surface

The sole authority is a `UEFCalystoDungeonDirectorPolicy` Primary Data Asset.
Create it with `Tools/Migration/Create-CalystoDungeonDirectorPolicyV3.py`. The
script creates exactly one asset when absent; when present it validates without
editing or saving it. Normal inspection and PIE must report their measured
`asset_mutations=[]` and `asset_saves=[]`.

Hard runtime limits:

- dungeon X/Y `18..30`, Z exactly `1`, but only sizes in `ValidatedDungeonSizes` may generate;
- candidate anchor density `0.20..0.50`, never zero;
- side-path chance `0.30..0.70`;
- at most 25 enemies, 8 loose food actors, 3 chests, 4 loot actors, 4 special events, and 36 Director actors initially; the active policy selects the special-event cap within that ceiling (default: 2);
- Forge and Shrine are the initial approved Calysto theme topology.

Inputs are probabilities, PERT distributions, budgets, traits, and normalized
intent biases. Never expose exact enemy counts, dungeon sizes, densities, or
theme quotas as gameplay controls. The active floor is immutable; queue intent
for the next replay/reroll/advance boundary.

## Protected integration

Allowed vendor interaction is restricted to transient clones and allowlisted
properties: deterministic runtime PCG seed, validated `DungeonSize`, candidate
anchor density, side-path chance, existing Forge/Shrine weights, the project
population-anchor class in the transient spawner clone, and the project floor
door class in the transient dungeon-mesh clone.

Never mutate or save:

- meshes, materials, structural arrays, tile dimensions, offsets, collision, grammar, room data, or PCG graphs;
- `BP_MassiveDungeon`, `DA_DungeonMesh`, `DA_DemoSpawner`, `DA_RoomTheme`, or another `/Game/Calysto` package;
- Marketplace, Engine, ACFU, DazToUnreal, Player, Female, Male, Multiple, or Frederick assets.

Require zero or one pre-existing dungeon actor, exactly one non-editor runtime
PCG component, the expected graph, and `GenerateOnDemand`. Issue exactly one
`GenerateLocal`; never combine Calysto Randomize and Refresh paths.

After PCG, collect and canonicalize project anchors, validate navigation and
safety exclusions, materialize the frozen manifest, remove anchors, and enable
the door only when PCG, navigation, manifest, and population are all ready.
Failure, timeout, duplicate callbacks, insufficient candidates, or schema drift
must fail closed and use the bounded recovery path.

## Workflow

1. Inspect live CDOs, active policy, protected DataAssets, PCG graph, map, and dirty states.
2. Compare against [calysto-contracts.md](references/calysto-contracts.md); stop on drift.
3. Resolve one immutable `FEFCalystoResolvedFloorIntent` before travel.
4. Generate once through transient Calysto clones.
5. Resolve one immutable `FEFCalystoRealizedFloorManifest` and materialize it deterministically.
6. Enable the ACF door only after the complete readiness conjunction succeeds.
7. Validate fixed seed, exact replay, changed reroll, restart reproduction, and real-door Floors 1–10.
8. Run [validation-gates.md](references/validation-gates.md), re-hash protected invariants, and mark unexecuted gates `PENDING`.

## Debug menu

Keep `L > Dungeon Harness` Development-only and after Appearance. It may queue
Preferred Style plus Scale, Branching, Threat, Resource, Theme, and Volatility
intent. It reports run ecology, budgets, realized counts, and hashes without
per-frame polling. Intent controls keep the menu open; travel closes it. Exact
test scenarios and statistical sampling belong to Development automation, not
the public Harness. Shipping rejects debug jumps, sampling, and intent/debug
overrides while production run operations and the generated door remain active.

## Porting

Port the V3 runtime policy/types/subsystem and PCG adapter first, then supply a
project-specific door bridge and content catalogs. Re-audit all asset paths,
protected hashes, Calysto property names, theme topology, and validated sizes;
never assume this project's assets exist elsewhere.
