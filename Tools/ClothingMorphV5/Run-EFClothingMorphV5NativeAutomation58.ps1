[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [ValidateRange(30, 120)]
    [int]$TimeoutSeconds = 75,
    [ValidatePattern('^[A-Za-z0-9_.]+$')]
    [string]$TestPath = 'EF.ClothingMorph.V51.Contracts'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
foreach ($requiredPath in @($launcher, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required V5.1 Automation path is missing: $requiredPath"
    }
}
if (@(Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'EF Clothing Morph V5.1 native Automation requires every Unreal Editor process to be closed.'
}

$runRoot = Join-Path $root "Saved\Migration\EFClothingMorphV51\NativeAutomation_$Stamp"
$reportRoot = Join-Path $runRoot 'Report'
$logPath = Join-Path $runRoot 'EFClothingMorphV51Native.log'
if (Test-Path -LiteralPath $runRoot) {
    throw "V5.1 Automation evidence path already exists: $runRoot"
}
[void][IO.Directory]::CreateDirectory($runRoot)

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
    ('-ExecCmds="Automation RunTests {0};Quit"' -f $TestPath)
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
    throw 'Protected V5.1 Automation launcher failed.'
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
    throw 'Protected launcher returned but no V5.1 Automation editor process appeared.'
}

$completed = $process.WaitForExit($TimeoutSeconds * 1000)
if (-not $completed) {
    [void]$process.CloseMainWindow()
    if (-not $process.WaitForExit(5000)) {
        Stop-Process -Id $process.Id -Force
    }
    throw "EF Clothing Morph V5.1 native Automation exceeded its $TimeoutSeconds second budget."
}

& $receiptGuard -ProjectRoot $root -VerifyOnly
if (-not $?) {
    throw 'Daz receipt postflight failed.'
}

$indexPath = Join-Path $reportRoot 'index.json'
foreach ($artifactPath in @($indexPath, $logPath)) {
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "V5.1 Automation artifact is missing: $artifactPath"
    }
    if ((Get-Item -LiteralPath $artifactPath).Length -le 0) {
        throw "V5.1 Automation artifact is empty: $artifactPath"
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

if ($summary.Succeeded -ne 1 -or
    $summary.SucceededWithWarnings -ne 0 -or
    $summary.Failed -ne 0 -or
    $summary.NotRun -ne 0 -or
    $summary.Tests -ne 1 -or
    [string]$tests[0].FullTestPath -ne $TestPath) {
    throw 'EF Clothing Morph V5.1 strict native Automation gate failed.'
}

Write-Host 'EF Clothing Morph V5.1 native Automation: PASS'
