[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration,
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [ValidateSet('PreCutover', 'FinalStrict')]
    [string]$ValidationMode = 'FinalStrict',
    [Int64]$RunSeed = 202609040006,
    [ValidateRange(1, 100)]
    [int]$MaxFloor = 10,
    [ValidateRange(60, 1800)]
    [int]$TimeoutSeconds = 600,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$')]
    [string]$PairTag = '',
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter'
)

<#
.SYNOPSIS
Proves same-seed Calysto V6 determinism in two independent packaged processes.

.DESCRIPTION
Runs the definitive V6 packaged smoke twice with Natural configuration and the
same seed, then compares the canonical V6 authority and every deterministic
floor field. Timing, paths, process IDs, telemetry sequence counters, and run
tags are intentionally excluded from the projection.
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-Sha256 {
    param([object]$Value)
    return [string]$Value -cmatch '^[A-Fa-f0-9]{64}$'
}

function Get-Utf8StringSha256 {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '')
    }
    finally { $algorithm.Dispose() }
}

function Write-Utf8CreateNew {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
        try { $writer.Write($Text) }
        finally { $writer.Dispose() }
    }
    finally { $stream.Dispose() }
}

function ConvertTo-FloorProjection {
    param([Parameter(Mandatory = $true)][object]$Floor)
    return [ordered]@{
        schema_version = [int]$Floor.schema_version
        generator_version = [int]$Floor.generator_version
        floor_number = [string]$Floor.floor_number
        generation_serial = [string]$Floor.generation_serial
        pcg_seed = [int]$Floor.pcg_seed
        style_id = [string]$Floor.style_id
        size_x = [int]$Floor.size_x
        size_y = [int]$Floor.size_y
        size_z = [int]$Floor.size_z
        room_count = [int]$Floor.room_count
        eligible_room_count = [int]$Floor.eligible_room_count
        themed_room_count = [int]$Floor.themed_room_count
        room_theme_ids = @($Floor.room_theme_ids | ForEach-Object { [string]$_ })
        candidate_anchor_count = [int]$Floor.candidate_anchor_count
        actor_decision_count = [int]$Floor.actor_decision_count
        chest_content_decision_count = [int]$Floor.chest_content_decision_count
        enemy_count = [int]$Floor.enemy_count
        loose_food_count = [int]$Floor.loose_food_count
        chest_count = [int]$Floor.chest_count
        loot_actor_count = [int]$Floor.loot_actor_count
        special_event_count = [int]$Floor.special_event_count
        spawned_actor_count = [int]$Floor.spawned_actor_count
        realized_threat_cost = [double]$Floor.realized_threat_cost
        realized_resource_cost = [double]$Floor.realized_resource_cost
        policy_hash = [string]$Floor.policy_hash
        ecology_hash = [string]$Floor.ecology_hash
        intent_hash = [string]$Floor.intent_hash
        floor_plan_hash = [string]$Floor.floor_plan_hash
        room_manifest_hash = [string]$Floor.room_manifest_hash
        population_plan_hash = [string]$Floor.population_plan_hash
        anchor_topology_hash = [string]$Floor.anchor_topology_hash
        companion_snapshot_hash = [string]$Floor.companion_snapshot_hash
        realized_manifest_hash = [string]$Floor.realized_manifest_hash
    }
}

function ConvertTo-DeterministicProjection {
    param([Parameter(Mandatory = $true)][object]$Receipt)
    return [ordered]@{
        schema_version = [int]$Receipt.schema_version
        artifact_schema_version = [int]$Receipt.artifact_schema_version
        generator_version = [int]$Receipt.generator_version
        configuration = [string]$Receipt.configuration
        scenario = [string]$Receipt.scenario
        forced_dungeon_edge = [int]$Receipt.forced_dungeon_edge
        run_seed = [string]$Receipt.run_seed
        maximum_floor = [int]$Receipt.maximum_floor
        completed_floor_count = [int]$Receipt.completed_floor_count
        floor_door_interaction_count = [int]$Receipt.floor_door_interaction_count
        policy_path = [string]$Receipt.policy_path
        policy_class = [string]$Receipt.policy_class
        policy_schema_version = [int]$Receipt.policy_schema_version
        policy_generator_version = [int]$Receipt.policy_generator_version
        policy_hash_schema_version = [int]$Receipt.policy_hash_schema_version
        policy_gameplay_hash = [string]$Receipt.policy_gameplay_hash
        policy_authoring_hash = [string]$Receipt.policy_authoring_hash
        policy_material_hash = [string]$Receipt.policy_material_hash
        policy_decal_hash = [string]$Receipt.policy_decal_hash
        door_to_level = [ordered]@{
            actor_class = [string]$Receipt.door_to_level.actor_class
            source_world = [string]$Receipt.door_to_level.source_world
            selected = [bool]$Receipt.door_to_level.selected
            interacted = [bool]$Receipt.door_to_level.interacted
            dungeon_world_observed = [bool]$Receipt.door_to_level.dungeon_world_observed
            entry_probe_ready = [bool]$Receipt.door_to_level.entry_probe_ready
            entry_probe_readiness_trace_count = [int]$Receipt.door_to_level.entry_probe_readiness_trace_count
        }
        floors = @(
            $Receipt.floors |
                Sort-Object { [int64]$_.floor_number } |
                ForEach-Object { ConvertTo-FloorProjection -Floor $_ }
        )
    }
}

if ($RunSeed -le 0) { throw 'RunSeed must be a positive Int64.' }
if ([string]::IsNullOrWhiteSpace($PairTag)) {
    $PairTag = 'pair_{0}_p{1}_{2}' -f `
        [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ', [Globalization.CultureInfo]::InvariantCulture), `
        $PID, ([Guid]::NewGuid().ToString('N').Substring(0, 8))
}

$projectFullPath = [IO.Path]::GetFullPath($ProjectRoot)
$expectedRoot = [IO.Path]::GetFullPath('D:\Projects UE5\NoShellForWinter')
if (-not $projectFullPath.TrimEnd('\').Equals(
        $expectedRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing V6 packaged determinism outside the writable target: $projectFullPath"
}
$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$smokeRunner = Join-Path $projectFullPath 'Tools\Migration\Run-CalystoDungeonDirectorV6PackagedSmoke58.ps1'
if (-not (Test-Path -LiteralPath $smokeRunner -PathType Leaf)) {
    throw "Definitive V6 packaged smoke runner is missing: $smokeRunner"
}
$pairRoot = Join-Path $projectFullPath `
    "Saved\Migration\CalystoDungeonDirectorV6\PackagedDeterminismPairs\$PairTag"
$outputPath = Join-Path $pairRoot 'ComparisonReceipt.json'
if (Test-Path -LiteralPath $pairRoot) {
    throw "PairTag evidence already exists; refusing reuse: $pairRoot"
}
[void][IO.Directory]::CreateDirectory($pairRoot)

$runs = [Collections.Generic.List[object]]::new()
$invocationFailures = [Collections.Generic.List[string]]::new()
foreach ($side in @('A', 'B')) {
    $tag = "${PairTag}_${side}"
    $runRoot = Join-Path $projectFullPath `
        "Saved\Migration\CalystoDungeonDirectorV6\PackagedRuns\$tag"
    $runnerReceiptPath = Join-Path $runRoot 'RunnerReceipt.json'
    $invocationLogPath = Join-Path $pairRoot "Invocation_${side}.log"
    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $smokeRunner,
        '-Configuration', $Configuration,
        '-ArchiveRoot', $archivePath,
        '-ValidationMode', $ValidationMode,
        '-Scenario', 'Natural',
        '-RunSeed', $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-MaxFloor', $MaxFloor.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-TimeoutSeconds', $TimeoutSeconds.ToString([Globalization.CultureInfo]::InvariantCulture),
        '-RunTag', $tag,
        '-ProjectRoot', $projectFullPath
    )
    $transcript = & powershell.exe @arguments 2>&1 | Out-String
    $exitCode = $LASTEXITCODE
    [IO.File]::WriteAllText(
        $invocationLogPath,
        $transcript,
        [Text.UTF8Encoding]::new($false))
    $runnerReceipt = $null
    $runtimeReceipt = $null
    $parseError = ''
    try {
        if (-not (Test-Path -LiteralPath $runnerReceiptPath -PathType Leaf)) {
            throw "Runner receipt missing: $runnerReceiptPath"
        }
        $runnerReceipt = Get-Content -Raw -LiteralPath $runnerReceiptPath | ConvertFrom-Json
        $runtimePath = [string]$runnerReceipt.runtime_receipt
        if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
            throw "Runtime receipt missing: $runtimePath"
        }
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $runtimePath).Hash -cne
            [string]$runnerReceipt.runtime_receipt_sha256) {
            throw "Runtime receipt hash mismatch for side $side"
        }
        $runtimeReceipt = Get-Content -Raw -LiteralPath $runtimePath | ConvertFrom-Json
    }
    catch { $parseError = $_.Exception.Message }
    if ($exitCode -ne 0 -or $null -eq $runnerReceipt -or
        [string]$runnerReceipt.status -cne 'PASS' -or
        $null -eq $runtimeReceipt -or [string]$runtimeReceipt.status -cne 'PASS') {
        $invocationFailures.Add(
            "side=$side exit=$exitCode parse=$parseError runner=$([string]$runnerReceipt.status) runtime=$([string]$runtimeReceipt.status)")
    }
    $runs.Add([pscustomobject]@{
        side = $side
        tag = $tag
        exit_code = $exitCode
        invocation_log = $invocationLogPath
        invocation_log_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $invocationLogPath).Hash
        runner_receipt_path = $runnerReceiptPath
        runner_receipt = $runnerReceipt
        runtime_receipt = $runtimeReceipt
        parse_error = $parseError
    })
}

$checks = [ordered]@{}
$mismatches = [Collections.Generic.List[object]]::new()
$left = $runs[0]
$right = $runs[1]
$checks['two_independent_v6_smokes_pass'] = $invocationFailures.Count -eq 0
$checks['run_tags_are_distinct'] = $left.tag -cne $right.tag
$checks['runner_receipt_schemas_exact'] =
    [int]$left.runner_receipt.receipt_schema_version -eq 1 -and
    [int]$right.runner_receipt.receipt_schema_version -eq 1
$checks['same_packaged_executable'] =
    (Test-Sha256 $left.runner_receipt.executable_sha256) -and
    [string]$left.runner_receipt.executable_sha256 -ceq
        [string]$right.runner_receipt.executable_sha256
$checks['same_authority_receipt'] =
    (ConvertTo-Json $left.runner_receipt.authority -Compress -Depth 8) -ceq
        (ConvertTo-Json $right.runner_receipt.authority -Compress -Depth 8)

$leftProjection = if ($null -ne $left.runtime_receipt) {
    ConvertTo-DeterministicProjection -Receipt $left.runtime_receipt
}
else { $null }
$rightProjection = if ($null -ne $right.runtime_receipt) {
    ConvertTo-DeterministicProjection -Receipt $right.runtime_receipt
}
else { $null }
$leftJson = if ($null -ne $leftProjection) {
    ConvertTo-Json $leftProjection -Compress -Depth 16
}
else { '' }
$rightJson = if ($null -ne $rightProjection) {
    ConvertTo-Json $rightProjection -Compress -Depth 16
}
else { '' }
$leftHash = Get-Utf8StringSha256 $leftJson
$rightHash = Get-Utf8StringSha256 $rightJson
$checks['canonical_v6_projection_byte_identical'] =
    -not [string]::IsNullOrWhiteSpace($leftJson) -and
    $leftJson -ceq $rightJson -and $leftHash -ceq $rightHash

if ($null -ne $leftProjection -and $null -ne $rightProjection) {
    $leftFloors = @($leftProjection.floors)
    $rightFloors = @($rightProjection.floors)
    if ($leftFloors.Count -ne $rightFloors.Count) {
        $mismatches.Add([pscustomobject]@{
            scope = 'floors'; field = 'count'; left = $leftFloors.Count; right = $rightFloors.Count
        })
    }
    $fieldNames = @((ConvertTo-FloorProjection -Floor $left.runtime_receipt.floors[0]).Keys)
    for ($index = 0; $index -lt [Math]::Min($leftFloors.Count, $rightFloors.Count); ++$index) {
        foreach ($field in $fieldNames) {
            $leftValue = ConvertTo-Json $leftFloors[$index][$field] -Compress -Depth 8
            $rightValue = ConvertTo-Json $rightFloors[$index][$field] -Compress -Depth 8
            if ($leftValue -cne $rightValue) {
                $mismatches.Add([pscustomobject]@{
                    scope = 'floor'
                    floor = $index + 1
                    field = $field
                    left = $leftValue
                    right = $rightValue
                })
            }
        }
    }
}
$checks['field_level_mismatch_set_empty'] = $mismatches.Count -eq 0

$failedChecks = @($checks.GetEnumerator() | Where-Object { -not [bool]$_.Value } | ForEach-Object { $_.Key })
$status = if ($failedChecks.Count -eq 0) { 'PASS' } else { 'FAIL' }
function ConvertTo-RunEvidence {
    param([Parameter(Mandatory = $true)][object]$Run)
    return [ordered]@{
        side = $Run.side
        tag = $Run.tag
        exit_code = $Run.exit_code
        invocation_log = $Run.invocation_log
        invocation_log_sha256 = $Run.invocation_log_sha256
        runner_receipt = $Run.runner_receipt_path
        runner_receipt_sha256 = if (Test-Path -LiteralPath $Run.runner_receipt_path) {
            (Get-FileHash -Algorithm SHA256 -LiteralPath $Run.runner_receipt_path).Hash
        }
        else { '' }
        runtime_receipt = if ($null -ne $Run.runner_receipt) {
            [string]$Run.runner_receipt.runtime_receipt
        }
        else { '' }
        runtime_receipt_sha256 = if ($null -ne $Run.runner_receipt) {
            [string]$Run.runner_receipt.runtime_receipt_sha256
        }
        else { '' }
        parse_error = $Run.parse_error
    }
}

$result = [ordered]@{
    receipt_schema_version = 1
    calysto_schema_version = 6
    generator_version = 6
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = $status
    semantic = 'v6_packaged_same_seed_ab_determinism'
    process_model = 'two_independent_sequential_packaged_processes'
    pair_tag = $PairTag
    configuration = $Configuration
    validation_mode = $ValidationMode
    scenario = 'Natural'
    run_seed = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    maximum_floor = $MaxFloor
    archive_root = $archivePath
    left = ConvertTo-RunEvidence -Run $left
    right = ConvertTo-RunEvidence -Run $right
    deterministic_projection = [ordered]@{
        left_sha256 = $leftHash
        right_sha256 = $rightHash
        left = $leftProjection
        right = $rightProjection
    }
    mismatches = @($mismatches)
    invocation_failures = @($invocationFailures)
    checks = $checks
    failed_checks = $failedChecks
}
Write-Utf8CreateNew -Path $outputPath `
    -Text (($result | ConvertTo-Json -Depth 24) + [Environment]::NewLine)
Write-Output ($result | ConvertTo-Json -Depth 24)
if ($status -ne 'PASS') {
    throw "Packaged Calysto V6 same-seed determinism failed: $($failedChecks -join ', ')"
}
