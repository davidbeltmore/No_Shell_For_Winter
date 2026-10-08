param([Parameter(Mandatory)][string]$StagedRoot,
      [string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
$exe=Join-Path $StagedRoot 'NoShellForWinter\Binaries\Win64\NoShellForWinter.exe'
if (!(Test-Path -LiteralPath $exe)) { $exe=Join-Path $StagedRoot 'Windows\NoShellForWinter\Binaries\Win64\NoShellForWinter.exe' }
if (!(Test-Path -LiteralPath $exe)) { throw "Missing package: $exe" }
$out=Join-Path $ProjectRoot ('Saved\BodyPresetsQA\PackagedQA_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
Set-Content -LiteralPath (Join-Path $ProjectRoot 'Saved\BodyPresetsQA\latest-packaged-qa.txt') -Value $out
$arguments='/Game/_Game/Hub/HUB -windowed -ResX=1400 -ResY=1000 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=8000 -csvExecCmds="90:Project.BodyPresets.QA quit" -UserDir="'+(Join-Path $out 'UserData')+'" -ABSLOG="'+(Join-Path $out 'game.log')+'"'
$started=Get-Date
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
$deadline=(Get-Date).AddSeconds(180)
while (!$process.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; $process.Refresh() }
if (!$process.HasExited) { [void]$process.CloseMainWindow(); throw "Packaged body QA exceeded 180 seconds: $out" }
$receipts=@(Get-ChildItem -LiteralPath $out -Recurse -Filter 'runtime.txt' -File)
$passed=$receipts.Count -eq 1 -and (Select-String -LiteralPath $receipts[0].FullName -Pattern 'status=PASS' -Quiet)
$errors=@(Select-String -LiteralPath (Join-Path $out 'game.log') -Pattern 'Handled ensure:|Fatal error!|ACFInventoryLog: Error:|Failed to (load|find object).*(Male2|DA_BodyPresets|PA_Male2|DA_EFClothingFitRegistry|DA_EFClothingSystemManifest|DA_EFClothingMorphDirector)')
$allErrors=@(Select-String -LiteralPath (Join-Path $out 'game.log') -Pattern ': Error:')
$allErrors.Line | Set-Content -LiteralPath (Join-Path $out 'startup-errors.txt')
$result=@{exit_code=$process.ExitCode; elapsed_seconds=((Get-Date)-$started).TotalSeconds; runtime_pass=[bool]$passed; feature_error_count=$errors.Count; all_error_count=$allErrors.Count; receipts=@($receipts.FullName); scope='Body preset runtime; unrelated startup errors require separate review'}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'process-result.json')
if (!$passed -or $process.ExitCode -ne 0 -or $errors.Count) { throw "Packaged body QA failed: $out" }
Write-Host "Packaged runtime assertions and process exit PASS; review screenshots: $out"
