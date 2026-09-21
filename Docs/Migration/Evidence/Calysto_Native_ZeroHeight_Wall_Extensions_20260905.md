# Native zero-height wall extensions — 2026-09-05

Scope: writable target `D:/Projects UE5/NoShellForWinter`, UE 5.8. The source project was not accessed. This records read-only native MCP graph inspection and the resulting project-owned compatibility contract. Build, runtime provenance matching, traversal, visual acceptance and packaging remain **PENDING** for the new contract until their independent receipts pass.

## Observed graph evidence

Server: `http://127.0.0.1:8000/mcp`. The connection was rediscovered with `list_toolsets`; `editor_toolset.toolsets.object.ObjectTools` was described before `get_properties`/`list_properties`. No asset setter, save, compile, PIE control or graph mutation was called during this inspection.

Graph: `/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh.PCG_SetDungeonMesh`.

| Node | Native setting or connected input |
| --- | --- |
| `GetActorProperty_9` | `PropertyName = Wall Height` |
| `GetActorProperty_7` | `PropertyName = Initial Wall Height` |
| `AttributeMathsOp_11` | Divide: `InA` from `_9.Out`, `InB` from `_7.Out` |
| `CreateAttribute_14` | Double constant `1` |
| `AttributeMathsOp_13` | Subtract: `InA` from `_11.Out`, `InB` from `CreateAttribute_14.Out` |
| `CreateAttribute_1` | Double constant `1`, connected to X and Y below |
| `MakeVectorAttribute_15` | Vector from three values; Z from `_13.Out` |
| `NamedRerouteDeclaration_12` | Title `Z Scale Multiplicator`; input from `MakeVectorAttribute_15.Out` |
| `NamedRerouteUsage_16` | Declaration above; output overrides both `TransformPoints_8.ScaleMin` and `.ScaleMax` |
| `GetActorProperty_5` | `PropertyName = Initial Wall Height` |
| `MakeVectorAttribute_6` | Z from `GetActorProperty_5.Out`; output overrides `TransformPoints_1.OffsetMin` and `.OffsetMax` |
| `TransformPoints_1` | Input from `ToPoint_33.Out`; relative offset and scale |
| `TransformPoints_8` | Input from `TransformPoints_1.Out`; absolute scale; relative zero additional offset |
| `AddTags_0` | Input from `TransformPoints_8.Out`; `TagsToAdd = NoSocket` |

All three property readers select the **Original** actor, exclude children, and do not select components. The arithmetic inputs use `@Last`; arithmetic/vector outputs use `@Source`.

The observed native formula is:

```text
upper_extension_scale = (1, 1, WallHeight / InitialWallHeight - 1)
upper_extension_vertical_offset = InitialWallHeight
```

Thus equal positive runtime heights produce a zero-height upper extension. This is an upper-wall extension rule, not a general permission for degenerate structure.

The final native wall path is:

```text
TransformPoints_8 -> AddTags_0(NoSocket)
  -> MatchAndSetAttributes_3 -> AttributePartition_3 -> Loop_1
  -> AttributeFilter_24 -> wall material branches / StaticMeshSpawner_0, _4, _5
```

`MatchAndSetAttributes_3` also receives a separate normal-wall stream from `Reroute_0`. Therefore the final loop output alone does not prove upper-extension provenance. The wall spawner selector's native attribute is `Mesh`. `Loop_1` invokes the simple object-transform graph; the existing owned cooked compatibility redirects that exact call to its validated compatible version. The other wall loop, `Loop_3`, invokes `/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon.PCG_ObjectTransformSimpleDungeon`; its transform uses relative scale and its later tag is `NoWallSpawn`.

## Runtime evidence and its limits

The root's runtime captures are retained at:

- `Saved/Migration/CalystoDungeonDirectorV7/Structure_20260905_WallTransform2/structure.json`: zero-height wall instance transforms among generated native walls.
- `Saved/Migration/CalystoDungeonDirectorV7/Structure_20260905_WallHeights/structure.json`: runtime `Wall Height = 300`, `Initial Wall Height = 300`; zero-height walls at Z `300.5`, scale `(1.01, 1, 0)`. The generated component tags contain only `PCG Generated Component` and `PCG`; upper and base wall instances share an ISM component.

The separately inspected vendor Blueprint CDO reads `Wall Height = 600`, `Initial Wall Height = 0`. Those defaults do **not** establish runtime dimensions and are never used to authorize the exception. The observed `300.5` transform also includes later native transforms; it is not hardcoded into classification.

## Project-owned contract

`FEFCalystoNativeAdapter::CanContainZeroHeightWallExtensions` requires the prepared runtime actor/component/graph, finite positive actual heights, equality of those heights, and the checked formula, selector and connection contract. Unequal valid heights return false without a configuration error.

Composition clones only the existing construction subgraph transiently and adds `EF Native Upper Wall Extensions` from its final `Loop_1.Out` through the root graph. Existing construction, material and spawning connections remain in place. The existing strongly retained root graph owns the added subgraph through its native subgraph reference. Required connections are checked before the sole `GenerateLocal` request.

The bounded reader accepts exact zero-Z points only when both the capability and the `NoSocket` data tag are present, with nonzero X/Y scale, finite transform and an actual resident `Mesh` reference. It preserves duplicate records. `FEFCalystoNativeResult::NativeZeroHeightWallExtensions` exposes each mesh and world transform for one-to-one matching against owned generated ISM instances. A missing tag, missing mesh, unknown zero, unmatched duplicate or unmatched record cannot silently obtain an exception.

The provider/structural verifier must mark exact matching instance indices, retain those instances as native output, and exclude their zero volume from structural bounds and valid-wall counts. Floor/roof degenerates and all unproven zero-height wall instances remain invalid. A graph inspection or unit pass alone does not prove runtime point tags survived or that every generated instance matched.

Added focused native test: `NoShellForWinter.CalystoDungeon.Director.Native.ZeroHeightWallExtensionContract`. It covers the loaded native formula, valid unequal heights, invalid numeric inputs, altered mathematics, missing edges, duplicate provenance cardinality and missing capability/tag rejection. Mutation tests operate on transient copies only.
