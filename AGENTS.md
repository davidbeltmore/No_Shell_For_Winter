# NoShellForWinter UE 5.8 Migration Rules

## Project identity

LustAsDeadlySin and NoShellForWinter are the same game.
NoShellForWinter is the UE 5.8 update of LustAsDeadlySin.

Source, read-only:

`D:\Projects UE5\LustAsDeadlySin`

Target, writable:

`D:\Projects UE5\NoShellForWinter`

## Authoritative target components

- Unreal Engine 5.8.
- ACF Ultimate 4.3.5 already installed in target.
- Current target DazToUnreal.
- Current `/Game/FullSample/Player.Player`.
- Frederick.
- Current `Multiple`, `Female`, and `Male` meshes.
- Current Female assignment on Player.

## Never do

- Never modify the source project.
- Never resave the source with UE 5.8.
- Never overwrite target ACFU with the old plugin.
- Never overwrite target DazToUnreal.
- Never launch or leave an editor receipt with `DazToUnreal` or `EFCharacterCreationDazBridge` disabled. The current precompiled Daz plugin may require a temporary UBT-only exclusion; use `Tools/Migration/Build-NoShellForWinterEditor58.ps1`, which repairs the receipt immediately, and launch through `Tools/Migration/Launch-NoShellForWinterEditor58.ps1`.
- Never directly modify Marketplace or Engine plugins.
- Never bulk-copy Content, Config, Plugins, Saved, Intermediate or Binaries.
- Never overwrite `/Game/FullSample` or `Player` wholesale.
- Never declare success without build, Blueprint compile, PIE, visual QA, cook and packaged validation.

## Required project-owned plugins

- EFCharacterCreation
- EFCharacterCreationDazBridge
- EFClothingMorph
- EFProcedural
- EFLevelFlow
- EFCharacterCreationACFUBridge
- EFProjectSystems

## Required input contract

- O: free camera
- Period: character creator
- L: debug menu
- Comma: full Needs & Status HUD
- N: custom walk
- C: crawl
- Y: actions/emotes/interactions
- J: Chronicle
- H: conditional status debug
- T/Plus/Minus: preserve exact source interactions

## Change policy

Use adapters, bridges, composition, interfaces, subclasses and project-owned compatibility code.
Preserve public asset paths and Blueprint contracts where possible.
Use Core Redirects for unavoidable class or module renames.
Create small commits and validate after every subsystem.
Keep `Docs/Migration` current.

## Evidence policy

- Mark unverified claims as `PENDING`, not `PASS`.
- Record the source/target path, symbol or asset, test, log or screenshot, and commit/snapshot for every gate.
- Re-hash protected ACFU, DazToUnreal, Player, Female, Frederick, Multiple, and Male invariants after each migration phase.
- Do not write to the source tree, including Git LFS hydration; use a detached copy if hydration is required.

## Default Unreal MCP context

- Invoke `$noshellforwinter-unreal-mcp` at the start of every task that inspects or changes Unreal code, config, assets, Blueprints, maps, editor state, PIE, or runtime behavior.
- Prefer the live UE 5.8 `unreal-mcp` server for editor-owned state that cannot be established safely from files alone.
- Keep the initial MCP pass read-only. Do not mutate assets or editor state unless the user's request authorizes that change.
- If `unreal-mcp` is unavailable, verify that this target project is open in Unreal Editor before falling back to filesystem-only inspection, and mark live-editor conclusions `PENDING`.
