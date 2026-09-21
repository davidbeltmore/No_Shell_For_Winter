[CmdletBinding()]
param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8",
    [Int64]$RunSeed = 5738796534536664893
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$Launcher = Join-Path $ProjectRoot "Tools\Migration\Launch-NoShellForWinterEditor58.ps1"
$ReceiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"
$Validator = Join-Path $ProjectRoot "Tools\Migration\Validate-CalystoPatrolPIE58.py"

foreach ($RequiredPath in @($Launcher, $ReceiptGuard, $Validator, (Join-Path $ProjectRoot "NoShellForWinter.uproject"))) {
    if (!(Test-Path -LiteralPath $RequiredPath -PathType Leaf)) {
        throw "Required patrol-validation input is missing: $RequiredPath"
    }
}
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -ne 0) {
    throw "Calysto patrol validation requires zero pre-existing UnrealEditor processes."
}

$RunDirectory = Join-Path $ProjectRoot ("Saved\Migration\CalystoDungeonDirectorV6\PatrolPIE_{0}" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
$ReportPath = Join-Path $RunDirectory "PatrolPIE.json"
[void][IO.Directory]::CreateDirectory($RunDirectory)

$PreviousBaselineFlag = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$PreviousScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$PreviousOutput = $env:CODEX_CALYSTO_PATROL_PIE_OUTPUT
$PreviousSeed = $env:CODEX_CALYSTO_PATROL_RUN_SEED

try {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ReceiptGuard -ProjectRoot $ProjectRoot -VerifyOnly
    if ($LASTEXITCODE -ne 0) { throw "Daz receipt verification failed before patrol PIE." }

    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = "1"
    $env:CODEX_MIGRATION_PIE_SCRIPT = $Validator
    $env:CODEX_CALYSTO_PATROL_PIE_OUTPUT = $ReportPath
    $env:CODEX_CALYSTO_PATROL_RUN_SEED = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)

    $Arguments = "-nop4 -nosplash -NoSound -NoAutoSave"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Launcher `
        -ProjectRoot $ProjectRoot -EngineRoot $EngineRoot -AdditionalArguments $Arguments -Wait
    if ($LASTEXITCODE -ne 0) { throw "Protected editor launcher failed with exit code $LASTEXITCODE." }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ReceiptGuard -ProjectRoot $ProjectRoot -VerifyOnly
    if ($LASTEXITCODE -ne 0) { throw "Daz receipt verification failed after patrol PIE." }
    if (!(Test-Path -LiteralPath $ReportPath -PathType Leaf)) {
        throw "Calysto patrol PIE did not write its report: $ReportPath"
    }

    $Report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
    if ([string]$Report.status -cne "PASS") {
        throw "Calysto patrol PIE report failed: $($Report.error)"
    }
    Write-Host "Calysto patrol PIE: PASS ($ReportPath)"
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $PreviousBaselineFlag
    $env:CODEX_MIGRATION_PIE_SCRIPT = $PreviousScript
    $env:CODEX_CALYSTO_PATROL_PIE_OUTPUT = $PreviousOutput
    $env:CODEX_CALYSTO_PATROL_RUN_SEED = $PreviousSeed
}
