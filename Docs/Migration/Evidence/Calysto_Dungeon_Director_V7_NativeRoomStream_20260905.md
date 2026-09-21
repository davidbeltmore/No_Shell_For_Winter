# Calysto native room stream diagnosis — 2026-09-05

Status: **CONFIRMED graph defect; repair PENDING cold build and real traversal.**
This is a shared native-adapter repair during V7 migration. V6 policy/data remain
immutable migration inputs; this report does not establish V7 authority.

## Reproduced symptom and exact ownership

The root agent's settled probe for topology seed `1779679224` reports native
Start `(-1350,-4050,0)`, End `(-1350,3450,0)`, but only 85 floor instances with
combined Y bounds `[-3450,2250]`. Both markers lie outside generated floor
coverage. Navigation bounds alone cannot recreate those missing structural rooms.

Probe artifacts are under
`Saved/Migration/CalystoDungeonDirectorV7/EntryProbe_20260905_043523/`,
`EntryProbe_20260905_043633/`, and `EntryProbe_20260905_043923/`; consult each
`evidence.json` for its exact run/seed/state, rather than treating directory order
as seed identity. Root owns the full runtime diagnosis and second-seed evidence.

Writable target: `D:/Projects UE5/NoShellForWinter`.
Protected native assets inspected: `PCG_MassiveDungeonShape`,
`PCG_MassiveDungeonMaster`, and `PDA_RoomMeshes` under `/Game/Calysto/Dungeon`.
No vendor asset or loaded graph was edited or saved during this investigation.

## Live read-only graph proof

MCP endpoint: `http://127.0.0.1:8000/mcp`. Tools were discovered and described
before use. Read-only `ObjectTools.get_properties`, `get_class` and
`list_properties` calls were batched using the documented programmatic tool.
Each graph walk was bounded to seven seconds; transport deadline was ten seconds.

Actual native edges observed in the loaded editor:

| Producer | Consumer | Meaning |
|---|---|---|
| Shape `MatchAndSetAttributes_26.Out` | `MergePoints_48.In` | Original native room metadata assignment |
| Shape `MergePoints_48.Out` | `NamedRerouteDeclaration_34.In` | Declaration title is **All Used Rooms** |
| `All Used Rooms` usage `NamedRerouteUsage_42.Out` | `MergePoints_4.In` | Feeds Shape output **All Rooms** |
| `All Used Rooms` usage `NamedRerouteUsage_2.Out` | `AttributePartition_48.In` -> `Loop_44.In` | Feeds native room-spline construction |
| Master `Subgraph_0.All Rooms` | `Subgraph_2.All Rooms`, `Subgraph_3.All Rooms`, root output | Shape output drives material assignment and native room opportunities |
| Master `Subgraph_0.Floor/Wall/AllRoof` | `Subgraph_2` (`PCG_SetRoomTheme`) | Structural surfaces receive per-room materials |
| Master `Subgraph_2` | `Subgraph_1` (`PCG_TextureOverride`) -> `Subgraph_43` (`PCG_SetDungeonMesh`) | Native structural instancing remains intact |
| Master `Subgraph_0.Start Room/End Room` | `Subgraph_4` (`PCG_SpawnStartAndEnd`) | Independent progression-marker branch |

The project-owned `PatchShapeGraph` previously removed the native match edge and
connected **only ThemedRooms** to `MergePoints_48`. Its comment incorrectly called
that merge a decoration-only branch. The selector deliberately puts Start, End
and every NoTheme room in **UnthemedRooms**, whose output was disconnected and
even required to be disconnected by the runtime validator. Therefore those rooms
vanished from native room construction while independent Start/End actor spawning
continued. This directly explains the observed marker/structure mismatch.

Source proof:

- `Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/Calysto/EFCalystoPCGRuntimeGraphV6.cpp`,
  `FBuildContext::PatchShapeGraph` and `ValidateRuntimeGraph`.
- `Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/Calysto/EFCalystoAssignRoomThemeV6.cpp`,
  protected flags, `SetAttributes`, and `EmitOutput` predicates for both streams.
- `EFCalystoPCGCookedCompatibility.cpp` preserves the same Shape path and only
  redirects the exact Master mesh subgraph to the project-owned cooked closure;
  it does not restore the omitted room stream.

## Target-only repair

Restore both disjoint selector streams to the existing native merge. Preserve
the ThemedRooms edge. Add seven standard native Add Attribute nodes to the
UnthemedRooms edge before merging:

- `RoomType`: supported UE 5.8 `FSoftObjectPath` to the already-resident native
  `PDA_RoomMeshes.Default__PDA_RoomMeshes_C`.
- Floor/Wall/Roof override flags: false.
- Floor/Wall/Roof material paths: the resolved Style materials.

Live inspection verified all eight CDO decoration arrays are empty:
WallBottom, WallMiddle, WallTop, Floor, CornerBottom, CornerMiddle, CornerTop,
Roof. This neutral schema lets native RoomType property reads succeed without
adding Theme decoration or changing the NoTheme identity. The runtime checks the
resident CDO and every empty-array contract before composition. It performs no
synchronous asset load, CDO mutation, per-room UObject creation or MID creation.
The existing native RoomTheme schema's RoomType property class retains its CDO;
runtime Theme instances additionally reference that class.

Validation now requires exactly two sources at the native merge, the unchanged
merge-to-All Used Rooms edge, and the exact neutral attribute chain. The runtime
graph configuration fingerprint includes this stream revision. There is no
additional GenerateLocal call or replacement topology generator.

## Snapshot and test status

Pre-change graph and test snapshots:
`Saved/Migration/CalystoDungeonDirectorV7/SkillPreflight_20260905_042056/Plugins/EFProcedural/Source/EFProceduralPCGRuntime/Private/`.
The graph snapshot SHA256 is
`8A0C35B86A0EF0BE4A24E11BFE32BC077F2C7E9FBDEAAE7848E2113FB5BB78CC`.
These source files were already untracked in the dirty worktree; Git HEAD is
not a replacement for the exact pre-change snapshot.

Existing focused signature tests were extended, preserving their registered
inventory. They explicitly preload the native neutral schema, assert that the
unthemed stream remains connected, verify its supported soft path, inject a
missing RoomType and disconnected stream, require rejection, then restore the
test-owned graph and require validation to recover. Cooked composition retains
the same Master + Shape clone count.

Cold build, execution of those native assertions, Blueprint compilation, both
failing seeds, real three-floor traversal, material/placement inspection, cook
and packaged validation are **PENDING** until the root agent records them. The
graph trace proves the omission; it does not prove the repair's runtime result
or eliminate additional navigation/transaction defects.
