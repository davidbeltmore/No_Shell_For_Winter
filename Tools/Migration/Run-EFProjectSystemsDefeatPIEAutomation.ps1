[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EditorCmd = 'D:\Unreal Engine 5\Library\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe',
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$editor = (Resolve-Path -LiteralPath $EditorCmd).Path
$project = Join-Path $root 'NoShellForWinter.uproject'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
    throw "Project descriptor is missing: $project"
}

$report = Join-Path $root "Saved\Migration\Automation\Phase4_EFProjectSystems_DefeatPIE_$Stamp"
$log = Join-Path $root "Saved\Migration\Logs\Phase4_EFProjectSystems_DefeatPIE_$Stamp.log"
$filters = @(
    '^ACFUltimateSample.Defeat.Flow.PIE.OutOfCombatRecovery$'
    '^ACFUltimateSample.Defeat.Flow.PIE.CombatStruggleWin$'
    '^ACFUltimateSample.Defeat.Flow.PIE.CombatStruggleLose$'
    '^ACFUltimateSample.Defeat.Flow.PIE.RepeatKnockoutSameCombat$'
    '^ACFUltimateSample.Defeat.Flow.PIE.CancelledDefeatedSceneRestoresMovement$'
    '^ACFUltimateSample.Defeat.Flow.PIE.TravelArrivalFapCancelRestoresMovement$'
) -join '+'

$arguments = @(
    $project
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    '-stdout'
    '-FullStdOutLogOutput'
    "-ExecCmds=Automation RunTests $filters;Quit"
    '-TestExit=Automation Test Queue Empty'
    "-ReportExportPath=$report"
    "-ABSLOG=$log"
)

& $editor @arguments
$exitCode = $LASTEXITCODE

$indexPath = Join-Path $report 'index.json'
if (-not (Test-Path -LiteralPath $indexPath -PathType Leaf)) {
    throw "Automation report is missing after exit code ${exitCode}: $indexPath"
}

$results = Get-Content -Raw -LiteralPath $indexPath | ConvertFrom-Json
$summary = [pscustomobject]@{
    ExitCode = $exitCode
    Succeeded = [int]$results.Succeeded
    SucceededWithWarnings = [int]$results.SucceededWithWarnings
    Failed = [int]$results.Failed
    NotRun = [int]$results.NotRun
    Report = $indexPath
    Log = $log
}
$summary | Format-List

if (
    $exitCode -ne 0 -or
    $summary.Succeeded -ne 6 -or
    $summary.SucceededWithWarnings -ne 0 -or
    $summary.Failed -ne 0 -or
    $summary.NotRun -ne 0
) {
    throw 'EFProjectSystems strict Defeat PIE Automation gate failed.'
}
