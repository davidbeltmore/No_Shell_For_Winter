# Day Cycle System — 2026-07-21

## Scope

Project-owned day counter and three-phase HUD for NoShellForWinter. The implementation does not modify ACFU, DazToUnreal, `/Game/FullSample/Player`, or Marketplace assets.

## Runtime contract

- The cycle starts automatically in game and PIE worlds.
- One complete day lasts 600 seconds (10 minutes).
- Each equal 200-second segment represents morning, afternoon, and night.
- The HUD shows `DAY <number>` and three labelled progress segments: `MORNING`, `AFTERNOON`, and `NIGHT`.
- The authoritative start time is replicated by `AProjectDayCycleStateActor`; clients derive progress from `AGameStateBase::GetServerWorldTimeSeconds()`.
- The native widget is the fallback. `UEFProjectUISettings::DayCycleWidgetClass` permits a future Widget Blueprint skin without making it a runtime requirement.

## Evidence

| Gate | Status | Evidence |
|---|---|---|
| Existing-system audit | PASS | Target/source filename and C++ search found ACF time-travel/time-warp UI only; no day-cycle implementation. |
| Project tree safety | PASS | All changes are under the NoShellForWinter project root. |
| Native math automation | PASS | `EFProjectSystems.DayCycle.Math`: 1 succeeded, 0 warnings, 0 failed, 0 not run. The private raw report is excluded from the public tree. |
| UE 5.8 Editor build | PASS | Cold `Build.bat NoShellForWinterEditor Win64 Development`; `Result: Succeeded`. |
| UE 5.8 Game build | PASS | Cold `Build.bat NoShellForWinter Win64 Development`; `Result: Succeeded`. |
| Blueprint compile/load | PASS / N/A | No Blueprint asset was created or modified. `/Game/FullSample/Test`, current `Player.Player_C`, ACF Ultimate GameMode and HUD loaded successfully in PIE. |
| PIE runtime | PASS | `ProjectDayCycleWidget` was present with `SELF_HIT_TEST_INVISIBLE`; log recorded `[ProjectDayCycle] Started a 600.0 second day cycle.` |
| Visual HUD QA | PASS | The current visible editor capture shows `DAY 1`, `MORNING`, `AFTERNOON`, and `NIGHT`: `Saved/Migration/PIE_Baseline_Python/20260722_001838/PIE_Test_Window.png`. The widget was visible, cleanup was `CLEAN`, and the run produced 0 fatal and 0 ensure lines. |
| Cook/package/archive | PASS | The English-label build passed `RunUAT BuildCookRun`, Windows Development, with 7,776 packages, `BUILD SUCCESSFUL`, and `ExitCode=0`. Archive: `Saved/Migration/DayCycleEnglishPackage_20260722/Windows`. |
| Packaged runtime launch | PENDING | The archived bootstrap and child process remained before `FEngineLoop`, created no log, and ignored `-seconds=10`/queued `quit` under both NullRHI and D3D12. Each exact archive process was terminated after a 180-second timeout. No day-cycle runtime claim is made from these attempts. |
| Protected invariant re-hash | PASS (no new delta) | After the English-label package, ACFU 5,043/5,043 and DazToUnreal 213/213 pass. The 74 existing target Daz/Player mismatches exactly match `Saved/Migration/DungeonGenerationParity/ProtectedInvariantsAfter.json` (`DeltaCount=0`). Current evidence: `Saved/Migration/Evidence/DayCycleEnglish_ProtectedInvariantVerification_PostPackage.json`. |
| Source read-only re-check | PASS | Post-package HEAD, status, modified-file hashes and Git LFS manifest match baseline: `Saved/Migration/Evidence/DayCycleEnglish_SourceReadOnlyVerification_PostPackage.json`. |

## Notes

- The live `unreal-mcp` preflight was unavailable because the UE 5.8 Editor was closed at task start. The later visible PIE run supplies runtime and visual evidence, but does not retroactively turn the MCP preflight into a PASS.
- The single PIE error line, `Can't Start the quest`, comes from the existing ACF sample quest flow and is unrelated to the day-cycle system.
