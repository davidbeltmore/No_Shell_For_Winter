param([string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
. (Join-Path $ProjectRoot 'Tools\Migration\Progression-SaveGuard.ps1')
$out=Join-Path $ProjectRoot ('Saved\Migration\InnerState20261005\SaveGuard_'+(Get-Date -Format yyyyMMdd_HHmmss))
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory $out -Action {
 if (!@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) {
  $arguments='-ExecutePythonScript="'+(Join-Path $ProjectRoot 'Tools\InnerState\SaveTravel-PIE58.py')+'" -nosound -csvNoProcessingThread'
  & (Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -AdditionalArguments $arguments -Wait
  return
 }
 & python (Join-Path $ProjectRoot 'Tools\InnerState\RunEditorPython58.py') (Join-Path $ProjectRoot 'Tools\InnerState\SaveTravel-PIE58.py')
 if ($LASTEXITCODE -ne 0) { throw 'Could not schedule save/travel test.' }
 $deadline=(Get-Date).AddSeconds(120)
 while (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
}
$report=Get-Content -LiteralPath (Join-Path $ProjectRoot 'Saved\Migration\InnerState20261005\save_travel.json') -Raw | ConvertFrom-Json
if (!$report.passed) { throw $report.error }
Write-Host "Save/travel PASS; savegame originals restored: $out"
