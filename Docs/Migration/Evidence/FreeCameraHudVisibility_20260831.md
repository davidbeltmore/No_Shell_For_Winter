# Free Camera HUD Visibility — 2026-08-31

## Scope

- Target project: `D:\Projects UE5\NoShellForWinter`
- Source project: not read or modified.
- Requested input contract:
  - `O`: gameplay free camera hides the ACFU HUD and project HUDs.
  - `,`: controls the Needs & Status HUD, including day/floor/time.

## Project-owned changes

- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Camera/ProjectGameplayFreeCameraSubsystem.h`
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Camera/ProjectGameplayFreeCameraSubsystem.cpp`
  - Saves and suppresses the project Needs HUD and `AHUD` state during free camera.
  - Invokes the existing reflected `SetHudEnabled(bool)` compatibility seam when the ACFU HUD supports it.
  - Restores the exact pre-camera HUD state on exit.
  - Consumes the configurable Needs-HUD key while free camera is active.
- `Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/DayCycle/ProjectDayCycleSubsystem.cpp`
  - Creates the day/floor/time widget only while `UProjectSurvivalNeedsSubsystem::IsNeedsHudVisible()` is true.

## Gates

| Gate | Evidence | Status |
| --- | --- | --- |
| Live-editor context | `unreal-mcp` at `http://127.0.0.1:8000/mcp`; Blueprint and Editor toolsets enumerated read-only. | PASS |
| Source diff hygiene | `git diff --check` on the three changed files. | PASS |
| Editor build | `Tools/Migration/Build-NoShellForWinterEditor58.ps1`; UHT blocker corrected in the project-owned Calysto header, then the wrapper completed successfully in 200.85s. `ProjectDayCycleSubsystem.cpp` and `ProjectGameplayFreeCameraSubsystem.cpp` compiled and `UnrealEditor-EFProjectSystemsGameplay.dll` linked. Daz receipt repair passed; both Daz plugins are enabled in `NoShellForWinterEditor.target`. | PASS |
| Blocking build defect | `Plugins/EFProcedural/Source/EFProceduralRuntime/Public/Calysto/EFCalystoDungeonDirectorPolicyV5Asset.h`: adjacent tooltip literals were collapsed into single literals without changing their text. | PASS |
| PIE input evidence | `Saved/QA/HubInputContract/20260831_205824/HubInputContractPIE58.json`; physical `O` dispatch reached the floating PIE window. Runtime log recorded free-camera start and enforced `SetNeedsHudVisible(false)`. | PASS |
| Manual visual validation | User confirmed in the restarted editor that the HUD now behaves correctly: `O` hides it in free camera, and day/floor/time appear only when the `,` Needs & Status HUD is open. | PASS |
| Blueprint compile | Not run in this validation pass. | PENDING |
| Cook / packaged validation | Not run in this validation pass. | PENDING |
