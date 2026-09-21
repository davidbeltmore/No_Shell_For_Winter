[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration,
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [ValidateSet('PreCutover', 'FinalStrict')]
    [string]$ValidationMode = 'FinalStrict',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$')]
    [string]$Scenario = 'Natural',
    [Int64]$RunSeed = 202609040006,
    [ValidateRange(1, 100)]
    [int]$MaxFloor = 10,
    [ValidateRange(60, 1800)]
    [int]$TimeoutSeconds = 600,
    [ValidateRange(0, 30)]
    [int]$ForcedDungeonEdge = 0,
    [switch]$CaptureVisual,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$')]
    [string]$RunTag = '',
    [switch]$ExpectShippingRejection,
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$OutputPath = '',
    [string]$LogPath = ''
)

<#
.SYNOPSIS
Runs the definitive Calysto V6 packaged traversal contract.

.DESCRIPTION
Starts a packaged build in the HUB and exercises the real DoorToLevel entrance,
room-local V6 generation, and floor doors. Every runtime receipt, telemetry
sequence, V6 authority hash, room Theme field, and package identity is checked.
Shipping development overrides are accepted only as an explicit fail-closed
test through -ExpectShippingRejection.
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-Sha256 {
    param([object]$Value)
    return [string]$Value -cmatch '^[A-Fa-f0-9]{64}$'
}

function Test-DescendantPath {
    param(
        [Parameter(Mandatory = $true)][string]$Child,
        [Parameter(Mandatory = $true)][string]$Parent
    )
    $childFull = [IO.Path]::GetFullPath($Child)
    $parentFull = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    return $childFull.StartsWith($parentFull, [StringComparison]::OrdinalIgnoreCase)
}

function Get-CandidateArtifact {
    param(
        [Parameter(Mandatory = $true)][string[]]$Roots,
        [Parameter(Mandatory = $true)][string]$FileName,
        [switch]$AllowMissing
    )
    $matches = @(
        foreach ($root in $Roots) {
            $candidate = Join-Path $root "CalystoDungeonDirectorV6\$FileName"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                Get-Item -LiteralPath $candidate
            }
        }
    )
    if ($matches.Count -gt 1) {
        throw "Ambiguous V6 packaged artifact '$FileName': $($matches.FullName -join ', ')"
    }
    if ($matches.Count -eq 0) {
        if ($AllowMissing) { return $null }
        throw "V6 packaged artifact was not produced: $FileName"
    }
    return $matches[0]
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

if ($RunSeed -le 0) { throw 'RunSeed must be a positive Int64.' }
if ($ForcedDungeonEdge -gt 0 -and $ForcedDungeonEdge -lt 18) {
    throw 'ForcedDungeonEdge must be 0 or an integer from 18 through 30.'
}
$hasDevelopmentOverride = $Scenario -cne 'Natural' -or $ForcedDungeonEdge -ne 0
if ($Configuration -ceq 'Shipping' -and $hasDevelopmentOverride -and -not $ExpectShippingRejection) {
    throw 'Shipping accepts only Natural with ForcedDungeonEdge 0. Use -ExpectShippingRejection to prove rejection.'
}
if ($ExpectShippingRejection -and ($Configuration -cne 'Shipping' -or -not $hasDevelopmentOverride)) {
    throw '-ExpectShippingRejection requires Shipping plus a development-only scenario or forced edge.'
}
if ($Scenario -cne 'Natural' -and $MaxFloor -ne 1 -and -not $ExpectShippingRejection) {
    throw 'Development scenario hooks must run as isolated one-floor processes.'
}
if ([string]::IsNullOrWhiteSpace($RunTag)) {
    $RunTag = '{0}_p{1}_{2}' -f `
        [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ', [Globalization.CultureInfo]::InvariantCulture), `
        $PID, ([Guid]::NewGuid().ToString('N').Substring(0, 8))
}

$projectFullPath = [IO.Path]::GetFullPath($ProjectRoot)
$expectedRoot = [IO.Path]::GetFullPath('D:\Projects UE5\NoShellForWinter')
if (-not $projectFullPath.TrimEnd('\').Equals(
        $expectedRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing V6 packaged smoke outside the writable target: $projectFullPath"
}
$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$windowsRoot = Join-Path $archivePath 'Windows'
$manifestPath = Join-Path $windowsRoot 'Manifest_UFSFiles_Win64.txt'
$projectDescriptorPath = Join-Path $projectFullPath 'NoShellForWinter.uproject'
$receiptGuardPath = Join-Path $projectFullPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$cookClosurePath = Join-Path $projectFullPath 'Saved\Migration\CalystoDungeonDirectorV6\ValidateCookClosureV6.json'
$exePath = if ($Configuration -ceq 'Shipping') {
    Join-Path $windowsRoot 'NoShellForWinter\Binaries\Win64\NoShellForWinter-Win64-Shipping.exe'
}
else {
    Join-Path $windowsRoot 'NoShellForWinter\Binaries\Win64\NoShellForWinter.exe'
}
foreach ($requiredPath in @(
        $windowsRoot, $manifestPath, $projectDescriptorPath, $receiptGuardPath,
        $cookClosurePath, $exePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required V6 packaged smoke path is missing: $requiredPath"
    }
}

$cookClosure = Get-Content -Raw -LiteralPath $cookClosurePath | ConvertFrom-Json
$expectedPolicyObject = '/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy'
$expectedPolicyClass = '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset'
if ([string]$cookClosure.status -cne 'PASS' -or
    [string]$cookClosure.policy.object -cne $expectedPolicyObject -or
    [string]$cookClosure.policy.class -cne $expectedPolicyClass -or
    -not (Test-Sha256 $cookClosure.policy.hashes.gameplay) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.authoring) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.materials) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.decals)) {
    throw 'Current V6 cook-closure receipt is not package-smoke ready.'
}

$bundleFullPath = Join-Path $projectFullPath "Saved\Migration\CalystoDungeonDirectorV6\PackagedRuns\$RunTag"
if (Test-Path -LiteralPath $bundleFullPath) {
    throw "RunTag evidence already exists; refusing to overwrite: $bundleFullPath"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $bundleFullPath 'RunnerReceipt.json'
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $bundleFullPath 'EngineRuntime.log'
}
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$engineLogFullPath = [IO.Path]::GetFullPath($LogPath)
if (-not (Test-DescendantPath -Child $outputFullPath -Parent $bundleFullPath) -or
    -not (Test-DescendantPath -Child $engineLogFullPath -Parent $bundleFullPath)) {
    throw 'OutputPath and LogPath must remain inside the unique V6 packaged-run evidence directory.'
}
$runtimeCopyPath = Join-Path $bundleFullPath 'RuntimeReceipt.json'
$telemetryCopyPath = Join-Path $bundleFullPath 'ProjectTelemetry.log'
$screenshotCopyPath = Join-Path $bundleFullPath 'Screenshot.png'
foreach ($artifact in @(
        $outputFullPath, $engineLogFullPath, $runtimeCopyPath,
        $telemetryCopyPath, $screenshotCopyPath)) {
    if (Test-Path -LiteralPath $artifact) {
        throw "Refusing to overwrite V6 packaged evidence: $artifact"
    }
}

$receiptFileName = if ($ExpectShippingRejection) {
    "PackagedSmokeReceipt_Invalid_${RunTag}.json"
}
else {
    "PackagedSmokeReceipt_${Configuration}_${Scenario}_${RunTag}.json"
}
$telemetryFileName = "PackagedSmokeTelemetry_${Configuration}_${Scenario}_${RunTag}.log"
$screenshotFileName = "PackagedSmokeVisual_${Configuration}_${Scenario}_${RunTag}.png"
$candidateSavedRoots = [Collections.Generic.List[string]]::new()
$candidateSavedRoots.Add((Join-Path $windowsRoot 'NoShellForWinter\Saved'))
if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    $candidateSavedRoots.Add((Join-Path $env:LOCALAPPDATA 'NoShellForWinter\Saved'))
}
$candidateRoots = @($candidateSavedRoots | Sort-Object -Unique)
foreach ($name in @($receiptFileName, $telemetryFileName, $screenshotFileName)) {
    if ($null -ne (Get-CandidateArtifact -Roots $candidateRoots -FileName $name -AllowMissing)) {
        throw "RunTag collision in packaged Saved roots for '$name'."
    }
}

[void][IO.Directory]::CreateDirectory($bundleFullPath)
$arguments = @(
    '/Game/_Game/Hub/HUB',
    '-unattended', '-nop4', '-nosplash', '-NoSound', '-RenderOffscreen',
    '-ResX=1280', '-ResY=720', '-CalystoV6PackagedSmoke',
    "-CalystoV6SmokeRunTag=$RunTag", "-CalystoV6SmokeSeed=$RunSeed",
    "-CalystoV6SmokeMaxFloor=$MaxFloor", "-CalystoV6SmokeTimeout=$TimeoutSeconds",
    "-CalystoV6SmokeScenario=$Scenario", "-CalystoV6SmokeForcedEdge=$ForcedDungeonEdge",
    "-ABSLOG=`"$engineLogFullPath`""
)
if ($CaptureVisual) { $arguments += '-CalystoV6SmokeCapture' }

$checks = [ordered]@{}
$failureReasons = [Collections.Generic.List[string]]::new()
$runtimeReceipt = $null
$runtimeSource = $null
$telemetrySource = $null
$screenshotSource = $null
$processExitCode = $null
$startedUtc = [DateTime]::UtcNow

try {
    $manifestPaths = @(
        foreach ($line in [IO.File]::ReadLines($manifestPath)) {
            $path = ($line -split "`t", 2)[0].Trim().Replace('\', '/')
            if (-not [string]::IsNullOrWhiteSpace($path)) { $path }
        }
    )
    $manifestText = $manifestPaths -join "`n"
    $expectedPolicyManifest = 'NoShellForWinter/Content/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.uasset'
    $checks['exact_v6_policy_in_manifest'] = @(
        $manifestPaths | Where-Object { [string]$_ -ceq $expectedPolicyManifest }
    ).Count -eq 1
    $retiredVersions = @(3..5)
    $retiredBlueprintSuffix = 'V' + [string](2 + 2)
    $legacyManifestTokens = @($retiredVersions | ForEach-Object {
        "/Content/_Game/Data/CalystoDungeon/V$($_)/"
    }) + @(
        "/BP_CalystoLockedChest$retiredBlueprintSuffix.uasset",
        "/BP_CalystoLockPickChest$retiredBlueprintSuffix.uasset",
        "/BP_CalystoArmorPickup$retiredBlueprintSuffix.uasset"
    )
    $legacyManifestFindings = @($legacyManifestTokens | Where-Object {
        $manifestText.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -ge 0
    })
    $checks['legacy_calysto_packages_absent_in_final_manifest'] =
        $ValidationMode -ceq 'PreCutover' -or $legacyManifestFindings.Count -eq 0

    $projectDescriptor = Get-Content -Raw -LiteralPath $projectDescriptorPath | ConvertFrom-Json
    foreach ($pluginName in @('DazToUnreal', 'EFCharacterCreationDazBridge')) {
        $entry = @($projectDescriptor.Plugins | Where-Object { [string]$_.Name -ceq $pluginName })
        $checks["${pluginName}_enabled_in_uproject"] = $entry.Count -eq 1 -and [bool]$entry[0].Enabled
    }

    $dazOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $receiptGuardPath -ProjectRoot $projectFullPath `
        -TargetName NoShellForWinter -Configuration $Configuration -VerifyOnly 2>&1 | Out-String
    $checks['daz_receipt_preflight_pass'] = $LASTEXITCODE -eq 0

    $process = Start-Process -FilePath $exePath -ArgumentList $arguments `
        -WorkingDirectory $windowsRoot -WindowStyle Hidden -PassThru
    $waitMilliseconds = [Math]::Min(
        [int64]::MaxValue, ([int64]$TimeoutSeconds + 120L) * 1000L)
    if (-not $process.WaitForExit([int]$waitMilliseconds)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Packaged V6 smoke exceeded the external timeout ($TimeoutSeconds + 120 seconds)."
    }
    $processExitCode = $process.ExitCode
    $checks['packaged_process_exit_zero'] = $processExitCode -eq 0

    $dazOutput += & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $receiptGuardPath -ProjectRoot $projectFullPath `
        -TargetName NoShellForWinter -Configuration $Configuration -VerifyOnly 2>&1 | Out-String
    $checks['daz_receipt_postflight_pass'] = $LASTEXITCODE -eq 0

    $runtimeSource = Get-CandidateArtifact -Roots $candidateRoots -FileName $receiptFileName
    Copy-Item -LiteralPath $runtimeSource.FullName -Destination $runtimeCopyPath
    $runtimeReceipt = Get-Content -Raw -LiteralPath $runtimeCopyPath | ConvertFrom-Json
    $engineLogText = if (Test-Path -LiteralPath $engineLogFullPath -PathType Leaf) {
        Get-Content -Raw -LiteralPath $engineLogFullPath
    }
    else { '' }

    $checks['runtime_receipt_v6_schema_exact'] =
        [int]$runtimeReceipt.schema_version -eq 6 -and
        [int]$runtimeReceipt.artifact_schema_version -eq 1 -and
        [int]$runtimeReceipt.generator_version -eq 6
    $checks['runtime_request_identity_exact'] =
        [string]$runtimeReceipt.configuration -ceq $Configuration -and
        [string]$runtimeReceipt.scenario -ceq $Scenario -and
        [int]$runtimeReceipt.forced_dungeon_edge -eq $ForcedDungeonEdge -and
        [string]$runtimeReceipt.run_tag -ceq $RunTag -and
        [string]$runtimeReceipt.run_seed -ceq
            $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture) -and
        [int]$runtimeReceipt.maximum_floor -eq $MaxFloor

    if ($ExpectShippingRejection) {
        $checks['shipping_override_rejected_exactly'] =
            [string]$runtimeReceipt.status -ceq 'FAIL' -and
            [string]$runtimeReceipt.reason -ceq 'SHIPPING_DEVELOPMENT_OVERRIDE_REJECTED' -and
            $engineLogText -match 'CALYSTO_V6_PACKAGED_SMOKE_COMPLETE status=FAIL' -and
            $engineLogText -match 'reason=SHIPPING_DEVELOPMENT_OVERRIDE_REJECTED'
        $checks['rejected_override_produced_no_telemetry'] =
            $null -eq (Get-CandidateArtifact -Roots $candidateRoots `
                -FileName $telemetryFileName -AllowMissing)
    }
    else {
        $checks['runtime_status_pass'] =
            [string]$runtimeReceipt.status -ceq 'PASS' -and
            [string]$runtimeReceipt.reason -ceq 'PASS'
        $checks['runtime_policy_identity_exact'] =
            [string]$runtimeReceipt.policy_path -ceq $expectedPolicyObject -and
            [string]$runtimeReceipt.policy_class -ceq $expectedPolicyClass -and
            [int]$runtimeReceipt.policy_schema_version -eq 6 -and
            [int]$runtimeReceipt.policy_generator_version -eq 6 -and
            [int]$runtimeReceipt.policy_hash_schema_version -eq 3 -and
            [string]$runtimeReceipt.policy_gameplay_hash -ceq
                ([string]$cookClosure.policy.hashes.gameplay).ToUpperInvariant() -and
            [string]$runtimeReceipt.policy_authoring_hash -ceq
                ([string]$cookClosure.policy.hashes.authoring).ToUpperInvariant() -and
            [string]$runtimeReceipt.policy_material_hash -ceq
                ([string]$cookClosure.policy.hashes.materials).ToUpperInvariant() -and
            [string]$runtimeReceipt.policy_decal_hash -ceq
                ([string]$cookClosure.policy.hashes.decals).ToUpperInvariant()
        $door = $runtimeReceipt.door_to_level
        $checks['door_to_level_hub_entry_complete'] =
            [string]$door.actor_class -ceq '/Game/Procedural/DoorToLevel.DoorToLevel_C' -and
            [bool]$door.selected -and [bool]$door.interacted -and
            [bool]$door.dungeon_world_observed -and [bool]$door.entry_probe_ready -and
            [int]$door.entry_probe_readiness_trace_count -eq 1

        $floors = @($runtimeReceipt.floors)
        $floorFailures = [Collections.Generic.List[string]]::new()
        for ($index = 0; $index -lt $floors.Count; ++$index) {
            $floor = $floors[$index]
            $expectedFloor = $index + 1
            if ([int]$floor.schema_version -ne 6 -or
                [int]$floor.generator_version -ne 6 -or
                [int64]$floor.floor_number -ne $expectedFloor -or
                [string]::IsNullOrWhiteSpace([string]$floor.style_id) -or
                [int]$floor.room_count -lt 1 -or
                [int]$floor.eligible_room_count -lt [int]$floor.themed_room_count -or
                @($floor.room_theme_ids).Count -lt 1) {
                $floorFailures.Add("floor=$expectedFloor identity/count/theme contract")
            }
            foreach ($field in @(
                    'policy_hash', 'ecology_hash', 'intent_hash', 'floor_plan_hash',
                    'room_manifest_hash', 'population_plan_hash', 'anchor_topology_hash',
                    'companion_snapshot_hash', 'realized_manifest_hash')) {
                if (-not (Test-Sha256 $floor.$field)) {
                    $floorFailures.Add("floor=$expectedFloor invalid $field")
                }
            }
            if ([string]$floor.policy_hash -cne [string]$runtimeReceipt.policy_gameplay_hash) {
                $floorFailures.Add("floor=$expectedFloor policy hash mismatch")
            }
        }
        $checks['all_floor_records_are_definitive_v6'] =
            $floors.Count -eq $MaxFloor -and $floorFailures.Count -eq 0
        $checks['floor_and_door_counts_exact'] =
            [int]$runtimeReceipt.completed_floor_count -eq $MaxFloor -and
            [int]$runtimeReceipt.floor_door_interaction_count -eq [Math]::Max(0, $MaxFloor - 1)
        $failedEmbeddedChecks = @($runtimeReceipt.checks.PSObject.Properties | Where-Object {
            -not [bool]$_.Value
        })
        $checks['embedded_runtime_checks_all_pass'] = $failedEmbeddedChecks.Count -eq 0
        $checks['runtime_completion_marker_exact'] =
            $engineLogText -match 'CALYSTO_V6_PACKAGED_SMOKE_COMPLETE status=PASS'

        $telemetrySource = Get-CandidateArtifact -Roots $candidateRoots -FileName $telemetryFileName
        Copy-Item -LiteralPath $telemetrySource.FullName -Destination $telemetryCopyPath
        $telemetryText = Get-Content -Raw -LiteralPath $telemetryCopyPath
        $checks['project_telemetry_v6_header_and_completion'] =
            $telemetryText -match 'CALYSTO_V6_PROJECT_TELEMETRY schema=2' -and
            ([regex]::Matches($telemetryText, 'event=Complete status=PASS')).Count -eq 1
        $checks['one_generate_local_per_generation'] =
            ([regex]::Matches(
                $telemetryText,
                'event=GenerateLocal source=PCGRuntimeTrace phase=EntryProbe')).Count -eq 1 -and
            ([regex]::Matches(
                $telemetryText,
                'event=GenerateLocal source=PCGRuntimeTrace phase=SeededRun')).Count -eq $MaxFloor
        $checks['telemetry_floor_sequences_exact'] =
            ([regex]::Matches($telemetryText, 'event=FloorReady status=PASS')).Count -eq $MaxFloor -and
            ([regex]::Matches($telemetryText, 'event=DoorToLevelSelected')).Count -eq 1 -and
            ([regex]::Matches($telemetryText, 'event=DoorToLevelInteracted')).Count -eq 1 -and
            ([regex]::Matches($telemetryText, 'event=FloorDoorSelected')).Count -eq $MaxFloor -and
            ([regex]::Matches($telemetryText, 'event=FloorDoorInteracted')).Count -eq [Math]::Max(0, $MaxFloor - 1)

        if ($CaptureVisual) {
            $screenshotSource = Get-CandidateArtifact -Roots $candidateRoots -FileName $screenshotFileName
            Copy-Item -LiteralPath $screenshotSource.FullName -Destination $screenshotCopyPath
            $checks['visual_capture_present'] = (Get-Item -LiteralPath $screenshotCopyPath).Length -gt 1024
        }
        else {
            $checks['visual_capture_not_requested'] =
                -not [bool]$runtimeReceipt.screenshot_requested -and
                [string]::IsNullOrWhiteSpace([string]$runtimeReceipt.screenshot_path)
        }

        $runtimeAndLogs = (Get-Content -Raw -LiteralPath $runtimeCopyPath) + "`n" +
            $engineLogText + "`n" + $telemetryText
        $legacyTokens = @($retiredVersions | ForEach-Object {
            "/CalystoDungeon/V$($_)/"
            "CalystoV$($_)"
        }) + @(
            "EFCalystoDungeonDirectorPolicyV$($retiredVersions[0])",
            "EFCalystoDungeonDirectorPolicyV$($retiredVersions[1])",
            "EFCalystoDungeonDirectorPolicyV$($retiredVersions[2])Asset",
            "BP_CalystoLockedChest$retiredBlueprintSuffix",
            "BP_CalystoLockPickChest$retiredBlueprintSuffix",
            "BP_CalystoArmorPickup$retiredBlueprintSuffix",
            'LoadSynchronous'
        )
        $legacyRuntimeFindings = @($legacyTokens | Where-Object {
            $runtimeAndLogs.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -ge 0
        })
        $checks['runtime_logs_and_receipts_have_no_legacy_or_sync_load_tokens'] =
            $legacyRuntimeFindings.Count -eq 0
    }
}
catch {
    $failureReasons.Add($_.Exception.Message)
}

foreach ($entry in $checks.GetEnumerator()) {
    if (-not [bool]$entry.Value) { $failureReasons.Add("Failed check: $($entry.Key)") }
}
$status = if ($failureReasons.Count -eq 0) { 'PASS' } else { 'FAIL' }
$result = [ordered]@{
    receipt_schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = $status
    semantic = if ($ExpectShippingRejection) {
        'v6_shipping_development_override_rejection'
    }
    else { 'v6_packaged_hub_dungeon_floor_traversal' }
    configuration = $Configuration
    validation_mode = $ValidationMode
    scenario = $Scenario
    forced_dungeon_edge = $ForcedDungeonEdge
    run_tag = $RunTag
    run_seed = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    maximum_floor = $MaxFloor
    archive_root = $archivePath
    executable = $exePath
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $exePath).Hash
    process_exit_code = $processExitCode
    started_utc = $startedUtc.ToString('o')
    elapsed_seconds = [Math]::Round(([DateTime]::UtcNow - $startedUtc).TotalSeconds, 3)
    cook_closure_receipt = $cookClosurePath
    authority = [ordered]@{
        policy_path = $expectedPolicyObject
        policy_class = $expectedPolicyClass
        gameplay_hash = ([string]$cookClosure.policy.hashes.gameplay).ToUpperInvariant()
        authoring_hash = ([string]$cookClosure.policy.hashes.authoring).ToUpperInvariant()
        material_hash = ([string]$cookClosure.policy.hashes.materials).ToUpperInvariant()
        decal_hash = ([string]$cookClosure.policy.hashes.decals).ToUpperInvariant()
    }
    runtime_receipt = $runtimeCopyPath
    runtime_receipt_sha256 = if (Test-Path -LiteralPath $runtimeCopyPath) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeCopyPath).Hash
    }
    else { '' }
    runtime_log = $engineLogFullPath
    runtime_log_sha256 = if (Test-Path -LiteralPath $engineLogFullPath) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $engineLogFullPath).Hash
    }
    else { '' }
    project_telemetry = if (Test-Path -LiteralPath $telemetryCopyPath) { $telemetryCopyPath } else { '' }
    screenshot = if (Test-Path -LiteralPath $screenshotCopyPath) { $screenshotCopyPath } else { '' }
    runtime = $runtimeReceipt
    checks = $checks
    failed_checks = @($checks.GetEnumerator() | Where-Object { -not [bool]$_.Value } | ForEach-Object { $_.Key })
    failure_reasons = @($failureReasons)
}
Write-Utf8CreateNew -Path $outputFullPath `
    -Text (($result | ConvertTo-Json -Depth 24) + [Environment]::NewLine)
Write-Output ($result | ConvertTo-Json -Depth 24)
if ($status -ne 'PASS') {
    throw "Packaged Calysto V6 smoke failed: $($failureReasons -join '; ')"
}
