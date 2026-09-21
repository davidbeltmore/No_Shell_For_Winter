# EF Clothing Morph V4.5 Opaque Material Policy — 2026-08-31

## Scope

- Director: `/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector`
- Project-owned implementation: `Plugins/EFClothingMorph/Source/EFClothingMorphEditor/Private/EFClothingMorphEditorModule.cpp`
- Source project: not opened or modified.
- DazToUnreal plugin mount `/DazToUnreal` and parent `/DazToUnreal/BaseAlphaMaterial`: not modified. Garment instances under `/Game/DazToUnreal` are the intended writable targets.

## Implementation

`ApplyOpaqueGarmentMaterialPolicy` runs through the Director's existing debounced automatic refresh and once during editor startup. It visits every material slot on every registered Clothing Mesh and saves an explicit `bOverride_BlendMode=true`, `BlendMode=BLEND_Opaque` override on project-owned Material Instance Constant assets under `/Game`.

Direct Material assets and assets outside `/Game` fail safely with an editor warning instead of modifying shared or protected content.

## Evidence

| Gate | Result | Evidence |
|---|---|---|
| UE 5.8 editor cold build | PASS | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; UBT result `Succeeded`; Daz receipt repair `PASS` |
| Startup policy execution | PASS | `LogEFClothingMorphEditor`: `automatically forced 5 clothing material instance(s) to Opaque` |
| Catalog binding health | PASS | `enabled=6 valid=6 drafts=0 invalid=0 stale=0 registry=6` |
| Current material overrides | PASS | Six registered garment materials inspected through Unreal MCP; every asset reports `bOverride_BlendMode=true` and `BLEND_Opaque` |
| Persistence | PASS | Director and all six material instances report `is_dirty=false` after automatic saving |
| Automatic PIE | NOT RUN | Explicit user requirement; Unreal MCP reports `IsPIERunning=false` |
| Manual visual QA | PENDING | User must open TattooShop in HUB, equip the garments, and confirm skin/tattoos remain hidden through every covered region |

Snapshot: uncommitted target working tree on 2026-08-31. Existing unrelated EFClothingMorph changes were preserved.
