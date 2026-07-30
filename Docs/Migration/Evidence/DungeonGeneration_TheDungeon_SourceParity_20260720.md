# DungeonGeneration / TheDungeon source parity — 2026-07-20

## Scope

- Reference map: legacy private build, read-only; local path intentionally omitted.
- Target, repaired in UE 5.8: `Content/Procedural/Maps/DungeonGeneration.umap`
- Requested actor: `TheDungeon`
- Independent target asset left untouched: `/Game/_Game/TheDungeon`

## Result

`PASS` for source-map structural parity and focused UE 5.8 PIE startup.

The source map contains no placed `TheDungeon` or `BP_MassiveDungeon` actor. It contains exactly:

1. `PlayerStart_0`
2. `NavMeshBoundsVolume_0`
3. `RecastNavMesh-Default`
4. `PCGWorldActor_0`

The pre-repair target contained 93 actors: the same four baseline actors plus a placed
`/Game/_Game/TheDungeon.TheDungeon_C` and 88 other generated/baked dungeon actors.
The UE 5.8 repair removed those 89 extras and saved only the target map. The final target
contains the same four actor keys, classes, names, labels, transforms and components as
the isolated UE 5.7 source inspection. Both inspections report zero `/Game` dependencies.

Binary identity is intentionally not asserted across engine serialization versions:

- Source UE 5.7.4: 58,016 bytes; SHA-256
  `B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F`
- Target UE 5.8.0: 58,272 bytes; SHA-256
  `E004C376D70E54F8D3A43647ED931738037FF77A4748CEDA6A4BD4906ED4F4E5`

This is exact semantic map parity with an engine-native UE 5.8 serialization.

## Validation

- Source read-only actor inspection: `PASS`, four actors, no save request, source hash unchanged.
- UE 5.8 repair/reload: `PASS`, 93 -> 4 actors, 89 removed, zero `/Game` dependencies, clean map package after reload.
- Independent UE 5.8 read-only reinspection: `PASS`, four actors and no dirty map packages.
- Relevant Blueprint: `/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon` is
  `BS_UP_TO_DATE`, parent `/Script/Engine.Actor`.
- Aggregate Calysto/Procedural compile: 74/74 processed, but overall `FAIL` remains because
  the unrelated inherited `/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController`
  is `BS_ERROR`.
- Focused PIE startup: `PASS`; one runtime `BP_MassiveDungeonRuntime`, zero runtime
  `TheDungeon`, player pawn present, two navigation bounds and one RecastNavMesh.
- UE 5.8 Editor build: `PASS`.
- UE 5.8 Game build: `PASS`.
- Source read-only post-gate: `PASS`.
- ACFU 4.3.5 protected hash set: `PASS`, 5,043/5,043 files.
- DazToUnreal 5.8.0.491 plugin protected hash set: `PASS`, 213/213 files.

The project-wide protected-assets gate remains `FAIL` independently of this map repair:
the existing baseline comparison reports changes in `Player`, `Female`, `Multiple`,
`Male`, and other target Daz assets. The repair script wrote only
`DungeonGeneration.umap`; none of those protected assets was in its save scope.

## Pending gates

- Full StartPoint discovery and complete PCG generation/cleanup: `PENDING`.
- Visible editor/MCP visual QA: `PENDING`; the UE 5.8 D3D12 editor launch exhausted
  available video memory, so validation continued through NullRHI commandlets.
- Cook/cooked-manifest: `PENDING`.
- Package and packaged-runtime validation: `PENDING`.

No full migration or procedural-runtime `PASS` is claimed by this focused repair.

## Evidence

- `Saved/Migration/DungeonGenerationParity/Source57.json`
- `Saved/Migration/DungeonGenerationParity/Target58_Before.json`
- `Saved/Migration/DungeonGenerationParity/Repair58.json`
- `Saved/Migration/DungeonGenerationParity/Target58_After.json`
- `Saved/Migration/DungeonGenerationParity/BlueprintCompile58.json`
- `Saved/Migration/DungeonGenerationParity/SourceParityPIE58.json`
- `Saved/Migration/DungeonGenerationParity/SourceReadOnlyAfter.json`
- `Saved/Migration/DungeonGenerationParity/ProtectedInvariantsAfter.json`
- `Saved/Migration/DungeonGenerationParity/Backup/DungeonGeneration_BeforeSourceParity58.umap`
- `Saved/Migration/Logs/DungeonGenerationParity_Source57.log`
- `Saved/Migration/Logs/DungeonGenerationParity_Target58_Before.log`
- `Saved/Migration/Logs/DungeonGenerationParity_Repair58.log`
- `Saved/Migration/Logs/DungeonGenerationParity_Target58_After.log`
- `Saved/Migration/Logs/DungeonGenerationParity_BlueprintCompile58.log`
- `Saved/Migration/Logs/DungeonGenerationParity_SourceParityPIE58.log`
- `Saved/Migration/Logs/DungeonGenerationParity_EditorBuild58.log`
- `Saved/Migration/Logs/DungeonGenerationParity_GameBuild58.log`

## Reproducible tools

- `Tools/Migration/Inspect-DungeonGenerationActorParity.py`
- `Tools/Migration/Repair-DungeonGenerationSourceParity58.py`
- `Tools/Migration/Validate-DungeonGenerationSourceParityPIE58.py`
