param([string]$ProjectRoot='D:\Projects UE5\NoShellForWinter', [switch]$EnableSound)
$ErrorActionPreference='Stop'
if (@(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Requires no existing editor.' }
$report=Join-Path $ProjectRoot ('Saved/Analysis/CompatibilityClosure/Session_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($report)
. (Join-Path $ProjectRoot 'Tools/Migration/Progression-SaveGuard.ps1')
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory ($report+'/SaveGuard') -Action {
 $arguments='-ExecutePythonScript="'+$ProjectRoot+'/Tools/Migration/CompatibilityInspectionHost58.py" -csvNoProcessingThread -ABSLOG="'+$report+'/editor.log"'
 if (!$EnableSound) { $arguments+=' -NoSound' }
 & (Join-Path $ProjectRoot 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
Write-Host $report
