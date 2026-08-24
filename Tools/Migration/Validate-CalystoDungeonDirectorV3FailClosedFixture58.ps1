[CmdletBinding()]
param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$FixturePath = "Saved\Migration\CalystoDungeonDirectorV3\EmptyTopologyFailClosed_Size18_Seed001_20260814.json",
    [string]$LogPath = "Saved\Migration\Logs\CalystoDungeonDirectorV3_EmptyTopologyFailClosed_Size18_Seed001_20260814.log",
    [string]$OutputPath = "Saved\Migration\CalystoDungeonDirectorV3\EmptyTopologyFailClosed_Acceptance_20260814.json"
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectPath {
    param([string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $ProjectRoot $Path
}

$fixtureFullPath = Resolve-ProjectPath $FixturePath
$logFullPath = Resolve-ProjectPath $LogPath
$outputFullPath = Resolve-ProjectPath $OutputPath
foreach ($requiredPath in @($fixtureFullPath, $logFullPath)) {
    if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required fail-closed evidence is missing: $requiredPath"
    }
}

$fixture = Get-Content -LiteralPath $fixtureFullPath -Raw | ConvertFrom-Json
$logText = [IO.File]::ReadAllText($logFullPath)
$case = @($fixture.cases)[0]
$attempts = @($case.attempts)

$expectedFailureCode = "START_POINT_REPAIR_NO_STRUCTURAL_SURFACE"
$repairAppliedPattern = "TopologyRepair applied|START_POINT_REPAIR applied|startPointRepair=applied"
$forbiddenProgressPattern = "NavigationPathReady|PopulationRealized|DoorEnabled"
$hubLoadPattern = "LogLoad: LoadMap: /Game/_Game/Hub/HUB"
$exhaustedPattern = "Calysto V3 exhausted attempts\. Returning to /Game/_Game/Hub/HUB; code=$expectedFailureCode"

$openingIdentities = @($attempts | ForEach-Object {
    if ($_.opening_log -match 'run=(?<Run>\d+) floor=(?<Floor>\d+) generation=(?<Generation>\d+) pcg_seed=(?<Seed>\d+) intent=(?<Intent>[A-F0-9]{64})') {
        "$($Matches.Run)|$($Matches.Floor)|$($Matches.Generation)|$($Matches.Seed)|$($Matches.Intent)"
    }
})

$checks = [ordered]@{
    source_fixture_is_expected_negative = (
        [string]$fixture.status -eq "FAIL" -and -not [bool]$fixture.success
    )
    exactly_two_bounded_attempts = ($attempts.Count -eq 2)
    one_generate_local_per_attempt = (
        @($attempts | Where-Object { [int]$_.generate_local_count -ne 1 }).Count -eq 0
    )
    retry_identity_is_exact = (
        $openingIdentities.Count -eq 2 -and @($openingIdentities | Sort-Object -Unique).Count -eq 1
    )
    pcg_completed_before_each_rejection = (
        @($attempts | Where-Object { $null -eq $_.event_ordinals.pcg_complete }).Count -eq 0
    )
    empty_structural_surface_observed = (
        [int]$case.physical_bounds.anchors.peak_actor_count -eq 0 -and
        [int]$case.physical_bounds.floor_doors.peak_actor_count -eq 0 -and
        [double]$case.physical_bounds.dungeon_actor.last_bounds.size.x -eq 1024.0 -and
        [double]$case.physical_bounds.dungeon_actor.last_bounds.size.y -eq 1024.0 -and
        [double]$case.physical_bounds.dungeon_actor.last_bounds.size.z -eq 1024.0
    )
    exact_failure_code_twice = (
        ([regex]::Matches(
            $logText,
            "(?m)^\[[0-9.:-]+\]\[[ 0-9]+\]LogEFCalystoDungeon: Error: Calysto generation failed code=$expectedFailureCode attempt=[12]/2"
        )).Count -eq 2
    )
    no_start_point_was_fabricated = (-not [regex]::IsMatch($logText, $repairAppliedPattern))
    readiness_pipeline_never_advanced = (-not [regex]::IsMatch($logText, $forbiddenProgressPattern))
    exhausted_attempts_returned_to_hub = (
        [regex]::IsMatch($logText, $exhaustedPattern) -and
        [regex]::IsMatch($logText, $hubLoadPattern) -and
        [string]$case.reason -eq "safe_return_to_hub_before_door_ready"
    )
    no_asset_mutations = (@($fixture.asset_mutations).Count -eq 0)
    no_asset_saves = (@($fixture.asset_saves).Count -eq 0)
    no_protected_asset_delta = (@($fixture.protected_assets.mismatches).Count -eq 0)
    dirty_state_gate_passed = ([string]$fixture.asset_monitor.dirty_state_gate -eq "PASS")
}

$failedChecks = @($checks.GetEnumerator() | Where-Object { -not [bool]$_.Value } | ForEach-Object { $_.Key })
$result = [ordered]@{
    schema_version = 3
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = if ($failedChecks.Count -eq 0) { "PASS" } else { "FAIL" }
    semantic = "expected_fail_closed_on_empty_pcg_topology"
    source_fixture_status = [string]$fixture.status
    source_fixture = $fixtureFullPath
    source_fixture_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $fixtureFullPath).Hash
    source_log = $logFullPath
    source_log_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $logFullPath).Hash
    expected_failure_code = $expectedFailureCode
    run_seed = [Int64]$case.run_seed
    dungeon_edge = [int]$case.edge
    attempt_count = $attempts.Count
    retry_identity = @($openingIdentities | Select-Object -First 1)[0]
    checks = $checks
    failed_checks = $failedChecks
    interpretation = "The source matrix correctly reports this size/seed as a generation FAIL. This acceptance gate passes only when V3 refuses to invent topology, retries the frozen identity exactly once, and returns safely to HUB without population or door readiness."
}

$outputDirectory = Split-Path -Parent $outputFullPath
if (!(Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputFullPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8

if ($failedChecks.Count -ne 0) {
    throw "Empty-topology fail-closed acceptance failed: $($failedChecks -join ', ')"
}
