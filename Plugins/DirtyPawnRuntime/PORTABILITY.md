# DirtyPawnRuntime Portability

`DirtyPawnRuntime` is a portable character-surface effects plugin. It intentionally has no dependency on EFProjectSystems, ACF, progression systems, hygiene gameplay, decals, atlas pools, or SkinnedDecal.

## Runtime Folder

Copy this plugin folder first:

- `Plugins/DirtyPawnRuntime`

The plugin owns the C++ component, volumes, band model, unified wash compositor, and editor module.

## Required Project Content

The current stable project setup references runtime assets under `/Game/DirtyPawnSystem`. Do not move those assets blindly. For another project, migrate the assets listed in `DirtyPawnRuntime.Portability.json` through Unreal Editor so redirectors and material function dependencies are preserved.

## Safe Integration Rules

- Keep `WashMask` away from clean `ColorIn`, DAZ base textures, and clean normals.
- Keep `BP_Water_Mud` out of unified wash.
- Keep stains visually above `Mud`, `Sand`, and `Snow`.
- Keep `Mud`, `Sand`, and `Snow` capped to three active environmental bands.
- Keep `Dirt`, `Blood`, `Burn`, and `Smear` persistent until Water/Wash.

## Optional Project Bridges

Project-specific systems should depend on `DirtyPawnRuntime`, not the reverse. NoShellForWinter uses `UProjectDirtyPawnEffectsBridgeComponent` in EFProjectSystems to translate surface coverage into project-owned status effects.
