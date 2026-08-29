[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202608210425,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunSeed -le 0) { throw 'The V4 live-soak PIE gate requires a positive Int64 seed.' }

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV4LiveSoakPIE58.py'
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV4\LiveSoakPIE_$Stamp"
$output = Join-Path $runRoot 'LiveSoak.json'
$log = Join-Path $runRoot 'LiveSoak.log'
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
$expectedGenerations = 25
$expectedDoorInteractions = 24
$p95LimitSeconds = 25.76125
$absoluteLimitSeconds = 30.0
$workingSetRangeLimitBytes = 2GB
$protectedBpHash = '47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B'

foreach ($path in @($launcher, $receiptGuard, $validator)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required V4 live-soak path is missing: $path"
    }
}
if (Test-Path -LiteralPath $runRoot) {
    throw "Live-soak evidence already exists; refusing reuse: $runRoot"
}
[void][IO.Directory]::CreateDirectory($runRoot)

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

$oldBaseline = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldOutput = $env:CODEX_CALYSTO_V4_LIVE_SOAK_OUTPUT
$oldSeed = $env:CODEX_CALYSTO_V4_LIVE_SOAK_SEED

Assert-DazEditorReceipt -Phase 'pre-run'
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
    $env:CODEX_CALYSTO_V4_LIVE_SOAK_OUTPUT = $output
    $env:CODEX_CALYSTO_V4_LIVE_SOAK_SEED = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
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
    Assert-DazEditorReceipt -Phase 'post-run'
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V4_LIVE_SOAK_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V4_LIVE_SOAK_SEED = $oldSeed
    Assert-DazEditorReceipt -Phase 'final'
}

foreach ($artifact in @($output, $log)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or (Get-Item -LiteralPath $artifact).Length -le 0) {
        throw "The live-soak gate did not produce a non-empty artifact: $artifact"
    }
}

try {
    $receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
}
catch {
    throw "Live-soak receipt is not valid JSON: $($_.Exception.Message)"
}
if (-not [bool]$receipt.success -or [string]$receipt.status -ne 'PASS') {
    throw "Live-soak gate failed in phase $($receipt.phase): $($receipt.error)"
}
if ([int]$receipt.schema_version -ne 4 -or [int]$receipt.generator_version -ne 4) {
    throw 'Live-soak receipt does not identify schema/generator V4.'
}
if ([int64]$receipt.run_seed -ne $RunSeed) {
    throw 'Live-soak receipt run seed does not match the requested seed.'
}
if ([int]$receipt.expected_generation_count -ne $expectedGenerations -or [int]$receipt.expected_door_interactions -ne $expectedDoorInteractions) {
    throw 'Live-soak receipt changed its strict generation/door contract.'
}
if ([string]$receipt.policy.class -cne '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4' -or
    [int]$receipt.policy.schema_version -ne 4 -or
    [int]$receipt.policy.generator_version -ne 4 -or
    [string]$receipt.policy.policy_id -cne 'CalystoDungeonDirectorV4' -or
    [string]$receipt.policy.policy_hash -notmatch '^[0-9A-F]{64}$') {
    throw 'Live-soak receipt policy identity is not the exact native V4 authority.'
}

$samples = @($receipt.samples)
$interactions = @($receipt.door_interactions)
if ($samples.Count -ne $expectedGenerations) {
    throw "Live-soak receipt contains $($samples.Count) samples; expected $expectedGenerations."
}
if ($interactions.Count -ne $expectedDoorInteractions) {
    throw "Live-soak receipt contains $($interactions.Count) real door interactions; expected $expectedDoorInteractions."
}
$expectedLabels = @('new_run_floor_1')
$expectedLabels += 2..25 | ForEach-Object { "advance_floor_$_" }
$actualLabels = @($samples | ForEach-Object { [string]$_.label })
if (($actualLabels -join '|') -cne ($expectedLabels -join '|')) {
    throw "Live-soak sample order is invalid: $($actualLabels -join ', ')"
}
$actualFloors = @($samples | ForEach-Object { [int]$_.floor })
$actualSerials = @($samples | ForEach-Object { [int64]$_.serial })
if (($actualFloors -join ',') -ne ((1..25) -join ',') -or ($actualSerials -join ',') -ne ((1..25) -join ',')) {
    throw 'Live-soak floor or generation-serial sequence is not exactly 1 through 25.'
}
$doorFloors = @($interactions | ForEach-Object { [int]$_.from_floor })
if (($doorFloors -join ',') -ne ((1..24) -join ',')) {
    throw "Real ACF door source floors are invalid: $($doorFloors -join ', ')"
}
foreach ($interaction in $interactions) {
    if ([string]$interaction.source -cne 'real_acf_interaction_component' -or
        [string]$interaction.operation -cne 'advance_via_real_acf_door' -or
        -not [bool]$interaction.dispatched -or
        -not [bool]$interaction.disabled_before_readiness -or
        [int]$interaction.selection_samples -lt 3 -or
        [int]$interaction.to_floor -ne ([int]$interaction.from_floor + 1) -or
        [int64]$interaction.to_serial -ne ([int64]$interaction.from_serial + 1)) {
        throw "Invalid real ACF door interaction evidence at Floor $($interaction.from_floor)."
    }
}

foreach ($sample in $samples) {
    $failedChecks = @(
        $sample.checks.PSObject.Properties |
            Where-Object { -not [bool]$_.Value } |
            ForEach-Object { $_.Name }
    )
    if ($failedChecks.Count -ne 0) {
        throw "Sample $($sample.label) has failed checks: $($failedChecks -join ', ')"
    }
    if (-not [bool]$sample.door.disabled_before_ready_observed -or -not [bool]$sample.door.enabled) {
        throw "Sample $($sample.label) did not prove disabled-before-ready and enabled-after-ready."
    }
    if ([int]$sample.runtime.residue_actor_count -ne 0 -or [int]$sample.runtime.anchors_after_ready -ne 0) {
        throw "Sample $($sample.label) retained runtime residue or anchors."
    }
    if ([int]$sample.runtime.population_actor_count -ne [int]$sample.spawned_actor_count) {
        throw "Sample $($sample.label) live population differs from its realized manifest."
    }
    if ([string]$sample.memory.measurement_status -cne 'PASS' -or
        [int64]$sample.memory.process_working_set_bytes -le 0 -or
        [int64]$sample.memory.system_available_physical_bytes -le 0) {
        throw "Sample $($sample.label) lacks valid stable memory measurements."
    }
    foreach ($hashProperty in $sample.hashes.PSObject.Properties) {
        if ([string]$hashProperty.Value -notmatch '^[0-9A-F]{64}$') {
            throw "Sample $($sample.label) contains invalid $($hashProperty.Name) hash."
        }
    }
}

$failedFinal = @(
    $receipt.final_checks.PSObject.Properties |
        Where-Object { -not [bool]$_.Value } |
        ForEach-Object { $_.Name }
)
if ($failedFinal.Count -ne 0) {
    throw "Live-soak final checks failed: $($failedFinal -join ', ')"
}
$metrics = $receipt.soak_metrics
if ([string]$metrics.measurement_status -cne 'PASS' -or
    [int]$metrics.generation_count -ne $expectedGenerations -or
    [int]$metrics.real_acf_door_interactions -ne $expectedDoorInteractions) {
    throw 'Live-soak metrics are incomplete or changed their exact count contract.'
}
$p95 = [double]$metrics.floor_ready_seconds.p95
$maximumReady = [double]$metrics.floor_ready_seconds.maximum
if ([string]$metrics.floor_ready_seconds.p95_method -cne 'nearest-rank' -or
    $p95 -gt $p95LimitSeconds -or $maximumReady -ge $absoluteLimitSeconds) {
    throw "Live-soak Floor Ready performance failed: P95=$p95 max=$maximumReady."
}
if ([int64]$metrics.process_working_set_bytes.range_post_warmup -gt $workingSetRangeLimitBytes -or
    [bool]$metrics.process_working_set_bytes.monotonic_non_decreasing_growth) {
    throw 'Live-soak process working set is unbounded or monotonically growing after warmup.'
}
if ([bool]$metrics.population_adjusted_world_actor_count.monotonic_non_decreasing_growth) {
    throw 'Live-soak active-world actor count grows monotonically after population adjustment.'
}
if ([string]$metrics.process_working_set_bytes.api -cne 'Win32 GetProcessMemoryInfo' -or
    [string]$metrics.system_available_physical_bytes.api -cne 'Win32 GlobalMemoryStatusEx') {
    throw 'Live-soak memory measurements do not identify the required stable Win32 APIs.'
}

if (@($receipt.asset_saves).Count -ne 0 -or
    @($receipt.dirty_changes.PSObject.Properties).Count -ne 0 -or
    @($receipt.protected_assets.mismatches).Count -ne 0) {
    throw 'Live-soak gate changed Content, changed a monitored dirty state, or changed a protected file.'
}
if ([string]$receipt.policy_sha256_before -cne [string]$receipt.policy_sha256_after) {
    throw 'Live-soak gate changed the authored V4 policy bytes.'
}
$bpPath = 'Content/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.uasset'
$bpBeforeProperty = $receipt.protected_assets.before.PSObject.Properties[$bpPath]
$bpAfterProperty = $receipt.protected_assets.after.PSObject.Properties[$bpPath]
if ($null -eq $bpBeforeProperty -or $null -eq $bpAfterProperty) {
    throw 'BP_MassiveDungeon is missing from protected pre/post hash evidence.'
}
$bpBefore = [string]$bpBeforeProperty.Value.sha256
$bpAfter = [string]$bpAfterProperty.Value.sha256
if ($bpBefore -cne $protectedBpHash -or $bpAfter -cne $protectedBpHash) {
    throw 'BP_MassiveDungeon failed its protected pre/post hash baseline.'
}

$lines = Get-Content -LiteralPath $log
$opening = @($lines | Select-String -SimpleMatch 'Opening Calysto V4 run=')
$accepted = @($lines | Select-String -SimpleMatch 'Calysto Director V4 accepted:')
$generate = @($lines | Select-String -SimpleMatch 'Calysto V4 adapter requested GenerateLocal exactly once')
$pcg = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PCGComplete world=')
$nav = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: NavigationPathReady world=')
$levels = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: EnemyLevelsReady world=')
$population = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PopulationRealized world=')
$roster = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: CompanionRosterReady world=')
$door = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: DoorEnabled world=')
$readinessSeries = [ordered]@{
    Opening = $opening
    Accepted = $accepted
    GenerateLocal = $generate
    PCGComplete = $pcg
    NavigationPathReady = $nav
    EnemyLevelsReady = $levels
    PopulationRealized = $population
    CompanionRosterReady = $roster
    DoorEnabled = $door
}
foreach ($entry in $readinessSeries.GetEnumerator()) {
    if (@($entry.Value).Count -ne $expectedGenerations) {
        throw "Live-soak gate emitted $(@($entry.Value).Count) $($entry.Key) records; expected exactly $expectedGenerations."
    }
}

$readinessSamples = [Collections.Generic.List[object]]::new()
for ($index = 0; $index -lt $expectedGenerations; ++$index) {
    if (-not ($opening[$index].LineNumber -le $accepted[$index].LineNumber -and
        $accepted[$index].LineNumber -le $generate[$index].LineNumber -and
        $generate[$index].LineNumber -le $pcg[$index].LineNumber -and
        $pcg[$index].LineNumber -le $nav[$index].LineNumber -and
        $nav[$index].LineNumber -le $levels[$index].LineNumber -and
        $levels[$index].LineNumber -le $population[$index].LineNumber -and
        $population[$index].LineNumber -le $roster[$index].LineNumber -and
        $roster[$index].LineNumber -le $door[$index].LineNumber)) {
        throw "Live-soak readiness sequence $index violated Opening <= Accepted <= GenerateLocal <= PCG <= Nav <= EnemyLevels <= Population <= Companion <= Door."
    }
    if ($nav[$index].Line -notmatch 'NavigationPathReady world=.*start=.*door=.*attempts=\d+') {
        throw "Live-soak navigation record $index does not prove a native Start-to-Door path check."
    }
    $floor = $index + 1
    if ($opening[$index].Line -notmatch "floor=$floor\s+generation=$floor\s+") {
        throw "Live-soak opening record $index does not match Floor/serial $floor."
    }
    $readinessSamples.Add([ordered]@{
        floor = $floor
        opening_line = $opening[$index].LineNumber
        accepted_line = $accepted[$index].LineNumber
        generate_local_line = $generate[$index].LineNumber
        pcg_complete_line = $pcg[$index].LineNumber
        navigation_path_ready_line = $nav[$index].LineNumber
        enemy_levels_ready_line = $levels[$index].LineNumber
        population_realized_line = $population[$index].LineNumber
        companion_roster_ready_line = $roster[$index].LineNumber
        door_enabled_line = $door[$index].LineNumber
        start_to_door_log = $nav[$index].Line.Trim()
    })
}

$blockedPatterns = @(
    'Blueprint Runtime Error',
    'LogBlueprint: Error',
    'Accessed None',
    'Ensure condition failed',
    'Fatal error:',
    'Assertion failed:',
    'Object Transform',
    'GetAttributeFromPointIndex_0',
    'Calysto controlled generation failed',
    'Calysto generation failed',
    'GenerateLocal was already requested',
    'duplicate generation',
    'duplicated generation',
    'FLOOR_READY_REJECTED',
    'COMPANION_SNAPSHOT_DRIFT',
    'LogEFCalystoDungeon: Error',
    'LogEFCalystoPopulationV4: Error',
    'LogEFProceduralPCGRuntime: Error',
    'LogProjectRunCompanions: Error',
    'LogEFCalystoFloorDoor: Error',
    'LogEFCalystoFloorDoor: Warning',
    'PolicyV3',
    'PlanV3',
    'THEME_V3',
    'CALYSTO_PHASE2'
)
$findings = @(
    foreach ($pattern in $blockedPatterns) {
        $lines | Select-String -SimpleMatch $pattern | ForEach-Object { "[$pattern] $($_.Line.Trim())" }
    }
)
if ($findings.Count -ne 0) {
    throw "Live-soak log contains blocked diagnostics:`n$($findings -join [Environment]::NewLine)"
}
if (($script:dazReceiptChecks -join '|') -cne 'pre-run|post-run|final') {
    throw "Daz receipt phases are incomplete: $($script:dazReceiptChecks -join ', ')"
}

$summary = [ordered]@{
    schema_version = 4
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'PASS'
    run_seed = $RunSeed
    receipt = $output
    log = $log
    expected_generation_count = $expectedGenerations
    accepted_generation_count = $samples.Count
    generate_local_count = $generate.Count
    real_acf_door_interactions = $interactions.Count
    sequential_floor_range = '1-25'
    floor_ready_p95_seconds = $p95
    floor_ready_p95_limit_seconds = $p95LimitSeconds
    floor_ready_maximum_seconds = $maximumReady
    floor_ready_absolute_limit_seconds = $absoluteLimitSeconds
    memory_measurement = [ordered]@{
        status = 'PASS'
        process_api = [string]$metrics.process_working_set_bytes.api
        system_api = [string]$metrics.system_available_physical_bytes.api
        working_set_range_post_warmup = [int64]$metrics.process_working_set_bytes.range_post_warmup
        working_set_allowed_range = $workingSetRangeLimitBytes
        working_set_monotonic_growth = [bool]$metrics.process_working_set_bytes.monotonic_non_decreasing_growth
        available_physical_final = [int64]$metrics.system_available_physical_bytes.final
    }
    actor_trend = $metrics.population_adjusted_world_actor_count
    residue_criterion = [string]$metrics.residue_criterion
    trend_criterion = [string]$metrics.trend_criterion
    readiness_order = 'Opening <= Accepted <= GenerateLocal <= PCGComplete <= NavigationPathReady <= EnemyLevelsReady <= PopulationRealized <= CompanionRosterReady <= DoorEnabled'
    readiness_samples = @($readinessSamples)
    daz_receipt_checks = @($script:dazReceiptChecks)
    protected_bp_massive_dungeon_sha256 = $protectedBpHash
    samples = $samples
    door_interactions = $interactions
    soak_metrics = $metrics
    final_checks = $receipt.final_checks
}
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Host 'Dungeon Director V4 live-soak PIE gate: PASS'
Write-Host "Evidence: $summaryPath"
