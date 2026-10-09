param([Parameter(Mandatory)][string]$StagedRoot,
      [string]$ProjectRoot='D:\Projects UE5\NoShellForWinter',
      [ValidateSet('Appearance','Logic')][string]$Mode='Appearance')
$ErrorActionPreference='Stop'
$exe=Join-Path $StagedRoot 'NoShellForWinter\Binaries\Win64\NoShellForWinter.exe'
if (!(Test-Path -LiteralPath $exe)) { $exe=Join-Path $StagedRoot 'Windows\NoShellForWinter\Binaries\Win64\NoShellForWinter.exe' }
if (!(Test-Path -LiteralPath $exe)) { throw "Missing executable: $exe" }
$suite=if ($Mode -eq 'Logic') {'NPCLogic'} else {'NPCDAZ'}
$limit=if ($Mode -eq 'Logic') {210} else {150}
$out=Join-Path $ProjectRoot ('Saved\'+$suite+'\Packaged_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$arguments='/Game/_Game/Hub/HUB -windowed -ResX=1600 -ResY=1000 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=16000 -csvExecCmds="90:Project.'+$suite+'.QA quit" -UserDir="'+(Join-Path $out 'UserData')+'" -ABSLOG="'+(Join-Path $out 'game.log')+'"'
$started=Get-Date
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
$deadline=(Get-Date).AddSeconds($limit)
while (!$process.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; $process.Refresh() }
if (!$process.HasExited) { Stop-Process -Id $process.Id -Force; throw "Packaged NPC QA exceeded $limit seconds: $out" }
$receipts=@(Get-ChildItem -LiteralPath $out -Recurse -Filter runtime.txt -File | Where-Object { $_.FullName -like ('*'+$suite+'*') })
$passed=$receipts.Count -eq 1 -and (Select-String -LiteralPath $receipts[0].FullName -Pattern 'status=PASS' -Quiet)
$errors=@(Select-String -LiteralPath (Join-Path $out 'game.log') -Pattern 'Fatal error[!:]|=== Critical error: ===|Handled ensure:|Blueprint Runtime Error|Failed to (load|find object).*(DA_NPCRoster|Characters/(Male|Female|Data|Framework|Animation))')
$result=@{mode=$Mode;exit_code=$process.ExitCode;seconds=((Get-Date)-$started).TotalSeconds;runtime_pass=[bool]$passed;feature_errors=$errors.Count;receipts=@($receipts.FullName);limit_seconds=$limit}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'process.json')
Set-Content -LiteralPath (Join-Path $ProjectRoot ('Saved\'+$suite+'\latest-packaged.txt')) -Value $out
if (!$passed -or $process.ExitCode -ne 0 -or $errors.Count) { throw "Packaged NPC QA failed: $out" }
Write-Host "Packaged NPC $Mode smoke PASS; inspect screenshots: $out"
