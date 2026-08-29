[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [ValidateRange(1, [Int64]::MaxValue)]
    [Int64]$BaseSeed = 202608212600,
    [ValidateRange(30, 300)]
    [int]$CaseTimeoutSeconds = 100,
    [string]$RevalidateEvidenceRoot = '',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($BaseSeed -gt ([Int64]::MaxValue - 30019)) {
    throw 'BaseSeed is too large to derive all 100 positive Int64 case seeds safely.'
}

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV4SizeMatrixPIE58.py'
$isRevalidation = -not [string]::IsNullOrWhiteSpace($RevalidateEvidenceRoot)
$runRoot = if ($isRevalidation) {
    (Resolve-Path -LiteralPath $RevalidateEvidenceRoot).Path
}
else {
    Join-Path $root "Saved\Migration\CalystoDungeonDirectorV4\SizeMatrixPIE_$Stamp"
}
$output = Join-Path $runRoot 'SizeMatrix.json'
$log = Join-Path $runRoot 'SizeMatrix.log'
$summaryPath = Join-Path $runRoot $(if ($isRevalidation) {
    'StrictSummaryRevalidated.json'
}
else {
    'StrictSummary.json'
})
$expectedSizes = @(26, 27, 28, 29, 30)
$expectedSeedsPerSize = 20
$expectedCases = 100
$baselineP95Seconds = 20.609
$p95LimitSeconds = $baselineP95Seconds * 1.25
$absoluteLimitSeconds = 30.0
$protectedBpHash = '47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B'

foreach ($path in @($launcher, $receiptGuard, $validator)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required V4 size-matrix path is missing: $path"
    }
}
if (-not $isRevalidation -and (Test-Path -LiteralPath $runRoot)) {
    throw "Size-matrix evidence already exists; refusing reuse: $runRoot"
}
if (-not $isRevalidation) {
    [void][IO.Directory]::CreateDirectory($runRoot)
}
elseif (-not (Test-Path -LiteralPath $runRoot -PathType Container)) {
    throw "The V4 size-matrix evidence root is not a directory: $runRoot"
}

$script:dazReceiptChecks = [Collections.Generic.List[string]]::new()
function Assert-DazEditorReceipt {
    param([Parameter(Mandatory = $true)][string]$Phase)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $root `
        -TargetName 'NoShellForWinterEditor' `
        -Configuration 'Development' `
        -VerifyOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Daz editor receipt verification failed during $Phase with exit code $LASTEXITCODE."
    }
    $script:dazReceiptChecks.Add($Phase)
}

$environmentNames = @(
    'CODEX_RUN_MIGRATION_BASELINE_PIE',
    'CODEX_MIGRATION_PIE_SCRIPT',
    'CODEX_CALYSTO_V4_SIZE_MATRIX_OUTPUT',
    'CODEX_CALYSTO_V4_SIZE_MATRIX_LOG',
    'CODEX_CALYSTO_V4_SIZE_MATRIX_BASE_SEED',
    'CODEX_CALYSTO_V4_SIZE_MATRIX_CASE_TIMEOUT'
)
$oldEnvironment = @{}
foreach ($name in $environmentNames) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

$failure = ''
$receipt = $null
$recomputedP95 = $null
$readinessCounts = [ordered]@{}
try {
    Assert-DazEditorReceipt -Phase $(if ($isRevalidation) { 'pre-revalidation' } else { 'pre-run' })
    if (-not $isRevalidation) {
        $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
        $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
        $env:CODEX_CALYSTO_V4_SIZE_MATRIX_OUTPUT = $output
        $env:CODEX_CALYSTO_V4_SIZE_MATRIX_LOG = $log
        $env:CODEX_CALYSTO_V4_SIZE_MATRIX_BASE_SEED = $BaseSeed.ToString(
            [Globalization.CultureInfo]::InvariantCulture
        )
        $env:CODEX_CALYSTO_V4_SIZE_MATRIX_CASE_TIMEOUT = $CaseTimeoutSeconds.ToString(
            [Globalization.CultureInfo]::InvariantCulture
        )
        $arguments = @(
            '-unattended'
            '-nop4'
            '-nosplash'
            '-NoSound'
            '-stdout'
            '-FullStdOutLogOutput'
            "-ABSLOG=`"$log`""
        ) -join ' '
        & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
        if ($LASTEXITCODE -ne 0) {
            throw "Protected launcher failed with exit code $LASTEXITCODE."
        }
    }
    Assert-DazEditorReceipt -Phase $(if ($isRevalidation) { 'post-revalidation' } else { 'post-run' })

    foreach ($artifact in @($output, $log)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or
            (Get-Item -LiteralPath $artifact).Length -le 0) {
            throw "The V4 size matrix did not produce a non-empty artifact: $artifact"
        }
    }
    try {
        $receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
    }
    catch {
        throw "The V4 size-matrix receipt is not valid JSON: $($_.Exception.Message)"
    }

    if (-not [bool]$receipt.success -or [string]$receipt.status -cne 'PASS') {
        throw "V4 size matrix failed in phase $($receipt.phase): $($receipt.error)"
    }
    if ([int]$receipt.schema_version -ne 4 -or [int]$receipt.generator_version -ne 4) {
        throw 'The V4 size-matrix receipt does not identify schema/generator 4.'
    }
    if ([int64]$receipt.base_seed -ne $BaseSeed -or
        [int]$receipt.case_count -ne $expectedCases -or
        [int]$receipt.completed_case_count -ne $expectedCases -or
        [int]$receipt.seeds_per_size -ne $expectedSeedsPerSize) {
        throw 'The V4 size matrix did not execute its exact 100-case seed contract.'
    }
    $requestedSizes = @($receipt.requested_sizes | ForEach-Object { [int]$_ })
    if (($requestedSizes -join ',') -cne ($expectedSizes -join ',')) {
        throw "Requested V4 sizes are invalid: $($requestedSizes -join ',')"
    }
    if ([string]$receipt.policy.object_path -cne '/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy' -or
        [string]$receipt.policy.class -cne '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4' -or
        [int]$receipt.policy.schema_version -ne 4 -or
        [int]$receipt.policy.generator_version -ne 4 -or
        [string]$receipt.policy.policy_id -cne 'CalystoDungeonDirectorV4' -or
        [string]$receipt.policy.policy_hash -notmatch '^[0-9A-F]{64}$' -or
        [string]$receipt.policy.candidate_policy_hash -notmatch '^[0-9A-F]{64}$') {
        throw 'The exact native V4 policy authority was not preserved.'
    }
    if (($receipt.policy.validated_dungeon_sizes -join ',') -cne ($expectedSizes -join ',') -or
        ($receipt.policy.candidate_validated_dungeon_sizes -join ',') -cne ($expectedSizes -join ',')) {
        throw 'The authored/candidate V4 validated-size set is not exactly 26..30.'
    }
    if (-not [bool]$receipt.contract.candidate_command_executed_once_before_first_new_run -or
        [int]$receipt.contract.candidate_command_execution_count -ne 1 -or
        -not [bool]$receipt.contract.candidate_command_verified_by_policy_hash -or
        -not [bool]$receipt.contract.candidate_command_clear_requested -or
        -not [bool]$receipt.contract.candidate_command_clear_verified_by_source_hash) {
        throw 'The transient candidate policy was not armed once and restored safely.'
    }
    if ([string]$receipt.policy.policy_sha256_before -notmatch '^[0-9A-F]{64}$' -or
        [string]$receipt.policy.policy_sha256_before -cne [string]$receipt.policy.policy_sha256_after) {
        throw 'The authored V4 policy bytes changed during the matrix.'
    }
    if (@($receipt.asset_saves).Count -ne 0 -or
        @($receipt.asset_mutations).Count -ne 0 -or
        @($receipt.dirty_state.newly_dirty).Count -ne 0 -or
        @($receipt.dirty_state.monitored_transitions).Count -ne 0 -or
        @($receipt.protected_assets.mismatches).Count -ne 0) {
        throw 'The V4 matrix saved, mutated, newly dirtied, or changed a protected asset.'
    }
    if ([string]$receipt.dirty_state.error) {
        throw "Dirty-state evidence is incomplete: $($receipt.dirty_state.error)"
    }

    $bpPath = 'Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset'
    $bpBeforeProperty = $receipt.protected_assets.before.PSObject.Properties[$bpPath]
    $bpAfterProperty = $receipt.protected_assets.after.PSObject.Properties[$bpPath]
    if ($null -eq $bpBeforeProperty -or $null -eq $bpAfterProperty -or
        [string]$bpBeforeProperty.Value.sha256 -cne $protectedBpHash -or
        [string]$bpAfterProperty.Value.sha256 -cne $protectedBpHash) {
        throw 'BP_MassiveDungeon failed its protected pre/post SHA-256 baseline.'
    }

    $cases = @($receipt.cases)
    if ($cases.Count -ne $expectedCases) {
        throw "The receipt contains $($cases.Count) cases; expected exactly $expectedCases."
    }
    $caseKeys = @($cases | ForEach-Object { "$([int]$_.edge):$([int]$_.seed_index)" })
    if (@($caseKeys | Sort-Object -Unique).Count -ne $expectedCases) {
        throw 'The receipt contains duplicate edge/seed-index cases.'
    }
    foreach ($edge in $expectedSizes) {
        $edgeCases = @($cases | Where-Object { [int]$_.edge -eq $edge })
        if ($edgeCases.Count -ne $expectedSeedsPerSize -or
            @($edgeCases.run_seed | Sort-Object -Unique).Count -ne $expectedSeedsPerSize) {
            throw "Edge $edge does not contain exactly 20 unique seeds."
        }
    }
    foreach ($case in $cases) {
        $failedChecks = @(
            $case.checks.PSObject.Properties |
                Where-Object { -not [bool]$_.Value } |
                ForEach-Object { $_.Name }
        )
        if (-not [bool]$case.success -or $failedChecks.Count -ne 0) {
            throw "V4 case $($case.case_number) edge=$($case.edge) seed=$($case.run_seed) failed: $($failedChecks -join ', ')"
        }
        if (($case.dungeon_size -join ',') -cne "$($case.edge),$($case.edge),1") {
            throw "V4 case $($case.case_number) did not realize its exact requested edge."
        }
        if ([int]$case.readiness.counts.generate_local -ne 1 -or
            [double]$case.duration_seconds.generate_local_to_door_enabled -lt 0.0 -or
            [double]$case.duration_seconds.generate_local_to_door_enabled -ge $absoluteLimitSeconds) {
            throw "V4 case $($case.case_number) violated GenerateLocal or the <30s Floor Ready limit."
        }
        foreach ($hash in $case.hashes.PSObject.Properties) {
            if ([string]$hash.Value -notmatch '^[0-9A-F]{64}$') {
                throw "V4 case $($case.case_number) contains invalid hash $($hash.Name)."
            }
        }
    }

    $durations = @(
        $cases |
            ForEach-Object { [double]$_.duration_seconds.generate_local_to_door_enabled } |
            Sort-Object
    )
    $p95Index = [Math]::Ceiling(0.95 * $durations.Count) - 1
    $recomputedP95 = [double]$durations[$p95Index]
    if ([Math]::Abs($recomputedP95 - [double]$receipt.performance.p95_ready_seconds) -gt 0.001) {
        throw 'The receipt P95 does not match the nearest-rank recomputation.'
    }
    if ($recomputedP95 -gt $p95LimitSeconds -or $recomputedP95 -ge $absoluteLimitSeconds) {
        throw "Floor Ready P95 $recomputedP95 exceeded the limit $p95LimitSeconds seconds."
    }

    $lines = Get-Content -LiteralPath $log
    # The Python receipt is also logged as one large JSON line and contains
    # copies of each readiness line. Anchor every match to an actual runtime
    # log prefix so evidence serialization cannot masquerade as telemetry.
    $runtimeLogPrefix = '^\[[^\]]+\]\[[^\]]+\]LogEFProceduralPCGRuntime: '
    $series = [ordered]@{
        GenerateLocal = @($lines | Select-String -Pattern ($runtimeLogPrefix + 'Calysto V4 adapter requested GenerateLocal exactly once'))
        PCGComplete = @($lines | Select-String -Pattern ($runtimeLogPrefix + 'PCGComplete world='))
        NavigationPathReady = @($lines | Select-String -Pattern ($runtimeLogPrefix + 'NavigationPathReady world='))
        EnemyLevelsReady = @($lines | Select-String -Pattern ($runtimeLogPrefix + 'EnemyLevelsReady world='))
        PopulationRealized = @($lines | Select-String -Pattern ($runtimeLogPrefix + 'PopulationRealized world='))
        CompanionRosterReady = @($lines | Select-String -Pattern ($runtimeLogPrefix + 'CompanionRosterReady world='))
        DoorEnabled = @($lines | Select-String -Pattern ($runtimeLogPrefix + 'DoorEnabled world='))
    }
    foreach ($entry in $series.GetEnumerator()) {
        $readinessCounts[$entry.Key] = @($entry.Value).Count
        if (@($entry.Value).Count -ne $expectedCases) {
            throw "The log contains $(@($entry.Value).Count) $($entry.Key) records; expected 100."
        }
    }
    for ($index = 0; $index -lt $expectedCases; ++$index) {
        if (-not (
            $series.GenerateLocal[$index].LineNumber -le $series.PCGComplete[$index].LineNumber -and
            $series.PCGComplete[$index].LineNumber -le $series.NavigationPathReady[$index].LineNumber -and
            $series.NavigationPathReady[$index].LineNumber -le $series.EnemyLevelsReady[$index].LineNumber -and
            $series.EnemyLevelsReady[$index].LineNumber -le $series.PopulationRealized[$index].LineNumber -and
            $series.PopulationRealized[$index].LineNumber -le $series.CompanionRosterReady[$index].LineNumber -and
            $series.CompanionRosterReady[$index].LineNumber -le $series.DoorEnabled[$index].LineNumber
        )) {
            throw "V4 readiness sequence $index violated the required telemetry order."
        }
    }
    $candidateArms = @($lines | Select-String -SimpleMatch 'CALYSTO_CERTIFICATION_POLICY_CANDIDATE')
    $candidateClears = @($lines | Select-String -SimpleMatch 'Development automation cleared the transient candidate policy')
    if ($candidateArms.Count -ne 1 -or $candidateClears.Count -ne 1) {
        throw 'The log did not prove exactly one transient candidate arm and clear.'
    }
    $blockedPatterns = @(
        'Blueprint Runtime Error', 'LogBlueprint: Error', 'Accessed None',
        'Ensure condition failed', 'Fatal error:', 'Assertion failed:',
        'Object Transform', 'GetAttributeFromPointIndex_0', 'cancelled or cleaned',
        'GenerateLocal was already requested', 'duplicate generation', 'duplicated generation',
        'FLOOR_READY_REJECTED', 'COMPANION_SNAPSHOT_DRIFT',
        'Calysto controlled generation failed', 'Calysto generation failed',
        'LogPCG: Error', 'LogEFCalystoDungeon: Error',
        'LogEFCalystoPopulationV4: Error', 'LogEFProceduralPCGRuntime: Error',
        'LogProjectRunCompanions: Error', 'LogEFCalystoFloorDoor: Error',
        'LogEFCalystoFloorDoor: Warning', 'PolicyV3', 'PlanV3', 'THEME_V3',
        'CALYSTO_PHASE2'
    )
    $findings = @(
        foreach ($pattern in $blockedPatterns) {
            $lines | Select-String -SimpleMatch $pattern | ForEach-Object {
                "[$pattern] $($_.Line.Trim())"
            }
        }
    )
    if ($findings.Count -ne 0) {
        throw "The V4 size-matrix log contains blocked diagnostics:`n$($findings -join [Environment]::NewLine)"
    }
    if ([string]$receipt.global_log_errors.gate_status -cne 'PASS' -or
        [int]$receipt.global_log_errors.actionable_count -ne 0) {
        throw 'The whole-log fail-closed error scan did not pass.'
    }
    $failedFinal = @(
        $receipt.final_checks.PSObject.Properties |
            Where-Object { -not [bool]$_.Value } |
            ForEach-Object { $_.Name }
    )
    if ($failedFinal.Count -ne 0) {
        throw "V4 final receipt checks failed: $($failedFinal -join ', ')"
    }
}
catch {
    $failure = $_.Exception.Message
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], 'Process')
    }
    try {
        Assert-DazEditorReceipt -Phase 'final'
    }
    catch {
        if ([string]::IsNullOrWhiteSpace($failure)) {
            $failure = $_.Exception.Message
        }
        else {
            $failure += " Final Daz receipt check also failed: $($_.Exception.Message)"
        }
    }
}

$summary = [ordered]@{
    schema_version = 4
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = if ([string]::IsNullOrWhiteSpace($failure)) { 'PASS' } else { 'FAIL' }
    error = $failure
    validation_mode = if ($isRevalidation) { 'ImmutableEvidenceRevalidation' } else { 'LivePIE' }
    source_strict_summary = if ($isRevalidation) { Join-Path $runRoot 'StrictSummary.json' } else { $null }
    sizes = $expectedSizes
    seeds_per_size = $expectedSeedsPerSize
    expected_cases = $expectedCases
    completed_cases = if ($null -ne $receipt) { [int]$receipt.completed_case_count } else { 0 }
    base_seed = $BaseSeed
    comparable_v3_p95_seconds = $baselineP95Seconds
    p95_limit_seconds = $p95LimitSeconds
    measured_p95_seconds = $recomputedP95
    readiness_counts = $readinessCounts
    readiness_order = 'GenerateLocal <= PCGComplete <= NavigationPathReady <= EnemyLevelsReady <= PopulationRealized <= CompanionRosterReady <= DoorEnabled'
    daz_receipt_checks = @($script:dazReceiptChecks)
    protected_bp_massive_dungeon_sha256 = $protectedBpHash
    receipt = $output
    log = $log
    cases = if ($null -ne $receipt) { $receipt.cases } else { @() }
    final_checks = if ($null -ne $receipt) { $receipt.final_checks } else { $null }
}
[IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 40),
    [Text.UTF8Encoding]::new($false)
)

if (-not [string]::IsNullOrWhiteSpace($failure)) {
    throw "Dungeon Director V4 size matrix: FAIL. Strict summary: $summaryPath`n$failure"
}
$expectedDazPhases = if ($isRevalidation) {
    'pre-revalidation|post-revalidation|final'
}
else {
    'pre-run|post-run|final'
}
if (($script:dazReceiptChecks -join '|') -cne $expectedDazPhases) {
    throw "Daz receipt phases are incomplete: $($script:dazReceiptChecks -join ', ')"
}

Write-Host $(if ($isRevalidation) {
    'Dungeon Director V4 exact-size matrix immutable evidence revalidation: PASS'
}
else {
    'Dungeon Director V4 exact-size matrix PIE gate: PASS'
})
Write-Host "Cases: $expectedCases (20 per edge 26..30)"
Write-Host "Floor Ready P95: $recomputedP95 s (limit $p95LimitSeconds s)"
Write-Host "Evidence: $summaryPath"
