# ACF 4.4.2 startup repair — 2026-09-22

Scope: repair the normal Editor startup failure reported for ACFTrainingSystem. Inventory implementation remains a separate, unfinished workstream. Source project was not accessed or modified.

## Diagnosis

Target: `D:/Projects UE5/NoShellForWinter`, UE 5.8.2. The installed Engine Marketplace `ACFUAsce5ab7c1439afbV5/AscentCombatFramework.uplugin` reports **4.4.2**, whereas the previous integration was built for 4.3.5. The failing training DLL was dated August 30; AdvancedRPGSystem.dll was replaced September 21.

`Saved/StartupRepair/startup_failure.log` records LoadLibrary error 127. Import/export comparison identified the missing symbol `?Z_Construct_UClass_UARSStatisticsComponent@@YAPEAVUClass@@W4ETypeConstructPhase@@@Z` in AdvancedRPGSystem.dll. The new public API and vendor class redirects replace both ARSStatisticsComponent and ACFGASAttributesComponent with ACFGASStatisticsComponent. Native DLL imports cannot use Blueprint class redirects.

## Changes

- `Plugins/ACFTrainingSystem/.../ACFTrainingComponent.{h,cpp}`: resolve and use the supported GAS statistics component for scalar requirements and cumulative reward modifiers; retain removal/reapply handles and attribute notifications.
- `Plugins/EFProjectSystems/.../InnerDoctrine/ProjectInnerDoctrineComponent.cpp`: preserve Pain Spike stat modification through the new component type.
- `Plugins/EFProjectSystems/.../Companions/ProjectCompanionRuntimeAdapter.cpp`: use the same supported component for existing character-row inspection, repair and validation.
- Project MCP skill: reusable startup diagnosis/build/launch recipe in `references/startup-module-recovery.md`.

No vendor source/binary patches, plugin disabling, asset edits, save conversion, or broad cache deletion were used. The existing inventory/travel dirty worktree was preserved.

## Evidence

Base commit: `cad7bb1`, with pre-existing uncommitted inventory/travel work. This repair is a separate source change.

| Gate | Result | Evidence |
| --- | --- | --- |
| Reproduce incompatibility against installed headers | Confirmed | `Saved/StartupRepair/build_editor.log`: removed ARSStatisticsComponent.h and ACFGASAttributesComponent.h |
| Editor build after adaptation | PASS | `Saved/StartupRepair/build_editor_fixed.log`, 40.78 seconds, Daz receipt repaired |
| Protected files during final rebuild/launch | PASS | `Saved/StartupRepair/protected_result.json`, 7,642 files, zero differences against installed-4.4.2 snapshot |
| Historical protected baseline | External drift | `external_version_drift.json`: 4,217 changed/added/removed paths, all within ACFU; Daz and protected project characters unchanged |
| Normal Editor startup / HUB ready | PASS | `startup_fixed.log`: HUB map check 0 errors/0 warnings, startup completed in 65.362 s; responsive target Editor PID 63060; `loaded_modules.json` confirms training/gameplay/Daz DLLs loaded |
| MCP connection | PASS | `Saved/StartupRepair/mcp_fixed.json`: native server initialized and toolsets listed. Subsequent current-level query occurred after an independently started PIE session and returned no Editor world; not used as map evidence |
| Project skill validation | PASS | skill-creator quick_validate.py on noshellforwinter-unreal-mcp |
| Gameplay, full Blueprint compile, PIE, cook and packaged validation under ACF 4.4.2 | PENDING | Not certified by this startup repair |

The new protected snapshot records the already installed vendor update; it does not replace or erase the historical 4.3.5 baseline. Initial MCP preflight was unavailable because the Editor was not running. Normal launch uses the protected wrapper with both Daz plugins enabled.

The user session was left running. An independently started PIE session appears in the log at 03:43 UTC; this is evidence that startup progressed, not an automated gameplay regression test. The startup log still includes asset/tag warnings and an Epic HTTP socket error unrelated to the repaired native DLL import. No clean whole-project gameplay claim is made.
