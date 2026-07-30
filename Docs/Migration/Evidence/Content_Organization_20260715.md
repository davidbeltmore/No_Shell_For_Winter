# Content organization evidence - 2026-07-15

## Scope

Target project: NoShellForWinter

Branch at audit time: private migration branch (not published)

Starting commit: `62b1f6108ddf5c66ef26dc710c3167a42a3e1517`

This pass organized only clearly project-owned loose assets. It did not move or edit external/formal content roots such as `Calysto`, `FullSample`, `DazToUnreal`, `KawaiiAnimations`, ACF content, Marketplace plugins, or project-owned formal systems. `/Game/FullSample/Player` remained outside `/Game/_Game`.

No assets were deleted as junk. Assets with no external referencers and test/import naming were quarantined under `/Game/_Game/Trash` for later human review.

## Editor-owned moves

All `.uasset` and `.umap` operations were performed through the live UE 5.8 Editor Asset Tools, never through raw filesystem moves.

| Old path | New path | Reason |
| --- | --- | --- |
| `/Game/_Game/Animations/1` | `/Game/_Game/Trash/AnimationTests/1` | Unreferenced animation test |
| `/Game/_Game/Animations/Teest2` | `/Game/_Game/Trash/AnimationTests/Teest2` | Unreferenced animation test |
| `/Game/_Game/Animations/3333333333333333*` and its imported materials/textures | `/Game/_Game/Trash/CharacterImports/3333333333333333/{Animations,Materials,Meshes,Rig,Textures}` | Unreferenced 25-asset character import bundle; dependencies were internal to the bundle |
| `/Game/_Game/LevelDesign/Chandelier` | `/Game/_Game/LevelDesign/Props/Lighting/Chandelier` | Useful project Blueprint, organized by purpose |
| `/Game/_Game/LevelDesign/Torch` | `/Game/_Game/LevelDesign/Props/Lighting/Torch` | Useful project Blueprint used by `StorySelection`, organized by purpose |

Final quarantine inventory:

- `AnimationTests`: 2 assets.
- `CharacterImports`: 25 assets.
- Total `/Game/_Game/Trash`: 27 assets.

## Redirector and reference verification

- The `Torch` redirector was fixed with UE `ResavePackages -FixupRedirects` using only the old `Torch` package and `/Game/_Game/Locations/StorySelection`.
- The second redirector pass reported one unreferenced redirector deleted.
- Live Asset Registry verification after reopening the project reported:
  - old `/Game/_Game/LevelDesign/Torch`: absent;
  - `/Game/_Game/Locations/StorySelection` directly references and depends on `/Game/_Game/LevelDesign/Props/Lighting/Torch`;
  - redirectors under the affected `_Game` folders: zero;
  - assets remaining in `/Game/_Game/Animations`: zero;
  - assets in `/Game/_Game/Trash`: 27.
- The quarantined skeletal mesh still depends on its moved physics asset, skeleton and materials, and the moved skeleton is still referenced by the moved mesh and animation.

## Validation gates

| Gate | Result | Evidence |
| --- | --- | --- |
| `Chandelier` Blueprint compile, warnings as errors | PASS | Live UE 5.8 MCP `BlueprintTools.compile_blueprint` |
| `Torch` Blueprint compile, warnings as errors | PASS | Live UE 5.8 MCP `BlueprintTools.compile_blueprint` |
| `StorySelection` map check | PASS | Editor log: 0 errors, 0 warnings |
| PIE on `StorySelection` | PASS | PIE world created and started in 27.522 seconds; the MCP request timed out before the delayed start, then `IsPIERunning` returned true |
| `NoShellForWinterEditor Win64 Development` build | PASS | `Build.bat`; target up to date |
| Full Development game build | PASS | UAT compiled 92 actions and produced `Binaries/Win64/NoShellForWinter.exe` |
| Cook/package of `StorySelection` | BLOCKED | Cook stopped on two Blueprint compiler errors in the protected Marketplace asset `ACF_PickAction_BP.uasset`: missing `GetInventoryComponent` function and stale `Return Value` pin. The protected plugin was not modified. |
| Visual QA | PENDING | Validation Editor ran with `NullRHI`; no reliable rendered viewport evidence was produced |
| Source project read-only | PASS | `Saved/Migration/Evidence/ContentOrganization_SourceReadOnly.json` |
| Protected ACFU plugin | PASS | 5,043 files; `Saved/Migration/Evidence/ContentOrganization_Post_ProtectedInvariants.json` |
| Protected DazToUnreal plugin | PASS | 213 files; `Saved/Migration/Evidence/ContentOrganization_Post_ProtectedInvariants.json` |
| Pre/post protected mismatch delta | PASS | 69 pre-existing target Daz/authoritative mismatches before and after; exact normalized delta count 0 |

Cook log copied by UAT: `C:\Users\bigin\AppData\Roaming\Unreal Engine\AutomationTool\Logs\D+Unreal+Engine+5+Library+UE_5.8\Cook-2026.07.15-16.07.53.txt`.

Read-only inventories:

- Before: `Saved/Analysis/ContentOrganization_Before.json`
- After: `Saved/Analysis/ContentOrganization_After.json`

## Publication state

Publication is pending because this checkout has no configured Git remote and GitHub CLI (`gh`) is not installed. No commit or push was attempted without those prerequisites.

The user-requested `commit all` scope currently contains 2,044 status entries: 195 tracked changes (including 10 deletions) plus 1,849 untracked files. The untracked portion is approximately 2.121 GiB and is primarily Unreal content tracked through Git LFS.
