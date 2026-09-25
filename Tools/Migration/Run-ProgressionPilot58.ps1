param([string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor/commandlet sessions before isolated automation.' }
$report = Join-Path $ProjectRoot ('Saved\Analysis\ProgressionPilot\Automation_' + (Get-Date -Format yyyyMMdd_HHmmss))
$log = $report + '.log'
$arguments = '/Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -NoSound -ExecCmds="Automation RunTests NoShellForWinter.ProgressionPilot+NoShellForWinter.InnerDoctrine" -TestExit="Automation Test Queue Empty" -ReportExportPath="' + $report + '" -ABSLOG="' + $log + '"'
. (Join-Path $ProjectRoot 'Tools\Migration\Progression-SaveGuard.ps1')
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory ($report + '_SaveGuard') -Action {
 & (Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
$index = Join-Path $report 'index.json'
if (!(Test-Path -LiteralPath $index)) { throw "No automation receipt: $index" }
$result = Get-Content -LiteralPath $index -Raw | ConvertFrom-Json
$result | Select-Object Succeeded,SucceededWithWarnings,Failed,NotRun | Format-List
Write-Host "Report: $index"
foreach ($requiredTest in @('NativePurchase', 'ExperienceGuards', 'NativeSaveRoundTrip', 'CurveAdapterParity')) {
 $match = @($result.tests | Where-Object { $_.fullTestPath -eq "NoShellForWinter.ProgressionPilot.$requiredTest" -and $_.state -eq 'Success' })
 if ($match.Count -ne 1) { throw "Missing successful pilot test: $requiredTest" }
}
if ($result.Failed -gt 0 -or $result.NotRun -gt 0 -or $result.SucceededWithWarnings -gt 0 -or $result.InProcess -gt 0 -or $result.Succeeded -lt 18) {
 throw 'Progression pilot gate failed or expected tests are missing.'
}
