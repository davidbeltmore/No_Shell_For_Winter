param([string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
. (Join-Path $ProjectRoot 'Tools\Migration\Progression-SaveGuard.ps1')
$evidence=Join-Path $ProjectRoot ('Saved\BodyPresetsQA\SaveGuard_'+(Get-Date -Format yyyyMMdd_HHmmss))
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory $evidence -Action {
 if (!@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) { throw 'Open the target HUB editor first.' }
 & python (Join-Path $ProjectRoot 'Tools\InnerState\RunEditorPython58.py') (Join-Path $ProjectRoot 'Tools\BodyPresets\NPCSaveQA.py')
 if ($LASTEXITCODE -ne 0) { throw 'Could not schedule NPC save test.' }
 $deadline=(Get-Date).AddSeconds(210)
 while (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
}
$report=Get-Content -LiteralPath (Join-Path $ProjectRoot 'Saved\BodyPresetsQA\npc-save.json') -Raw | ConvertFrom-Json
if (!$report.passed) { throw $report.error }
Write-Host "NPC save/load PASS; original savegames restored: $evidence"
