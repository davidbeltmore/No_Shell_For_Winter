param([string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
. (Join-Path $ProjectRoot 'Tools\Migration\Progression-SaveGuard.ps1')
$evidence=Join-Path $ProjectRoot ('Saved\BodyPresetsQA\VisualSaveGuard_'+(Get-Date -Format yyyyMMdd_HHmmss))
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory $evidence -Action {
 if (!@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) {
  $arguments='-nosound -ExecutePythonScript="'+(Join-Path $ProjectRoot 'Tools\BodyPresets\VisualQA.py')+'"'
  & (Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -AdditionalArguments $arguments -Wait
  return
 }
 & python (Join-Path $ProjectRoot 'Tools\InnerState\RunEditorPython58.py') (Join-Path $ProjectRoot 'Tools\BodyPresets\VisualQA.py')
 if ($LASTEXITCODE -ne 0) { throw 'Could not schedule visual test.' }
 $deadline=(Get-Date).AddSeconds(510)
 while (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
}
$directory=Get-Content -LiteralPath (Join-Path $ProjectRoot 'Saved\BodyPresetsQA\latest-visual.txt') -Raw
$report=Get-Content -LiteralPath (Join-Path $directory 'result.json') -Raw | ConvertFrom-Json
if ($report.status -ne 'PASS') { throw $report.error }
Write-Host "Clothing runtime checks PASS; manual image review remains required: $directory"
