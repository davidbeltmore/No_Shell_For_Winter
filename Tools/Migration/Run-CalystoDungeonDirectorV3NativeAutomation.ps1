[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$project = Join-Path $root 'NoShellForWinter.uproject'
foreach ($requiredPath in @($launcher, $project)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required Dungeon Director V3 automation path is missing: $requiredPath"
    }
}

$report = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV3\NativeAutomation_$Stamp"
$log = Join-Path $root "Saved\Migration\Logs\CalystoDungeonDirectorV3Native_$Stamp.log"
$filters = @(
    'StartsWith:NoShellForWinter.CalystoDungeon.V3'
    'StartsWith:NoShellForWinter.GameplayDebug.DungeonHarness.V3'
) -join '+'

$arguments = @(
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    '-stdout'
    '-FullStdOutLogOutput'
    "-ExecCmds=`"Automation RunTests $filters;Quit`""
    '-TestExit="Automation Test Queue Empty"'
    "-ReportExportPath=`"$report`""
    "-ABSLOG=`"$log`""
) -join ' '

& $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
if ($LASTEXITCODE -ne 0) {
    throw "Protected launcher failed with exit code $LASTEXITCODE."
}

$indexPath = Join-Path $report 'index.json'
if (-not (Test-Path -LiteralPath $indexPath -PathType Leaf)) {
    throw "Dungeon Director V3 Automation report is missing: $indexPath"
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    throw "Dungeon Director V3 Automation log is missing: $log"
}

$results = Get-Content -Raw -LiteralPath $indexPath | ConvertFrom-Json
$summary = [pscustomobject]@{
    Succeeded = [int]$results.Succeeded
    SucceededWithWarnings = [int]$results.SucceededWithWarnings
    Failed = [int]$results.Failed
    NotRun = [int]$results.NotRun
    Report = $indexPath
    Log = $log
}
$summary | Format-List

$expectedTestCount = 18
if (
    $summary.Succeeded -ne $expectedTestCount -or
    $summary.SucceededWithWarnings -ne 0 -or
    $summary.Failed -ne 0 -or
    $summary.NotRun -ne 0
) {
    throw "Dungeon Director V3 strict native Automation gate failed; expected $expectedTestCount passing tests."
}

$forbiddenLogText = @(
    'Blueprint Runtime Error',
    'LogBlueprint: Error',
    'Ensure condition failed',
    'Fatal error:',
    'Assertion failed:',
    'Object Transform',
    'GetAttributeFromPointIndex_0'
)
$logFindings = @(
    foreach ($pattern in $forbiddenLogText) {
        Select-String -LiteralPath $log -SimpleMatch -Pattern $pattern |
            ForEach-Object { "[$pattern] $($_.Line.Trim())" }
    }
)
if ($logFindings.Count -ne 0) {
    throw "Dungeon Director V3 Automation log contains forbidden diagnostics:`n$($logFindings -join [Environment]::NewLine)"
}

Write-Host 'Dungeon Director V3 native Automation: PASS'
