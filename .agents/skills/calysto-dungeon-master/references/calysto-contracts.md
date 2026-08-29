# Calysto Dungeon Director V3 runtime contracts

V3 is the sole active authority. Phase 1 and Phase 2 receipts remain immutable
historical evidence, but no pre-V3 DataTable, row struct, preset, alias, or
fallback may participate in runtime generation.

## Protected vendor baseline

- Brain: `/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon`
- Runtime graph: `/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster`
- Mesh source: `/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh`
- Spawner source: `/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner`
- Theme source: `/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme`
- Map: `/Game/Procedural/Maps/DungeonGeneration`
- Door: `/Script/EFProceduralACFURuntime.EFCalystoFloorDoor`
- Population anchor: `/Script/EFProceduralPCGRuntime.EFCalystoPopulationAnchor`

The protected actor baseline is `DungeonSize=(30,30,1)`, candidate density
`0.30`, side paths `0.50`, room sizes `4..8`, and one non-editor
`GenerateOnDemand` PCG component using `PCG_MassiveDungeonMaster`. The editor
PCG component is never seeded or generated at runtime.

## Policy authority

The only active policy asset is:

`/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy`

It must be exactly `UEFCalystoDungeonDirectorPolicy`, with SchemaVersion 3,
GeneratorVersion 3, a stable PolicyId, unique stable catalog/style IDs, valid
soft references, canonical ordering, valid distributions, and a non-empty
`ValidatedDungeonSizes`. Missing, invalid, or drifted policy fails closed.

The authoring script is create-only. Its first success reports exactly the V3
package in measured mutations/saves and zero `/Game/Calysto` mutations. Later
runs validate read-only and report zero measured mutations/saves.

## Immutable generation records

Before travel, the subsystem freezes `FEFCalystoResolvedFloorIntent`, including
identity, policy/ecology hashes, domain seeds, style, traits, exact safe Calysto
scalars, presence rolls, budgets, target counts, and selected catalog IDs.

After PCG/navigation, the PCG owner freezes
`FEFCalystoRealizedFloorManifest`, including canonical anchor topology,
spawn directives, realized counts/budgets, and topology/population/resource/
manifest hashes. `NotifyFloorReady` rejects readiness until the manifest has
been accepted through `NotifyPopulationRealized`.

Replay requires the same intent, manifest, layout, and initial population.
Reroll and advance increment GenerationSerial once. PolicyHash identifies
canonical validated policy content; EcologyHash identifies committed run
memory; IntentHash identifies the pre-PCG decision; ManifestHash identifies
the realized floor.

## Run ecology and intent

Run state is GameInstance-only. Ecology contains five persistent traits
(Scale, Branching, Threat, Abundance, Mystery), recent style/theme history,
pity/cooldowns, performance EMA, last committed floor, revision, and hash.

`FEFCalystoDirectorIntent` contains PreferredStyle plus Scale, Branching,
Threat, Resource, Theme, and Volatility biases. Biases are normalized and
clamped; they never bypass hard caps or validated sizes. Outcome adaptation is
normalized, frozen into the next intent, EMA-smoothed, and capped at ±15%.

## Generation and population

The adapter may alter only allowlisted properties on transient duplicates. The
transient spawner clone contains only the lightweight project anchor. After the
single PCG pass, anchors receive stable IDs from quantized transforms and are
sorted before allocation. Entrance, exit, door-clearance, collision, and
navigation-invalid candidates are excluded. Deterministic grid projection may
complete a shortage; global random navigation points are forbidden.

Maximums are 30×30×1, 25 enemies, 8 loose food actors, 3 chests, 4 loot actors,
4 special events, and 36 initial Director actors. The active policy selects the special-event cap within that
ceiling (default: 2). Zero enemies is valid; zero candidate density is not. The door
remains disabled until:

`PCGComplete && NavigationPathReady && ManifestReady && PopulationReady`.

## Player-outcome bridge

`UProjectCalystoFloorOutcomeSubsystem` is the project-owned, GameInstance-scoped
adapter for player telemetry. It binds synchronously to the Director's
before-Advance delegate, submits exactly one normalized outcome sample, and is
therefore shared by the real ACF floor door and the Development Harness.
Combat compares the realized manifest's initial enemy count against living
actors tagged `EF.Calysto.Enemy`. Survival uses a finite player-health ratio
when the typed project bridge can resolve it. Resources average the available
Hunger and Thirst ratios. Pace starts at Floor Ready and uses a bounded expected
time derived from validated size plus initial enemy count. Missing or invalid
signals are `0.5` neutral; every value is finite and clamped to `0..1`.

## Historical boundary

Do not rewrite Phase 1, Phase 2, Spawner Expansion, or Tattoo/Calysto repair
evidence. New V3 evidence supersedes them only for the active runtime contract.
