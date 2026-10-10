# Retained ACF MetaHuman requirements — 2026-10-09

The HUB displayed two AssetGuideline notifications: missing Live Link Control Rig
and Apple ARKit Face Support for
`/AscentCombatFramework/MetaHumans/Common/Common/Mocap/mh_arkit_mapping_anim`,
and four renderer settings for
`/Game/FullSample/MetaHumans/Hadley/Face/Hadley_FaceMesh`.

Read-only native MCP confirmed the target HUB. AssetTools `get_referencers`
identified `mh_arkit_mapping_pose` as a referencer of the mapping animation, and
`/Game/FullSample/MetaHumans/ACF_NPCAI_BP` plus four groom bindings as referencers
of Hadley's face. Removing the visible MetaHuman payload from DAZ variants does
not eliminate the retained original class's dependencies.

UE 5.8 `Engine/Source/Editor/UnrealEd/Private/AssetGuideline.cpp`,
`UAssetGuideline::PostLoad`, checks enabled plugins and exact required INI string
values. Missing explicit config can warn even when a CVar's default agrees. The
notifications are asset requirements; they do not establish the cause of AI or
held-firearm pose failures.

## Scoped correction

- `NoShellForWinter.uproject`: enable installed engine plugins
  `LiveLinkControlRig` and `AppleARKitFaceSupport` (both support this Win64 setup).
- `Config/DefaultEngine.ini`, `[/Script/Engine.RendererSettings]`:
  `r.GPUSkin.Support16BitBoneIndex=True`,
  `r.GPUSkin.UnlimitedBoneInfluences=True`,
  `r.SkinCache.CompileShaders=True`, `SkeletalMesh.UseExperimentalChunking=1`.
- Keep the original asset guidelines and vendor/sample assets unchanged.
  No suppression, wholesale MetaHuman reimport or ACFU replacement.

These renderer capabilities affect shader/mesh processing project-wide and require
a cold restart. See the [official MetaHuman configuration documentation](https://dev.epicgames.com/documentation/en-us/metahuman/requirements-and-configuration-settings-for-metahumans-in-unreal-engine).

`Tools/NPC/AuditMetaHumanRequirements.py` is a read-only live audit, invoked through
`Tools/InnerState/RunEditorPython58.py` after restart. It checks the two plugins,
the protected DAZ plugin enablement, exact authored strings, effective CVars and
both warning assets loading. It neither saves assets nor removes notifications.

## Evidence

Baseline commit: `cfc3bce3ad66bed908a92d3a3a5d0917283d5362`.
The patch changes project configuration, reusable diagnostics and the clothing
bindings automatically regenerated after the renderer restart.
Receipts are collected under `Saved/MetaHumanGuidelines` and
`Saved/NPCDAZ/metahuman-*`; published copies are under
`Docs/Migration/Evidence/MetaHumanRequirements`.

| Gate | Result | Measured scope |
| --- | --- | --- |
| Editor / Game build | PASS | 50.467s / 63.922s, each bounded at 240s |
| Cold live requirements | PASS | 2 required plugins + 2 DAZ plugins enabled; 4/4 authored and effective settings; both named assets load; 2.562s native audit |
| Blueprint compile | PASS | Both owned `ACF_NPCAI_BPMale/Female`; 6.094s native, 90s outer ceiling |
| Cook / package | PASS | 6703 packages; 772.863s outer duration, 900s ceiling; exit 0 |
| Packaged appearance | PASS | Final refreshed package: 12/12 NPCs; clean exit, zero feature errors; 26.393s process, 150s ceiling |
| PIE appearance | PASS after preparation | 12/12; 11.201s native / 13.391s host; 90s / 120s ceilings |
| Visual QA | PASS within scope | Editor UI capture has neither reported notification; DAZ bodies/materials/upright pose and skin variation visible in editor and packaged roster |
| Protected files | PASS | 7643 hashes, zero changes; final comparison 51.5s |
| Final editor state | PASS | `/Game/_Game/Hub/HUB.HUB`, no PIE, no dirty content or maps |
| Output retention | PASS | 3 obsolete Cooked/Staged directories, 5,226,391,531 bytes removed after dry run; logs/receipts/backups retained |

Retained validated package: `Saved/BodyPresetsQA/Package_20261009_215742`.
Final packaged run: `Saved/NPCDAZ/Packaged_20261009_222539`.
The existing HUB door partially occludes the center of the roster view. Numeric
per-NPC assertions complement the image; this is not exhaustive pose or firearm QA.

The first cook (`Package_20261009_215049`) stopped when VS Code was accidentally
closed, as reported by the user. Its last logged count was 6253/6702 packages; no
completion receipt and no remaining cook processes were found on resume. It is
recorded as INTERRUPTED, not PASS. The resumed run reuses the shader cache and
retains the first run's log; successful Editor/Game builds were not repeated.

The first editor readiness monitor was stopped because the nested PowerShell
launch stripped the spaces-related quoting from its optional ABSLOG argument.
That monitor's FAIL is retained. A first MCP request also timed out during loading;
the later live audit and SceneTools confirmed the correct HUB and effective state
without restarting the editor again. The normal protected launch itself succeeded.

The first cold PIE smoke failed its five-second appearance-settling assertion
(`Missing generated DAZ appearance: ACFMeleeEnemyBPMale_C_0`); its UI capture still
showed 51 resources preparing. The failed receipt and screenshot are preserved.
After preparation, `Editor.AsyncAssetCompilationFinishAll` returned immediately
and a single repeat passed all 12 representatives. No gameplay assertion or timeout
was weakened. The exact cause of the transient first-run delay is not certified;
this change does not claim a general cold-start performance fix.

## Generated clothing refresh

`FEFClothingMorphEditorModule::StartupModule` queues `RunAutomaticRefresh` once on
editor startup. After the renderer change, this rebuilt all 18 generated V4
bindings and republished the V4/V5 registries at 22:14 (local project session).
The source garments, body meshes and authoring Director did not change. These
outputs are preserved together, including removal of their obsolete binding paths
by the existing editor gate, rather than restoring stale references to clean Git.

Read-only strict certification found 18 enabled / 18 valid bindings, zero drafts,
invalid or stale rows, plus a valid V5 manifest. Native duration: 1.328s.
The initial helper tried to assign deprecated `MinimumClearanceCm`; Unreal rejected
that protected property before validation. The reusable
`Tools/ClothingMorphV5/Audit-Freshness58.py` uses its canonical zero default instead.

Because the first full package preceded the automatic editor refresh, the same
retained package was incrementally recooked/staged with the final bindings:
176.694s including the process wrapper, exit 0, 480s ceiling. The final packaged
appearance run passed in 26.393s and its roster image was reviewed.

The final protected editor launch retained correct ABSLOG quoting by passing its
arguments inside a PowerShell script rather than through nested command-line
parsing. Native MCP confirmed the HUB in 60.628s. The reusable read-only audits
passed again; all 51 generated output asset hashes were identical across this
restart, so no additional clothing regeneration occurred. The final editor is
outside PIE with no dirty content/maps. `manifest.json` pins the implementation
snapshot and preserved evidence hashes. Clothing certification here covers data
freshness and manifest consistency; a new exhaustive garment-fitting suite was
not run for this requirements correction.
