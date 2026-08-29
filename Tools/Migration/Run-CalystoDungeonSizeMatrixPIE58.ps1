[CmdletBinding()]
param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [ValidateRange(18, 30)]
    [int[]]$Sizes = (18..30),
    [ValidateRange(1, 100)]
    [int]$SeedsPerSize = 20,
    [ValidateRange(1, [Int64]::MaxValue)]
    [Int64]$BaseSeed = 202608140000,
    [Int64[]]$Seeds = @(),
    [ValidateSet('rotating', 'seed-major', 'size-major')]
    [string]$CaseOrder = 'rotating',
    [ValidateRange(30, 300)]
    [int]$CaseTimeoutSeconds = 100,
    [string]$OutputPath = "",
    [string]$LogPath = ""
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$Sizes = @($Sizes | Sort-Object -Unique)
if ($Sizes.Count -eq 0 -or @($Sizes | Where-Object { $_ -lt 18 -or $_ -gt 30 }).Count -ne 0) {
    throw 'Calysto size certification accepts only one or more exact edges in 18..30.'
}
if (@($Seeds | Where-Object { $_ -le 0 }).Count -ne 0) {
    throw 'Every explicit Calysto matrix seed must be a positive Int64.'
}
if ($Seeds.Count -ne @($Seeds | Sort-Object -Unique).Count) {
    throw 'Explicit Calysto matrix seeds must be unique.'
}

$launcher = Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$pythonScript = Join-Path $ProjectRoot 'Tools\Migration\Validate-CalystoDungeonSizeMatrixPIE58.py'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$sizeLabel = ($Sizes -join '-')
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot "Saved\Migration\CalystoDungeonDirectorV3\SizeMatrix_${sizeLabel}_$stamp.json"
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $ProjectRoot "Saved\Migration\Logs\CalystoDungeonDirectorV3SizeMatrix_${sizeLabel}_$stamp.log"
}
foreach ($path in @($launcher, $pythonScript)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required Calysto size-matrix path is missing: $path"
    }
}

$environmentNames = @(
    'CODEX_RUN_MIGRATION_BASELINE_PIE',
    'CODEX_MIGRATION_PIE_SCRIPT',
    'CODEX_CALYSTO_SIZE_MATRIX_OUTPUT',
    'CODEX_CALYSTO_SIZE_MATRIX_LOG',
    'CODEX_CALYSTO_SIZE_MATRIX_SIZES',
    'CODEX_CALYSTO_SIZE_MATRIX_SEEDS_PER_SIZE',
    'CODEX_CALYSTO_SIZE_MATRIX_BASE_SEED',
    'CODEX_CALYSTO_SIZE_MATRIX_SEEDS',
    'CODEX_CALYSTO_SIZE_MATRIX_CASE_ORDER',
    'CODEX_CALYSTO_SIZE_MATRIX_CASE_TIMEOUT'
)
$oldEnvironment = @{}
foreach ($name in $environmentNames) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $pythonScript
    $env:CODEX_CALYSTO_SIZE_MATRIX_OUTPUT = $OutputPath
    $env:CODEX_CALYSTO_SIZE_MATRIX_LOG = $LogPath
    $env:CODEX_CALYSTO_SIZE_MATRIX_SIZES = $Sizes -join ','
    $env:CODEX_CALYSTO_SIZE_MATRIX_SEEDS_PER_SIZE = $SeedsPerSize.ToString(
        [System.Globalization.CultureInfo]::InvariantCulture
    )
    $env:CODEX_CALYSTO_SIZE_MATRIX_BASE_SEED = $BaseSeed.ToString(
        [System.Globalization.CultureInfo]::InvariantCulture
    )
    $env:CODEX_CALYSTO_SIZE_MATRIX_SEEDS = $Seeds -join ','
    $env:CODEX_CALYSTO_SIZE_MATRIX_CASE_ORDER = $CaseOrder.ToLowerInvariant()
    $env:CODEX_CALYSTO_SIZE_MATRIX_CASE_TIMEOUT = $CaseTimeoutSeconds.ToString(
        [System.Globalization.CultureInfo]::InvariantCulture
    )
    $arguments = '-unattended -nop4 -nosplash -NoSound -stdout -FullStdOutLogOutput -ABSLOG="' + $LogPath + '"'
    & $launcher -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
}

if (!(Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
    throw "Calysto size-matrix evidence was not produced: $OutputPath"
}
if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    throw "Calysto size-matrix log was not produced: $LogPath"
}

$result = Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
$expectedSeedsPerSize = if ($Seeds.Count -gt 0) { $Seeds.Count } else { $SeedsPerSize }
$expectedCases = $Sizes.Count * $expectedSeedsPerSize
$expectedSizeKey = (@($Sizes | ForEach-Object { [int]$_ } | Sort-Object -Unique) -join ',')
$candidateSizeKey = (@(
    $result.policy.candidate_validated_dungeon_sizes |
        ForEach-Object { [int]$_ } |
        Sort-Object -Unique
) -join ',')
$certifiedSizeKey = (@(
    $result.certified_sizes |
        ForEach-Object { [int]$_ } |
        Sort-Object -Unique
) -join ',')
if ([int]$result.schema_version -ne 1) {
    throw "Calysto size matrix emitted unsupported schema $($result.schema_version)."
}
if ([int]$result.case_count -ne $expectedCases -or [int]$result.completed_case_count -ne $expectedCases) {
    throw "Calysto size matrix completed $($result.completed_case_count)/$expectedCases cases."
}
if (@($result.asset_mutations).Count -ne 0 -or @($result.asset_saves).Count -ne 0) {
    throw 'Calysto size matrix mutated or saved Content assets.'
}
if (@($result.protected_assets.mismatches).Count -ne 0) {
    throw "Calysto size matrix changed protected assets: $(@($result.protected_assets.mismatches) -join ', ')"
}
if ([string]::IsNullOrWhiteSpace([string]$result.policy.uasset_sha256) -or
    @($result.policy.policy_hashes).Count -ne 1) {
    throw 'Calysto size matrix did not freeze one canonical policy hash and one V3 uasset hash.'
}
if ([string]::IsNullOrWhiteSpace([string]$result.engine_version) -or
    [int]$result.policy.schema_version -ne 3 -or
    [int]$result.policy.generator_version -ne 3 -or
    [string]$result.policy.policy_id -ne 'CalystoDungeonDirectorV3') {
    throw 'Calysto size matrix did not preserve the UE/V3 schema, generator, and policy identity contract.'
}
$initialPolicyProperties = @($result.policy.initial.PSObject.Properties.Name)
if ($initialPolicyProperties -notcontains 'validated_dungeon_sizes' -or
    [string]::IsNullOrWhiteSpace([string]$result.policy.initial.uasset_sha256) -or
    [int]$result.policy.initial.schema_version -ne 3 -or
    [int]$result.policy.initial.generator_version -ne 3 -or
    [string]$result.policy.initial.policy_id -ne 'CalystoDungeonDirectorV3') {
    throw 'Calysto size matrix did not preserve the initial V3 policy SHA and authored validated-size list.'
}
$initialAuthoredSizeKey = (@(
    $result.policy.initial.validated_dungeon_sizes |
        ForEach-Object { [int]$_ } |
        Sort-Object -Unique
) -join ',')
$finalAuthoredSizeKey = (@(
    $result.policy.validated_dungeon_sizes |
        ForEach-Object { [int]$_ } |
        Sort-Object -Unique
) -join ',')
if ([string]$result.policy.uasset_sha256 -ne [string]$result.policy.initial.uasset_sha256 -or
    $finalAuthoredSizeKey -ne $initialAuthoredSizeKey) {
    throw 'Calysto matrix changed the authored V3 policy SHA or its source ValidatedDungeonSizes.'
}
if ($candidateSizeKey -ne $expectedSizeKey) {
    throw "Calysto candidate validated sizes '$candidateSizeKey' did not match requested sizes '$expectedSizeKey'."
}
if ([string]$result.case_order -ne $CaseOrder.ToLowerInvariant()) {
    throw "Calysto receipt case order '$($result.case_order)' did not match requested order '$CaseOrder'."
}
if (![bool]$result.contract.candidate_command_executed_once_before_first_new_run -or
    [int]$result.contract.candidate_command_execution_count -ne 1 -or
    ![bool]$result.contract.candidate_command_verified_by_policy_hash -or
    ![bool]$result.contract.candidate_command_clear_requested -or
    ![bool]$result.contract.candidate_command_clear_verified_by_source_hash) {
    throw 'Calysto candidate validated-size override was not hash-verified when armed and cleared in the same PIE receipt.'
}
if ([string]$result.policy.candidate_policy_hash -ne [string]$result.policy.candidate_policy_observed_hash -or
    [string]$result.policy.candidate_policy_hash -ne [string]$result.policy.policy_hashes[0]) {
    throw 'Calysto candidate PolicyHash did not match the observed runtime PolicyHash.'
}

$invalidGenerateLocal = @(
    $result.cases | Where-Object { ![bool]$_.one_generate_local_per_attempt }
)
if ($invalidGenerateLocal.Count -ne 0) {
    throw "Calysto size matrix found a generation attempt without exactly one GenerateLocal: $($invalidGenerateLocal.Count) case(s)."
}

if (![bool]$result.success) {
    $failures = @(
        $result.cases |
            Where-Object { ![bool]$_.success } |
            ForEach-Object {
                $missing = @(
                    $_.checks.PSObject.Properties |
                        Where-Object { ![bool]$_.Value } |
                        ForEach-Object { $_.Name }
                )
                "edge=$($_.edge) seed=$($_.run_seed) reason=$($_.reason) missing=$($missing -join '+')"
            }
    )
    $receiptGates = @(
        "dirty_state=$([string]$result.asset_monitor.dirty_state_gate)",
        "global_log=$([string]$result.global_log_errors.gate_status)",
        "candidate_armed=$([bool]$result.contract.candidate_command_executed_once_before_first_new_run)",
        "candidate_cleared=$([bool]$result.contract.candidate_command_clear_requested)"
    )
    throw "Calysto exact-size certification failed. Evidence was preserved at $OutputPath`nReceipt gates: $($receiptGates -join '; ')`n$($failures -join [Environment]::NewLine)"
}

if ([string]$result.status -ne 'PASS') {
    throw "Calysto receipt reported status '$($result.status)' after success=true."
}
if ([string]$result.asset_monitor.dirty_state_gate -ne 'PASS' -or
    [string]$result.global_log_errors.gate_status -ne 'PASS') {
    throw 'Calysto receipt success was inconsistent with a non-PASS dirty-state or global-log gate.'
}
if ($expectedSeedsPerSize -ge 20 -and $certifiedSizeKey -ne $expectedSizeKey) {
    throw "All requested cases passed, but certified sizes '$certifiedSizeKey' did not exactly match '$expectedSizeKey'."
}

Write-Host 'Calysto Dungeon Director V3 exact-size matrix request: PASS'
if (@($result.certified_sizes).Count -gt 0) {
    Write-Host "Certified sizes (20+ unique seeds): $(@($result.certified_sizes) -join ', ')"
} else {
    Write-Host "Screening only; no size was certified because fewer than 20 unique seeds were requested."
}
Write-Host "Cases: $expectedCases"
Write-Host "Evidence: $OutputPath"
Write-Host "Log: $LogPath"
