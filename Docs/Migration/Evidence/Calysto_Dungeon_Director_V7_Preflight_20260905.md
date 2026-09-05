# V7 preflight and failure reproduction, 2026-09-05

V7 implementation and release: **PENDING**. This receipt records preparation
and observed V6 failures; it does not establish V7 acceptance.

Target: `D:/Projects UE5/NoShellForWinter/NoShellForWinter.uproject`, UE 5.8.
Source `D:/Projects UE5/LustAsDeadlySin` remains read-only and was not written.
Snapshot: `Saved/Migration/CalystoDungeonDirectorV7/SkillPreflight_20260905_042056`.
Its `git-head.txt`, `git-status.txt`, `snapshot-files.json` preserve the starting
commit, dirty worktree and pre-edit skill/configuration/V6-asset hashes. The
Calysto graph adapter cpp was added to that snapshot before its first edit.

## Skill and operator preparation

Updated `.agents/skills/calysto-dungeon-master` and the V7 master contract with
the complete locked plan, verified current command/API inventory, bounded timing,
three-consecutive-floor rapid gate, native in-memory probability suite, release
soak and final package gates. The plan helper only generates PLANNED schedules.

Skill validator exited 0. Independent forward review corrected documentation
links, timing inconsistencies, omitted probability laws and final-HUB fixture.
The helper and mock-transport suite passed 19 tests in 14.025 seconds, exit 0;
these are operator-tool tests, **not Director gameplay/probability gates**.
The MCP transport now bounds the whole handshake and headers/JSON/SSE bodies,
and rejects JSON-RPC/tool errors even in Raw mode.

## Live context

The configured MCP connector initially returned a transport error. Local probe
`Test-UnrealMcp.ps1 -TimeoutSec 5` succeeded and native MCP calls subsequently
worked at `http://127.0.0.1:8000/mcp`. Process 8644 opened this target with
`D:/Unreal Engine 5/Library/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe`.
Read-only MCP `EditorAppToolset.IsPIERunning` was false and
`SceneTools.get_current_level` returned `/Game/_Game/Hub/HUB`.
Python preflight found no dirty editor packages. No Content save was requested.

Config/DefaultGame.ini still selects the V6 asset and V6 AssetManager scan.
Current authoring class is EFCalystoDungeonDirectorPolicyV6Asset; V7 has not
replaced runtime authority at this preflight.

## Protected historical discrepancies

`Test-ProtectedInvariants.ps1` exited 1 with 145 pre-existing discrepancies:
ACFU_4_3_5: one length difference; Target_Daz_Assets: 59 length differences,
10 hash differences and 71 additional files; four authoritative asset differences
for Player, Female, Multiple and Male. DazToUnreal plugin files passed (213 files).
See `project-protected-before.json` for exact paths and expected/current hashes.
These are preserved separately and are not silently accepted or rebaselined.

`Test-CalystoProtectedAssets.ps1` captured 13 current Calysto path hashes in
`calysto-protected-before.json`. Its PASS means capture succeeded, not historical
parity, because this invocation did not supply a baseline. BP_MassiveDungeon was
already dirty in Git. It was not saved or patched during these probes.

## Exact known-seed reproduction

Historical log copied byte-preserved to snapshot `original-failing-seeds.log`:
`Saved/Logs/NoShellForWinter-backup-2026.09.05-09.18.22.log`.
It establishes the original Int64 run seed to topology seed mapping:

| Run seed | Topology seed | Live observation |
|---|---:|---|
| 2959332854660340481 | 1779679224 | Settled missing Start/End floor; two TOPOLOGY_REPAIR_START_INVALID failures; HUB return at 34.5 s |
| 2930986289486100775 | 1190737158 | Settled missing Start/End floor and null nav projection; HUB return at 19.796 s |

Probe: `Tools/Migration/Probe-CalystoEntry58.py`, executed in the current editor
console then standard MCP StartPIE. It calls the public seeded-run API, samples
actual components and stops its owned PIE on observed return or a 40-second
diagnostic cap. This is a reproduction fixture, **not natural-door traversal**.
The first tool attempt failed on Python object discovery and stopped in 3.641 s;
that error remains recorded in `EntryProbe_20260905_043523/evidence.json`.
After the probe correction, `EntryProbe_20260905_043633/evidence.json` captured
the first seed without a probe exception. The second seed has its own subsequent
timestamped EntryProbe directory. Both successful captures report no dirty
packages before/after and `gameplay_verified=false`.

First seed: native BP_MassiveDungeonRuntime owned nine static mesh instance
components: Floor 85, Roof 85, Wall 52+14, WallDoor 4+4, Frame 4 and RampBevel
112+36. Floors used BlockAll, QueryAndPhysics and navigation relevance. Settled
floor bounds center was (-1350,-600,0), extent (1200,2850,0). Start at
(-1350,-4050,0) and End at (-1350,3450,0) were outside that floor union.
Downward traces found no blocking floor at either marker after geometry settled.

Second seed: Start (150,-4050,0), End (150,3750,0); floor traces and nav projections
returned null. The existing map navigation volume had extent (20000,20000,100).
The separately spawned runtime NavMeshBoundsVolume_1 had center
(-721.517085,-148.076920,512.990845) and **zero extent**, with only BrushComponent0.
Relevant Recast geometry existed for surviving inner rooms. This confirms that
an idle nav queue and a spawned/scaled volume do not prove entry coverage.

Exact graph room-stream root-cause proof and correction are recorded separately
in the NativeRoomStream evidence receipt. Full structural instance transforms,
capsule sweeps, valid routes, actual material boundaries and natural three-floor
traversal remain subsequent gates; this diagnostic samples transforms only.

## Recorded hardware

AMD Ryzen 7 5800X, 8 cores / 16 threads; NVIDIA GeForce RTX 5070 Ti,
driver 32.0.16.1664; 51,460,853,760 bytes physical RAM. Cold/warm PSO, hitch,
latency P95 and retained-memory acceptance remain PENDING.
