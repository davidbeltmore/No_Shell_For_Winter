param([string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor/commandlet sessions before isolated PIE capture.' }
$script = Join-Path $ProjectRoot 'Tools\Migration\Capture-ProgressionBaselinePIE58.py'
$log = Join-Path $ProjectRoot 'Saved\Analysis\ProgressionPilot\visual.log'
$arguments = '-ExecutePythonScript="' + $script + '" -NoSound -ABSLOG="' + $log + '"'
. (Join-Path $ProjectRoot 'Tools\Migration\Progression-SaveGuard.ps1')
$guard = Join-Path $ProjectRoot ('Saved\Analysis\ProgressionPilot\VisualSaveGuard_' + (Get-Date -Format yyyyMMdd_HHmmss))
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory $guard -Action {
 & (Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
$receipt = Join-Path $ProjectRoot 'Saved\Analysis\ProgressionPilot\Visual\receipt.json'
if (!(Test-Path -LiteralPath $receipt)) { throw 'Visual receipt missing.' }
$result = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
if ($result.error -or $result.screenshots.Count -lt 2) { throw "PIE capture failed: $receipt" }
Write-Host "Screenshots require human/visual inspection: $receipt"
