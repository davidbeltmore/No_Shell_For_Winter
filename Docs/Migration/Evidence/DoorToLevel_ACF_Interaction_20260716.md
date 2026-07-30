# DoorToLevel native ACF interaction repair

Date: 2026-07-16 (America/Bogota)

## Scope

- The legacy private build remained read-only; its identity is intentionally omitted.
- Target asset: `/Game/Procedural/DoorToLevel`.
- Target placement: the single map-owned actor in `/Game/_Game/Hub/HUB`.
- Runtime destination: `/Game/Procedural/Maps/DungeonGeneration`.

## Root cause and repair

The first UE 5.8 rebuild used `WorldStatic` for the Door sphere. ACFU 4.3.5's
`UACFInteractionComponent` detects `ECC_Pawn` by default, so that actor never
entered the player's interactable set.

`AProjectLevelDoor` now keeps the source Blueprint's collision and component
contract: `Sphere` is the 100 cm root, uses `QueryOnly`, object type `Pawn`, and
overlaps all channels. `StaticMesh` is attached to that sphere at
`(0, 60, -110)`, yaw `-90`, with `BlockAllDynamic`. The thin Blueprint remains
`/Game/Procedural/DoorToLevel.DoorToLevel_C`, parented to
`/Script/EFLevelFlowRuntime.ProjectLevelDoor`, and owns no duplicate travel
EventGraph.

HUB contains exactly one unattached/map-owned instance at location
`(1250, 200, 110)`, rotation `(0, 0, 0)`, scale `(1, 1, 1)`. No player or pawn
spawn path owns the Door.

## Evidence

- Cold `NoShellForWinterEditor Win64 Development` build: PASS.
- Blueprint/HUB repair receipt:
  `Saved/Migration/CalystoProcedural/DoorToLevelInteractionRepair58.json` =
  `UE58_DOOR_TO_LEVEL_INTERACTION_REPAIR_PASS`.
- Real ACF PIE receipt:
  `Saved/Migration/CalystoProcedural/DoorToLevelInteractionPIE58.json` =
  `UE58_DOOR_TO_LEVEL_INTERACTION_PIE_PASS`.
- PIE observed one Door in HUB, the player's real
  `UACFInteractionComponent`, physical detector overlap, three stable samples
  selecting Door as the best interactable, one `Interact("")` call, and three
  stable samples in `DungeonGeneration`. The destination contained zero Door
  actors.
- Runtime log recorded `AProjectLevelDoor` opening the exact destination and
  EFProcedural completing PCG generation/navmesh preparation.
- Post-repair protected re-hash: ACFU 5,043/5,043 and DazToUnreal plugin
  213/213 match baseline. The pre-existing 67 target Daz-asset and two
  authoritative-asset mismatch signatures are unchanged.
- Source HEAD remains `8808ee6493b6c8e73b58fca30d9d5f1c1bca77b4`; its pre-existing tracked
  status signature is unchanged.

The runtime test loaded with `BpGeneratorUltimate` and MCP disabled. There is
no UBG runtime dependency.

## Remaining gates

Quest-target/map-marker runtime behavior, user-owned visual QA, focused cook,
package, and packaged-runtime validation remain `PENDING`; this receipt only
promotes Door interaction and travel.
