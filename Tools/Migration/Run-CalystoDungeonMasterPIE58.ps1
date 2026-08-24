[CmdletBinding()]
param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [Int64]$RunSeed = 2026080201,
    [ValidateRange(10, 100)][int]$MaxTestFloor = 10,
    [switch]$RequireStartPointRepair,
    [string]$OutputPath = "",
    [string]$LogPath = ""
)

$ErrorActionPreference = "Stop"
if ($RunSeed -le 0) {
    throw "Calysto Dungeon Director V3 PIE requires a positive Int64 RunSeed."
}

$launcher = Join-Path $ProjectRoot "Tools\Migration\Launch-NoShellForWinterEditor58.ps1"
$pythonScript = Join-Path $ProjectRoot "Tools\Migration\Validate-CalystoDungeonMasterPIE58.py"
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot "Saved\Migration\CalystoDungeonDirectorV3\SeedReplayRerollAdvancePIE58_$stamp.json"
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $ProjectRoot "Saved\Migration\Logs\CalystoDungeonDirectorV3PIE_$stamp.log"
}

foreach ($requiredPath in @($launcher, $pythonScript)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required Calysto Dungeon Director V3 PIE path not found: $requiredPath"
    }
}

$oldRun = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldOutput = $env:CODEX_CALYSTO_DIRECTOR_V3_PIE_OUTPUT
$oldSeed = $env:CODEX_CALYSTO_DIRECTOR_V3_RUN_SEED
$oldMaxTestFloor = $env:CODEX_CALYSTO_DIRECTOR_V3_MAX_TEST_FLOOR
$oldRequireStartRepair = $env:CODEX_CALYSTO_DIRECTOR_V3_REQUIRE_START_REPAIR
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = "1"
    $env:CODEX_MIGRATION_PIE_SCRIPT = $pythonScript
    $env:CODEX_CALYSTO_DIRECTOR_V3_PIE_OUTPUT = $OutputPath
    $env:CODEX_CALYSTO_DIRECTOR_V3_RUN_SEED = $RunSeed.ToString(
        [System.Globalization.CultureInfo]::InvariantCulture
    )
    $env:CODEX_CALYSTO_DIRECTOR_V3_MAX_TEST_FLOOR = $MaxTestFloor.ToString(
        [System.Globalization.CultureInfo]::InvariantCulture
    )
    $env:CODEX_CALYSTO_DIRECTOR_V3_REQUIRE_START_REPAIR = if ($RequireStartPointRepair) { "1" } else { "0" }
    $arguments = '-unattended -nop4 -nosplash -NoSound -stdout -FullStdOutLogOutput -ABSLOG="' + $LogPath + '"'
    & $launcher -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldRun
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_DIRECTOR_V3_PIE_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_DIRECTOR_V3_RUN_SEED = $oldSeed
    $env:CODEX_CALYSTO_DIRECTOR_V3_MAX_TEST_FLOOR = $oldMaxTestFloor
    $env:CODEX_CALYSTO_DIRECTOR_V3_REQUIRE_START_REPAIR = $oldRequireStartRepair
}

if (!(Test-Path -LiteralPath $OutputPath)) {
    throw "Calysto Dungeon Director V3 PIE evidence was not produced: $OutputPath"
}
if (!(Test-Path -LiteralPath $LogPath)) {
    throw "Calysto Dungeon Director V3 PIE log was not produced: $LogPath"
}

$result = Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
if (!$result.success) {
    throw "Calysto Dungeon Director V3 PIE failed: $($result.error)"
}
if ([int]$result.schema_version -lt 3) {
    throw "Calysto PIE emitted legacy evidence schema $($result.schema_version); V3 requires schema 3."
}
if ([Int64]$result.run_seed -ne $RunSeed) {
    throw "Calysto PIE used run seed $($result.run_seed), expected $RunSeed."
}
if ([int]$result.max_test_floor -ne $MaxTestFloor) {
    throw "Calysto PIE used max test floor $($result.max_test_floor), expected $MaxTestFloor."
}
if ([bool]$result.require_start_point_repair -ne [bool]$RequireStartPointRepair) {
    throw "Calysto PIE start-point-repair requirement did not match the requested gate."
}
if ([int]$result.v3_policy.schema_version -ne 3 -or
    [int]$result.v3_policy.generator_version -ne 3 -or
    [string]$result.v3_policy.native_validation -ne "PASS") {
    throw "Calysto PIE did not validate the sole Dungeon Director V3 policy natively."
}
if ([string]::IsNullOrWhiteSpace([string]$result.asset_monitor.method)) {
    throw "Calysto PIE did not report its measured asset mutation/save method."
}

$failedChecks = @(
    $result.checks.PSObject.Properties |
        Where-Object { ![bool]$_.Value } |
        ForEach-Object { $_.Name }
)
if ($failedChecks.Count -ne 0) {
    throw "Calysto Dungeon Director V3 PIE reported false checks: $($failedChecks -join ', ')"
}
if (@($result.asset_mutations).Count -ne 0) {
    throw "Calysto Dungeon Director V3 PIE mutated assets: $(@($result.asset_mutations) -join ', ')"
}
if (@($result.asset_saves).Count -ne 0) {
    throw "Calysto Dungeon Director V3 PIE saved assets: $(@($result.asset_saves) -join ', ')"
}

$forbiddenLogText = @(
    "Blueprint Runtime Error",
    "LogBlueprint: Error",
    "Accessed None",
    "Ensure condition failed",
    "Fatal error:",
    "Assertion failed:",
    "cancelled or cleaned",
    "Calysto controlled generation failed",
    "Calysto generation failed",
    "Calysto V3 exhausted attempts",
    "TopologyRepair failed closed",
    "duplicate generation",
    "Object Transform",
    "GetAttributeFromPointIndex_0"
)
$logFindings = @(
    foreach ($pattern in $forbiddenLogText) {
        Select-String -LiteralPath $LogPath -SimpleMatch -Pattern $pattern |
            ForEach-Object { "[$pattern] $($_.Line.Trim())" }
    }
)
if ($logFindings.Count -ne 0) {
    throw "Calysto Dungeon Director V3 PIE log contains forbidden diagnostics:`n$($logFindings -join [Environment]::NewLine)"
}

Write-Host "Calysto Dungeon Director V3 seed/replay/reroll/advance through Floor ${MaxTestFloor} PIE: PASS"
Write-Host "Run seed: $RunSeed"
Write-Host "Evidence: $OutputPath"
Write-Host "Log: $LogPath"
