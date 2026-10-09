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
- ACF Ultimate 4.4.2 installed in target (descriptor + Epic manifest audit, 2026-10-09).
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

## Default Calysto dungeon context

- Invoke `$calysto-dungeon-master` after `$noshellforwinter-unreal-mcp` for any task involving Calysto, `BP_MassiveDungeon`, dungeon floors, procedural size, PCG seeds, enemy/spawner weights, room themes, `End_Point`, the floor door, or the `L > Dungeon Harness` menu.
- Treat `Plugins/EFProcedural` as the single project-owned master integration. Never patch or save `BP_MassiveDungeon` or Calysto source DataAssets; tune through transient clones and `/Game/_Game/Data/CalystoDungeon`.

## Imported body presets and NPC appearance

- Use `$ef-body-preset-workbench` (`A:/Skills Codex/ef-body-preset-workbench/SKILL.md`) after Unreal MCP for imported DAZ body onboarding, per-gender player presets, and NPC body/morph variety.
- The user handles DAZ export/import with morphs. Preferred conversion is `Convert To Epic Skeleton > SKM_UEFN_Mannequin`, `Merge Type = New`, replacing the imported body's own skeleton in its own folder. Verify the final mesh skeleton assignment: the installed converter can reassign the shared UEFN skeleton after the New dialog.
- Adding a preset preserves existing bodies. Do not interpret it as global reference replacement. Player and NPC consumers share `/Game/_Game/Data/CharacterCreation/DA_BodyPresets` with stable IDs and per-body supported morph profiles.
- Use `Tools/BodyPresets/OnboardBody.py` (inspect, then prepare) and the project-owned G9 adapter. It preserves the intended New/local-skeleton result without the vendor's final shared-skeleton reassignment. Never convert an already-converted mesh again.
- See `Docs/Migration/BodyPresetsImplementation.md` for measured gates and limitations, and `Docs/Migration/BodyPresetWorkflow.md` for repeatable entry points. A preparation receipt alone does not certify runtime support.

## DAZ NPC roster and appearance

- Use `.agents/skills/ef-npc-daz-workbench/SKILL.md` after Unreal MCP for NPC roster,
  spawn appearance, neutral male morphs and character animation initialization.
- Extend `Tools/NPC/Roster.json` and `/Game/_Game/Characters/Data/DA_NPCRoster`;
  preserve owned character Blueprint ancestry and per-variant initializer assets.
- New male NPC bodies require verified neutral morph defaults. Keep these outside
  randomization and preserve them during preset onboarding and save migration.
- Prefer bounded structural checks for all variants and representative runtime QA;
  measure completion counts, pose/morph assertions, elapsed seconds and failures.

## Generated validation output retention

- Repeated `Saved/**/Package_*/Cooked` and `Staged` outputs caused the 2026-10-08 size regression. Preserve receipts, logs, screenshots, saves, autosaves and rollback backups; keep one latest successfully validated package instead of accumulating full build copies.
- After successful cook and packaged QA, use `Tools/Migration/Remove-OldValidationPackages.ps1` in dry-run mode first, then `-Apply` within the authorized cleanup scope. It retains the successful body-preset package referenced by `Saved/BodyPresetsQA/latest-package.txt`; verify that package is the intended retained artifact. Never run it during packaging or a packaged game session.
- Keep `Intermediate/PipInstall`, required binaries and productive assets. Do not force Git LFS pruning when remote verification fails. Record measured before/after bytes and protected comparisons in `Docs/Migration`.
