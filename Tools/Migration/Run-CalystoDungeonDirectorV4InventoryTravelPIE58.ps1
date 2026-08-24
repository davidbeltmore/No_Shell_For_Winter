[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202608210505,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunSeed -le 0) { throw 'The V4 inventory travel gate requires a positive Int64 seed.' }

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV4InventoryTravelPIE58.py'
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV4\InventoryTravelPIE_$Stamp"
$output = Join-Path $runRoot 'InventoryTravel.json'
$log = Join-Path $runRoot 'InventoryTravel.log'
foreach ($path in @($launcher, $receiptGuard, $validator)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required path is missing: $path" }
}
if (Test-Path -LiteralPath $runRoot) { throw "Evidence already exists; refusing reuse: $runRoot" }
[void][IO.Directory]::CreateDirectory($runRoot)

function Assert-DazEditorReceipt {
    param([Parameter(Mandatory = $true)][string]$Phase)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $root -TargetName NoShellForWinterEditor -Configuration Development -VerifyOnly
    if ($LASTEXITCODE -ne 0) { throw "Daz editor receipt verification failed during $Phase." }
}

function Get-FixtureFields {
    param(
        [Parameter(Mandatory = $true)]$Receipt,
        [Parameter(Mandatory = $true)][string]$Operation
    )
    $record = $Receipt.inventory_travel.$Operation
    if ($null -eq $record -or $null -eq $record.fixture -or $null -eq $record.fixture.fields) {
        throw "Inventory receipt is missing fixture fields for $Operation."
    }
    if ([string]$record.fixture.raw -notmatch '^PASS(?:\||$)') {
        throw "Native fixture did not return PASS for $Operation`: $($record.fixture.raw)"
    }
    return $record.fixture.fields
}

function Assert-Operation {
    param(
        [Parameter(Mandatory = $true)]$Fields,
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][Int64]$Epoch,
        [Parameter(Mandatory = $true)][int]$Floor,
        [Parameter(Mandatory = $true)][int]$RecallCount,
        [Parameter(Mandatory = $true)][int]$InventoryChanged,
        [Parameter(Mandatory = $true)][int]$ItemAdded,
        [Parameter(Mandatory = $true)][int]$ItemRemoved,
        [Parameter(Mandatory = $true)][int]$CurrencyChanged
    )
    $actualKind = ([string]$Fields.kind).Replace('_', '')
    if ($actualKind -ine $Kind.Replace('_', '')) { throw "$Kind reported fixture kind '$actualKind'." }
    if ([Int64]$Fields.run_epoch -ne $Epoch -or [int]$Fields.floor -ne $Floor) {
        throw "$Kind reported identity epoch=$($Fields.run_epoch), floor=$($Fields.floor); expected $Epoch/$Floor."
    }
    if ([string]$Fields.hash -cnotmatch '^[0-9A-F]{64}$') { throw "$Kind reported an invalid canonical inventory hash." }
    if ([string]$Fields.weight_bits -notmatch '^(?:0x)?[0-9A-Fa-f]{8}$') { throw "$Kind reported invalid weight bits." }
    if ([int]$Fields.max_slots -le 0 -or [int]$Fields.max_weight -le 0) { throw "$Kind reported invalid inventory capacity." }
    $actual = @(
        [int]$Fields.recall_count,
        [int]$Fields.inventory_changed,
        [int]$Fields.item_added,
        [int]$Fields.item_removed,
        [int]$Fields.currency_changed
    )
    $expected = @($RecallCount, $InventoryChanged, $ItemAdded, $ItemRemoved, $CurrencyChanged)
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        if ($actual[$index] -ne $expected[$index]) {
            throw "$Kind delegate/purge vector was [$($actual -join ',')], expected [$($expected -join ',')]."
        }
    }
}

$oldBaseline = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldOutput = $env:CODEX_CALYSTO_V4_INVENTORY_TRAVEL_OUTPUT
$oldSeed = $env:CODEX_CALYSTO_V4_INVENTORY_TRAVEL_SEED
Assert-DazEditorReceipt -Phase 'pre-run'
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
    $env:CODEX_CALYSTO_V4_INVENTORY_TRAVEL_OUTPUT = $output
    $env:CODEX_CALYSTO_V4_INVENTORY_TRAVEL_SEED = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    $arguments = @('-unattended','-nop4','-nosplash','-NoSound','-NullRHI',"-ABSLOG=`"$log`"") -join ' '
    & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
    if ($LASTEXITCODE -ne 0) { throw "Protected launcher failed with exit code $LASTEXITCODE." }
    Assert-DazEditorReceipt -Phase 'post-run'
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V4_INVENTORY_TRAVEL_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V4_INVENTORY_TRAVEL_SEED = $oldSeed
    Assert-DazEditorReceipt -Phase 'final'
}

foreach ($artifact in @($output, $log)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or (Get-Item -LiteralPath $artifact).Length -le 0) {
        throw "The inventory travel gate did not produce a non-empty artifact: $artifact"
    }
}

$receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
if (-not [bool]$receipt.success -or [string]$receipt.status -ne 'PASS') {
    throw "The inventory travel gate failed in phase $($receipt.phase): $($receipt.error)"
}
if ([int]$receipt.schema_version -ne 4 -or [int]$receipt.generator_version -ne 4 -or [int]$receipt.generation_count -ne 5) {
    throw 'The inventory travel receipt does not identify the exact five-generation V4 gate.'
}
if ([Int64]$receipt.run_seed -ne $RunSeed -or [string]$receipt.scenario -ne 'Zero' -or [int]$receipt.forced_dungeon_edge -ne 26) {
    throw 'The inventory travel receipt used an unexpected seed, scenario or dungeon edge.'
}
if ([string]$receipt.frozen_fixture_hash -cnotmatch '^[0-9A-F]{64}$' -or [string]::IsNullOrEmpty([string]$receipt.frozen_fixture_document)) {
    throw 'The inventory travel receipt is missing the frozen fixture document/hash.'
}
if (@($receipt.asset_saves).Count -ne 0 -or @($receipt.asset_mutations).Count -ne 0 -or @($receipt.protected_assets.mismatches).Count -ne 0) {
    throw 'The inventory travel gate changed Content, dirtied a monitored package or changed a protected file.'
}
if ([string]$receipt.policy_sha256_before -cne [string]$receipt.policy_sha256_after) {
    throw 'The inventory travel gate changed the authored V4 policy bytes.'
}
foreach ($entry in $receipt.protected_assets.before.PSObject.Properties) {
    if (-not [bool]$entry.Value.exists -or [string]$entry.Value.sha256 -cnotmatch '^[0-9A-F]{64}$') {
        throw "Protected baseline is missing or invalid for $($entry.Name)."
    }
}

$arm = Get-FixtureFields -Receipt $receipt -Operation 'initial_arm'
$replay = Get-FixtureFields -Receipt $receipt -Operation 'replay'
$reroll = Get-FixtureFields -Receipt $receipt -Operation 'reroll'
$advance = Get-FixtureFields -Receipt $receipt -Operation 'advance'
$newRun = Get-FixtureFields -Receipt $receipt -Operation 'new_run'
$epoch = [Int64]$arm.run_epoch
if ($epoch -le 0) { throw 'The fixture baseline has an invalid RunEpoch.' }
$documentMatch = [regex]::Match(
    [string]$receipt.frozen_fixture_document,
    '^ProjectV4InventoryFixture\|currency=(?<currency>[0-9A-Fa-f]{8})\|weight=(?<weight>[0-9A-Fa-f]{8})\|maxSlots=(?<slots>\d+)\|maxWeight=(?<maxWeight>\d+)\|items=[\s\S]+$'
)
if (-not $documentMatch.Success) { throw 'The frozen fixture document has an invalid canonical shape.' }
$baselineWeightBits = $documentMatch.Groups['weight'].Value.ToUpperInvariant()
if ($documentMatch.Groups['currency'].Value.ToUpperInvariant() -cne '449A5000') {
    throw 'The frozen fixture does not contain the exact 1234.5f currency bits.'
}

if ([int]$arm.armed -ne 1 -or [int]$arm.floor -ne 1 -or [int]$arm.items -ne 4 -or
    [int]$arm.arrow -ne 7 -or [int]$arm.recall -ne 2 -or
    [string]$arm.block_fragment -notmatch 'Block' -or
    [string]$arm.hash -cne [string]$receipt.frozen_fixture_hash) {
    throw 'The native Arm result does not describe the exact Arrow/Shield/two-Recall fixture.'
}
if ([int]$arm.max_slots -ne 37 -or [int]$arm.max_weight -ne 222 -or
    [int]$arm.max_slots -ne [int]$documentMatch.Groups['slots'].Value -or
    [int]$arm.max_weight -ne [int]$documentMatch.Groups['maxWeight'].Value) {
    throw 'The native Arm result and frozen document disagree on inventory capacity.'
}
Assert-Operation -Fields $replay -Kind 'Replay' -Epoch $epoch -Floor 1 -RecallCount 2 -InventoryChanged 1 -ItemAdded 0 -ItemRemoved 0 -CurrencyChanged 1
Assert-Operation -Fields $reroll -Kind 'Reroll' -Epoch $epoch -Floor 1 -RecallCount 2 -InventoryChanged 1 -ItemAdded 0 -ItemRemoved 0 -CurrencyChanged 1
Assert-Operation -Fields $advance -Kind 'Advance' -Epoch $epoch -Floor 2 -RecallCount 2 -InventoryChanged 1 -ItemAdded 0 -ItemRemoved 0 -CurrencyChanged 1
Assert-Operation -Fields $newRun -Kind 'NewRun' -Epoch ($epoch + 1) -Floor 1 -RecallCount 0 -InventoryChanged 3 -ItemAdded 0 -ItemRemoved 2 -CurrencyChanged 1

foreach ($fields in @($arm, $replay, $reroll, $advance, $newRun)) {
    if ([int]$fields.max_slots -ne [int]$arm.max_slots -or [int]$fields.max_weight -ne [int]$arm.max_weight) {
        throw 'An inventory travel changed MaxSlots or MaxInventoryWeight.'
    }
}
foreach ($fields in @($replay, $reroll, $advance)) {
    if ([string]$fields.hash -cne [string]$receipt.frozen_fixture_hash -or [string]$fields.weight_bits -cne $baselineWeightBits) {
        throw 'Replay, Reroll or Advance did not preserve the exact fixture hash and weight bits.'
    }
}
if ([string]$newRun.hash -ceq [string]$receipt.frozen_fixture_hash -or [string]$newRun.weight_bits -ceq $baselineWeightBits) {
    throw 'New Run did not produce the expected Recall-free inventory state.'
}

$lines = Get-Content -LiteralPath $log
$generate = @($lines | Select-String -SimpleMatch 'Calysto V4 adapter requested GenerateLocal exactly once')
$pcg = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PCGComplete world=')
$nav = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: NavigationPathReady world=')
$levels = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: EnemyLevelsReady world=')
$population = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PopulationRealized world=')
$roster = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: CompanionRosterReady world=')
$door = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: DoorEnabled world=')
foreach ($entry in ([ordered]@{ GenerateLocal=$generate; PCG=$pcg; Navigation=$nav; EnemyLevels=$levels; Population=$population; CompanionRoster=$roster; Door=$door }).GetEnumerator()) {
    if (@($entry.Value).Count -ne 5) { throw "Inventory travel gate emitted $(@($entry.Value).Count) $($entry.Key) records; expected exactly 5." }
}
for ($index = 0; $index -lt 5; ++$index) {
    if (-not ($pcg[$index].LineNumber -le $nav[$index].LineNumber -and
        $nav[$index].LineNumber -le $levels[$index].LineNumber -and
        $levels[$index].LineNumber -le $population[$index].LineNumber -and
        $population[$index].LineNumber -le $roster[$index].LineNumber -and
        $roster[$index].LineNumber -le $door[$index].LineNumber)) {
        throw "Inventory readiness sequence $index violated PCG <= Nav <= EnemyLevels <= Population <= Companion <= Door."
    }
}

$resultLines = @($lines | Select-String -SimpleMatch 'CALYSTO_V4_INVENTORY_TRAVEL_RESULT ')
if ($resultLines.Count -ne 1) { throw "Inventory gate emitted $($resultLines.Count) final result records; expected exactly one." }

$blockedPatterns = @(
    'Blueprint Runtime Error','LogBlueprint: Error','Accessed None','Ensure condition failed',
    'Fatal error:','Assertion failed:','Object Transform','GetAttributeFromPointIndex_0',
    'Calysto controlled generation failed','Calysto generation failed','duplicate generation',
    'duplicated generation','LogEFCalystoDungeon: Error','LogEFCalystoPopulationV4: Error',
    'LogEFProceduralPCGRuntime: Error','LogProjectRunCompanions: Error',
    'Inventory travel fixture failed','CALYSTO_V4_INVENTORY_TRAVEL FAIL'
)
$findings = @(
    foreach ($pattern in $blockedPatterns) {
        $lines | Select-String -SimpleMatch $pattern | ForEach-Object { "[$pattern] $($_.Line.Trim())" }
    }
)
if ($findings.Count -ne 0) { throw "Inventory travel log contains blocked diagnostics:`n$($findings -join [Environment]::NewLine)" }

$summary = [ordered]@{
    schema_version = 4
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'PASS'
    run_seed = $RunSeed
    receipt = $output
    log = $log
    generation_count = 5
    initial_run_epoch = $epoch
    final_run_epoch = $epoch + 1
    frozen_fixture_hash = [string]$receipt.frozen_fixture_hash
    max_slots = [int]$arm.max_slots
    max_weight = [int]$arm.max_weight
    replay_reroll_advance_exact = $true
    new_run_recall_purge = $true
    delegate_contract = [ordered]@{
        replay_reroll_advance = 'inventory_changed=1,item_added=0,item_removed=0,currency_changed=1'
        new_run = 'inventory_changed=3,item_added=0,item_removed=2,currency_changed=1'
    }
    inventory_travel = $receipt.inventory_travel
    protected_assets = $receipt.protected_assets
}
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))
Write-Host 'Dungeon Director V4 inventory travel PIE gate: PASS'
Write-Host "Evidence: $summaryPath"
