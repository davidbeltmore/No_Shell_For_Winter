# Project size cleanup — 2026-09-24

## Scope

- Source project `D:\Projects UE5\LustAsDeadlySin`: read-only and untouched.
- Target project `D:\Projects UE5\NoShellForWinter`: generated-output cleanup and read-only asset-reference audit.
- Existing user changes preserved: `Content/_Game/Hub/HUB.umap`, `Content/_Game/Chest/`, and `Docs/Migration/ChestMoveRollback.md`.
- No productive `.uasset` or `.umap` was deleted, moved, renamed, or saved in this pass.

## Size result

Inventory tool: `ue5-project-cleanup-orchestrator/scripts/collect_cleanup_inventory.py`.

| Measurement | GiB |
|---|---:|
| Before | 50.742 |
| After protected cold build and Editor startup | 23.208 |
| Reclaimed | 27.535 |

Main reductions after regenerating the build outputs needed by the Editor:

| Root | Before GiB | After GiB | Reclaimed GiB |
|---|---:|---:|---:|
| `Saved` | 23.384 | 3.035 | 20.349 |
| `Intermediate` | 11.121 | 7.320 | 3.801 |
| `Plugins` | 4.995 | 2.879 | 2.116 |
| `Binaries` | 1.312 | 0.066 | 1.246 |
| `DerivedDataCache` | 0.024 | 0.002 | 0.023 |

Removed output included five duplicated packaged builds under
`Saved/Artifacts/PendingValidation`, `Saved/StagedBuilds/Windows`, cooked data,
crash/log/shader output, project build caches, and rebuildable project-owned
plugin binaries/intermediates. The cleanup used a verified quarantine path and
then removed that quarantine. The quarantine no longer exists.

`Intermediate/PipInstall` was initially removed as a cache, but Unreal rebuilt
its 4.703 GiB Python environment during the protected Editor startup because the
enabled plugins require it. The reusable cleanup script therefore preserves this
environment on future runs.

## Validation and invariants

- Protected baseline: 7,642 ACFU, DazToUnreal, Player, Female, Male, Multiple,
  and Frederick files captured before cleanup.
- Post-clean comparison: `PASS`, zero differences.
- Cold build: `NoShellForWinterEditor Win64 Development`, 281 actions,
  `Result: Succeeded`.
- Daz receipt repair: `PASS`; both Daz plugins remained enabled for Editor launch.
- Editor startup: UE 5.8 initialized successfully.
- Unreal MCP: `http://127.0.0.1:8000/mcp`, protocol `2025-11-25`, read-only
  `AssetTools` and `ProgrammaticToolset` audit completed.
- Git LFS routing remains enabled for `*.uasset` and `*.umap`.
- Blueprint compile, PIE, visual QA, cook, and packaged validation: `PENDING`.
  No productive asset or gameplay code changed in this pass, so these gates were
  not promoted.

## Content audit

Retained because live Asset Registry or project code found productive use:

- `/Game/Food_Props_Kit`: `_Game/FoodSystem` referencers.
- `/Game/RealisticBlood`: Calysto and EFProcedural referencers.
- `/Game/ShareTextures`: `_Game` referencer.
- `/Game/_Temp`: FullSample arrow referencer.
- `/Game/QuangPhan`: dynamic EFCharacterCreation hair path.
- `/Game/Fantastic_Dungeon_Pack`: dynamic Inner Doctrine altar paths.
- `/Game/KawaiiAnimations`: project asset-path resolver plus migration/validation contracts.

Manual-review deletion candidates with zero live Asset Registry referencers and
no project text/config references found:

- `/Game/Vefects`: 130 assets, 112.83 MiB.
- `/Game/LevelPrototyping`: 29 assets, 2.73 MiB.
- `/Game/_MigrationBackup`: 1 asset, 1.13 MiB.

These three roots remain `PENDING` because the project safety guard classifies
Content-root deletion as manual review. Any removal must happen through Unreal
Editor AssetTools, followed by redirector/reference validation; raw filesystem
deletion is forbidden.

## Reusable operation

`Tools/Migration/Invoke-SafeGeneratedCleanup.ps1` contains the closed list of
regenerable filesystem targets, absolute-path containment checks, quarantine-first
handling, and long-path deletion support used by this pass.
