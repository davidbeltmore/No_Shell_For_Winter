# Module startup recovery

Read `Saved/Logs/NoShellForWinter.log` before relaunching and preserve the failed log under a fresh `Saved/StartupRepair` evidence directory. Check the exact target Editor process; an unavailable MCP endpoint is expected if module loading failed.

On Windows, `GetLastError=127` during DLL loading can mean an imported procedure is absent from a dependency. Compare `dumpbin /imports <project-module.dll>` with `dumpbin /exports <actual-dependency.dll>` using the installed VS x64 tool. Record the missing decorated symbol, dependency path, and installed `.uplugin` version. Do not disable the failing plugin or copy old vendor binaries over the installation.

## ACF 4.4.2 compatibility

The September 2026 installation replaced ACF 4.3.5 with 4.4.2. Old project DLLs imported `Z_Construct_UClass_UARSStatisticsComponent` from AdvancedRPGSystem, which no longer exports that class. The public supported component is `UACFGASStatisticsComponent` in `ACFGASStatisticsComponent.h`, module AdvancedRPGSystem. It also replaces `UACFGASAttributesComponent`; the vendor's BaseAscentCombatFramework.ini contains both class redirects. Update direct project C++ references and rebuild; Blueprint redirects alone cannot fix native DLL imports. Preserve modifier handles, reward math, stat calls and row validation.

Affected integration sites: ACFTrainingComponent, ProjectInnerDoctrineComponent::EmitPainSpike, and ProjectCompanionRuntimeAdapter. Confirm the currently installed API before reusing this migration on another version.

## Protected rebuild and verification

From the target project root, with its Editor closed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Migration/Build-NoShellForWinterEditor58.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/Migration/Launch-NoShellForWinterEditor58.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .agents/skills/noshellforwinter-unreal-mcp/scripts/Test-UnrealMcp.ps1
```

These wrappers preserve the Daz receipt contract. Inspect the full build result, then verify loaded target project/module and map readiness in the normal Editor. Do not treat a launch request or a successful TCP connection alone as a successful load. Preserve existing unsaved assets and user save files. Record external plugin updates as baseline drift instead of claiming old protected hashes still pass. Separate startup verification from gameplay, cook and packaged validation; those gates require their own evidence after an ACF update.
