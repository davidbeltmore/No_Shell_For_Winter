[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202608210404,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunSeed -le 0) { throw 'The V4 traversal PIE gate requires a positive Int64 seed.' }

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV4TraversalPIE58.py'
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV4\TraversalPIE_$Stamp"
$output = Join-Path $runRoot 'Traversal.json'
$log = Join-Path $runRoot 'Traversal.log'
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
$expectedGenerations = 19
$expectedDoorInteractions = 9
$protectedBpHash = '47A948D3037F6D3012663D5E1D59E4C996FC9EEC8E2EE65D1BE8C5C6A9E5800B'

foreach ($path in @($launcher, $receiptGuard, $validator)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required V4 traversal path is missing: $path"
    }
}
if (Test-Path -LiteralPath $runRoot) {
    throw "Traversal evidence already exists; refusing reuse: $runRoot"
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
$oldOutput = $env:CODEX_CALYSTO_V4_TRAVERSAL_OUTPUT
$oldSeed = $env:CODEX_CALYSTO_V4_TRAVERSAL_SEED

Assert-DazEditorReceipt -Phase 'pre-run'
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
    $env:CODEX_CALYSTO_V4_TRAVERSAL_OUTPUT = $output
    $env:CODEX_CALYSTO_V4_TRAVERSAL_SEED = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    $arguments = @(
        '-unattended'
        '-nop4'
        '-nosplash'
        '-NoSound'
        # Rendering is covered by the separate final visual-QA gate.
        '-NullRHI'
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
    $env:CODEX_CALYSTO_V4_TRAVERSAL_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V4_TRAVERSAL_SEED = $oldSeed
    Assert-DazEditorReceipt -Phase 'final'
}

foreach ($artifact in @($output, $log)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or (Get-Item -LiteralPath $artifact).Length -le 0) {
        throw "The traversal gate did not produce a non-empty artifact: $artifact"
    }
}

try {
    $receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
}
catch {
    throw "Traversal receipt is not valid JSON: $($_.Exception.Message)"
}
if (-not [bool]$receipt.success -or [string]$receipt.status -ne 'PASS') {
    throw "Traversal gate failed in phase $($receipt.phase): $($receipt.error)"
}
if ([int]$receipt.schema_version -ne 4 -or [int]$receipt.generator_version -ne 4) {
    throw 'Traversal receipt does not identify schema/generator V4.'
}
if ([int64]$receipt.run_seed -ne $RunSeed) {
    throw 'Traversal receipt run seed does not match the requested seed.'
}
if ([int]$receipt.expected_generation_count -ne $expectedGenerations -or [int]$receipt.expected_door_interactions -ne $expectedDoorInteractions) {
    throw 'Traversal receipt changed its strict generation/door contract.'
}
if ([string]$receipt.policy.class -cne '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4' -or
    [int]$receipt.policy.schema_version -ne 4 -or
    [int]$receipt.policy.generator_version -ne 4 -or
    [string]$receipt.policy.policy_id -cne 'CalystoDungeonDirectorV4' -or
    [string]$receipt.policy.policy_hash -notmatch '^[0-9A-F]{64}$') {
    throw 'Traversal receipt policy identity is not the exact native V4 authority.'
}
if (@($receipt.samples).Count -ne $expectedGenerations) {
    throw "Traversal receipt contains $(@($receipt.samples).Count) samples; expected $expectedGenerations."
}
if (@($receipt.door_interactions).Count -ne $expectedDoorInteractions) {
    throw "Traversal receipt contains $(@($receipt.door_interactions).Count) real door interactions; expected $expectedDoorInteractions."
}

$expectedLabels = @('new_run_floor_1','replay_floor_1','reroll_floor_1')
$expectedLabels += 2..10 | ForEach-Object { "advance_floor_$_" }
$expectedLabels += @(25,50,100,101,125,500,1000) | ForEach-Object { "jump_floor_$_" }
$actualLabels = @($receipt.samples | ForEach-Object { [string]$_.label })
if (($actualLabels -join '|') -cne ($expectedLabels -join '|')) {
    throw "Traversal sample order is invalid: $($actualLabels -join ', ')"
}
$doorFloors = @($receipt.door_interactions | ForEach-Object { [int]$_.from_floor })
if (($doorFloors -join ',') -ne ((1..9) -join ',')) {
    throw "Real ACF door source floors are invalid: $($doorFloors -join ', ')"
}
foreach ($interaction in @($receipt.door_interactions)) {
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

foreach ($sample in @($receipt.samples)) {
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
    throw "Traversal final checks failed: $($failedFinal -join ', ')"
}

if (@($receipt.asset_saves).Count -ne 0 -or @($receipt.asset_mutations).Count -ne 0 -or @($receipt.protected_assets.mismatches).Count -ne 0) {
    throw 'Traversal gate changed Content, dirtied a monitored package, or changed a protected file.'
}
if ([string]$receipt.policy_sha256_before -cne [string]$receipt.policy_sha256_after) {
    throw 'Traversal gate changed the authored V4 policy bytes.'
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
$generate = @($lines | Select-String -SimpleMatch 'Calysto V4 adapter requested GenerateLocal exactly once')
$pcg = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PCGComplete world=')
$nav = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: NavigationPathReady world=')
$levels = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: EnemyLevelsReady world=')
$population = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PopulationRealized world=')
$roster = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: CompanionRosterReady world=')
$door = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: DoorEnabled world=')
$readinessSeries = [ordered]@{
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
        throw "Traversal gate emitted $(@($entry.Value).Count) $($entry.Key) records; expected exactly $expectedGenerations."
    }
}
for ($index = 0; $index -lt $expectedGenerations; ++$index) {
    if (-not ($generate[$index].LineNumber -le $pcg[$index].LineNumber -and
        $pcg[$index].LineNumber -le $nav[$index].LineNumber -and
        $nav[$index].LineNumber -le $levels[$index].LineNumber -and
        $levels[$index].LineNumber -le $population[$index].LineNumber -and
        $population[$index].LineNumber -le $roster[$index].LineNumber -and
        $roster[$index].LineNumber -le $door[$index].LineNumber)) {
        throw "Traversal readiness sequence $index violated GenerateLocal <= PCG <= Nav <= EnemyLevels <= Population <= Companion <= Door."
    }
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
    throw "Traversal log contains blocked diagnostics:`n$($findings -join [Environment]::NewLine)"
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
    generation_count = $expectedGenerations
    generate_local_count = $generate.Count
    real_acf_door_interactions = $expectedDoorInteractions
    sequential_floor_range = '1-10'
    development_jump_floors = @(25,50,100,101,125,500,1000)
    readiness_order = 'GenerateLocal <= PCGComplete <= NavigationPathReady <= EnemyLevelsReady <= PopulationRealized <= CompanionRosterReady <= DoorEnabled'
    daz_receipt_checks = @($script:dazReceiptChecks)
    protected_bp_massive_dungeon_sha256 = $protectedBpHash
    samples = $receipt.samples
    operations = $receipt.operations
    final_checks = $receipt.final_checks
}
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))
Write-Host 'Dungeon Director V4 traversal PIE gate: PASS'
Write-Host "Evidence: $summaryPath"
