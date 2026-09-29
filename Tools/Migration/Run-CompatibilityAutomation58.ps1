param([ValidateSet('default','en')][string]$Culture='default', [string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
if (@(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Requires no existing editor.' }
$report=Join-Path $ProjectRoot ('Saved/Analysis/CompatibilityClosure/Automation_'+$Culture+'_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($report)
$arguments='/Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -NoSound -csvNoProcessingThread -ExecCmds="Automation RunTests System.Core.Experimental.UnifiedError+NoShellForWinter.Practice+NoShellForWinter.ProgressionPilot+NoShellForWinter.InnerDoctrine" -TestExit="Automation Test Queue Empty" -ReportExportPath="'+$report+'" -ABSLOG="'+$report+'/editor.log"'
if ($Culture -eq 'en') { $arguments+=' -culture=en' }
. (Join-Path $ProjectRoot 'Tools/Migration/Progression-SaveGuard.ps1')
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory ($report+'/SaveGuard') -Action {
 & (Join-Path $ProjectRoot 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
if (!(Test-Path -LiteralPath ($report+'/index.json'))) { throw 'Automation did not produce its report.' }
Write-Host $report
