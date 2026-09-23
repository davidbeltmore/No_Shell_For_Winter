[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [ValidateRange(30, 120)]
    [int]$TimeoutSeconds = 75,
    [ValidatePattern('^[A-Za-z0-9_.]+$')]
    [string]$TestPath = 'EF.Equipment'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'EquipmentAutomationReport.ps1')

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
foreach ($requiredPath in @($launcher, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required Equipment Automation path is missing: $requiredPath"
    }
}
if (@(Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'EFEquipment native Automation requires every Unreal Editor process to be closed.'
}

$runRoot = Join-Path $root "Saved\EquipmentQA\NativeAutomation_$Stamp"
$reportRoot = Join-Path $runRoot 'Report'
$logPath = Join-Path $runRoot 'EquipmentNative.log'
if (Test-Path -LiteralPath $runRoot) {
    throw "Equipment Automation evidence path already exists: $runRoot"
}
[void][IO.Directory]::CreateDirectory($runRoot)

$requiredTests = @('EF.Equipment.Native.Vertical', 'EF.Equipment.Policy.Contracts')
if ($TestPath -cne 'EF.Equipment') { throw 'This gate requires the exact two-test equipment suite.' }
$exactFilter = (@($requiredTests | ForEach-Object { '^' + $_ + '$' }) -join '+')
$additionalArguments = @(
    '/Engine/Maps/Entry'
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    '-DisablePlugins=BpGeneratorUltimate,MCPClientToolset,ModelContextProtocol'
    '-stdout'
    '-FullStdOutLogOutput'
    ('-ExecCmds="Automation RunTests {0};Quit"' -f $exactFilter)
    '-TestExit="Automation Test Queue Empty"'
    "-ReportExportPath=`"$reportRoot`""
    "-ABSLOG=`"$logPath`""
) -join ' '

& $receiptGuard -ProjectRoot $root -VerifyOnly
if (-not $?) {
    throw 'Daz receipt preflight failed.'
}

$launchStarted = Get-Date
& $launcher `
    -ProjectRoot $root `
    -AdditionalArguments $additionalArguments
if (-not $?) {
    throw 'Protected Equipment Automation launcher failed.'
}

$startDeadline = (Get-Date).AddSeconds(15)
$process = $null
do {
    Start-Sleep -Milliseconds 200
    $process = Get-Process UnrealEditor -ErrorAction SilentlyContinue |
        Where-Object { $_.StartTime -ge $launchStarted.AddSeconds(-1) } |
        Sort-Object StartTime -Descending |
        Select-Object -First 1
} while (-not $process -and (Get-Date) -lt $startDeadline)
if (-not $process) {
    throw 'Protected launcher returned but no Equipment Automation editor process appeared.'
}

$null = $process.Handle
$completed = $process.WaitForExit($TimeoutSeconds * 1000)
if (-not $completed) {
    [void]$process.CloseMainWindow()
    if (-not $process.WaitForExit(5000)) {
        Stop-Process -Id $process.Id -Force
    }
    throw "EFEquipment native Automation exceeded its $TimeoutSeconds second budget."
}

& $receiptGuard -ProjectRoot $root -VerifyOnly
if (-not $?) {
    throw 'Daz receipt postflight failed.'
}

$indexPath = Join-Path $reportRoot 'index.json'
foreach ($artifactPath in @($indexPath, $logPath)) {
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "Equipment Automation artifact is missing: $artifactPath"
    }
    if ((Get-Item -LiteralPath $artifactPath).Length -le 0) {
        throw "Equipment Automation artifact is empty: $artifactPath"
    }
}

$results = Get-Content -Raw -LiteralPath $indexPath | ConvertFrom-Json
$tests = @($results.Tests)
$summary = [pscustomobject]@{
    Succeeded = [int]$results.Succeeded
    SucceededWithWarnings = [int]$results.SucceededWithWarnings
    Failed = [int]$results.Failed
    NotRun = [int]$results.NotRun
    Tests = $tests.Count
    ElapsedSeconds = [math]::Round(((Get-Date) - $launchStarted).TotalSeconds, 2)
    Report = $indexPath
    Log = $logPath
}
$summary | Format-List

if ($process.ExitCode -ne 0 -or -not (Test-ExactEquipmentAutomationReport -Report $results -Expected $requiredTests)) {
    throw 'EFEquipment native Automation gate failed or required tests are missing.'
}

Write-Host 'EFEquipment native Automation: PASS'
