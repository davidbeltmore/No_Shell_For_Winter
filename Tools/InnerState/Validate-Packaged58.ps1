param([Parameter(Mandatory)][string]$StagedRoot,
      [string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
$exe=Join-Path $StagedRoot 'NoShellForWinter\Binaries\Win64\NoShellForWinter.exe'
if (!(Test-Path -LiteralPath $exe)) { $exe=Join-Path $StagedRoot 'Windows\NoShellForWinter\Binaries\Win64\NoShellForWinter.exe' }
if (!(Test-Path -LiteralPath $exe)) { throw "Missing package: $exe" }
$out=Join-Path $ProjectRoot ('Saved\Migration\InnerState20261005\PackagedQA_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$arguments='/Game/_Game/Hub/HUB -windowed -ResX=1600 -ResY=1000 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=8000 -csvExecCmds="90:Project.InnerState.QA" -InnerStateQAQuit -InnerStateQAOutput="'+$out+'" -UserDir="'+(Join-Path $out 'UserData')+'" -ABSLOG="'+(Join-Path $out 'game.log')+'"'
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
$deadline=(Get-Date).AddSeconds(180) # Includes engine/map startup. Harness independently caps gameplay at 110.
while (!$process.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; $process.Refresh() }
if (!$process.HasExited) { [void]$process.CloseMainWindow(); throw "Packaged QA timeout: $out" }
$report=Join-Path $out 'report.json'
if (!(Test-Path -LiteralPath $report)) { throw "No runtime report: $out" }
$data=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
if (!$data.passed -or $data.wall_seconds -gt 120 -or $process.ExitCode -ne 0) { throw "Packaged QA failed: $report; exit=$($process.ExitCode)" }
$runtimeErrors=Select-String -LiteralPath (Join-Path $out 'game.log') -Pattern 'Handled ensure:|Fatal error!|ACFInventoryLog: Error:'
if ($runtimeErrors) { throw "Functional checks passed but packaged runtime emitted errors: $out" }
Write-Host "PASS: $report"
