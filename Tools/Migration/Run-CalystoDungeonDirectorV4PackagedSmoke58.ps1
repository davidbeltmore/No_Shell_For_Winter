[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration,
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [ValidateSet("PreCutover", "FinalStrict")]
    [string]$ValidationMode = "FinalStrict",
    [ValidateSet("Natural", "Zero", "EnemyCap25", "ResourceMin", "ResourceMax", "NPCTotal4", "SpecialEvents6")]
    [string]$Scenario = "Natural",
    [Int64]$RunSeed = 202608140058,
    [ValidateRange(1, 100)]
    [int]$MaxFloor = 10,
    [ValidateRange(60, 1800)]
    [int]$TimeoutSeconds = 420,
    [switch]$CaptureVisual,
    [switch]$DisableOutcomeTelemetry,
    [string]$RunTag = "",
    [switch]$ExpectShippingRejection,
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$OutputPath = "",
    [string]$LogPath = ""
)

$ErrorActionPreference = "Stop"

function Test-JsonProperty {
    param([object]$Object, [Parameter(Mandatory = $true)][string]$Name)
    return $null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]
}

function Test-Sha256 {
    param([object]$Value)
    return [string]$Value -cmatch '^[A-Fa-f0-9]{64}$'
}

function Test-TextContains {
    param([string]$Text, [string]$Value)
    return $Text.IndexOf($Value, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

function Find-RegexesInBinary {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][Collections.IDictionary]$Patterns
    )

    $remaining = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $compiled = @{}
    foreach ($entry in $Patterns.GetEnumerator()) {
        $label = [string]$entry.Key
        [void]$remaining.Add($label)
        $compiled[$label] = [Text.RegularExpressions.Regex]::new(
            [string]$entry.Value,
            [Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
                [Text.RegularExpressions.RegexOptions]::CultureInvariant
        )
    }
    $found = [Collections.Generic.List[object]]::new()
    $tailLength = 512
    $asciiTail = ""
    $unicodeTail = ""
    $buffer = [byte[]]::new(4MB)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        while ($remaining.Count -gt 0) {
            $read = $stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            $isFinalChunk = $stream.Position -ge $stream.Length
            $asciiText = $asciiTail + [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
            $unicodeByteCount = $read - ($read % 2)
            $unicodeText = $unicodeTail + [Text.Encoding]::Unicode.GetString($buffer, 0, $unicodeByteCount)
            foreach ($label in @($remaining)) {
                $regex = $compiled[$label]
                $asciiMatch = $regex.Match($asciiText)
                $unicodeMatch = $regex.Match($unicodeText)
                $asciiCutoff = [Math]::Max(0, $asciiText.Length - $tailLength)
                $unicodeCutoff = [Math]::Max(0, $unicodeText.Length - $tailLength)
                $asciiFound = $asciiMatch.Success -and (
                    $isFinalChunk -or $asciiMatch.Index -lt $asciiCutoff
                )
                $unicodeFound = $unicodeMatch.Success -and (
                    $isFinalChunk -or $unicodeMatch.Index -lt $unicodeCutoff
                )
                if ($asciiFound -or $unicodeFound) {
                    $found.Add([pscustomobject]@{
                        label = $label
                        pattern = [string]$Patterns[$label]
                    })
                    [void]$remaining.Remove($label)
                }
            }
            if (!$isFinalChunk) {
                $asciiTail = if ($asciiText.Length -gt $tailLength) {
                    $asciiText.Substring($asciiText.Length - $tailLength)
                } else { $asciiText }
                $unicodeTail = if ($unicodeText.Length -gt $tailLength) {
                    $unicodeText.Substring($unicodeText.Length - $tailLength)
                } else { $unicodeText }
            }
        }
    }
    finally { $stream.Dispose() }
    return @($found)
}

if ($RunSeed -le 0) { throw "RunSeed must be positive." }
if ($Configuration -eq "Shipping" -and $Scenario -ne "Natural" -and !$ExpectShippingRejection) {
    throw "Shipping accepts only Natural. Use -ExpectShippingRejection to prove fail-closed behavior."
}
if ($ExpectShippingRejection -and ($Configuration -ne "Shipping" -or $Scenario -eq "Natural")) {
    throw "-ExpectShippingRejection requires Shipping plus a non-Natural scenario."
}
if ($DisableOutcomeTelemetry -and (
        $Configuration -ne "Development" -or $Scenario -ne "Natural" -or $ExpectShippingRejection)) {
    throw "-DisableOutcomeTelemetry requires Development Natural without expected rejection."
}
if ($Scenario -ne "Natural" -and $MaxFloor -ne 1 -and !$ExpectShippingRejection) {
    throw "Exact V4 population scenarios must run as isolated one-floor processes."
}
if ([string]::IsNullOrWhiteSpace($RunTag)) {
    $RunTag = "{0}_p{1}_{2}" -f `
        [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssfffZ", [Globalization.CultureInfo]::InvariantCulture), `
        $PID, ([Guid]::NewGuid().ToString("N").Substring(0, 8))
}
if ($RunTag -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$') {
    throw "RunTag must match ^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$ exactly."
}

$projectFullPath = [IO.Path]::GetFullPath($ProjectRoot)
$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
if ($archivePath -match '(?i)(?:CALYSTO_PHASE2|PHASE[ _-]*2)') {
    throw "V4 packaged smoke refuses a Phase 2 archive."
}
$windowsRoot = Join-Path $archivePath "Windows"
$manifestPath = Join-Path $windowsRoot "Manifest_UFSFiles_Win64.txt"
$projectDescriptorPath = Join-Path $projectFullPath "NoShellForWinter.uproject"
$receiptGuard = Join-Path $projectFullPath "Tools\Migration\Repair-DazPluginReceipt58.ps1"
$exePath = if ($Configuration -eq "Shipping") {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter-Win64-Shipping.exe"
}
else {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter.exe"
}
$globalUcasPath = Join-Path $windowsRoot "NoShellForWinter\Content\Paks\global.ucas"
foreach ($requiredPath in @($windowsRoot, $manifestPath, $projectDescriptorPath, $receiptGuard, $exePath, $globalUcasPath)) {
    if (!(Test-Path -LiteralPath $requiredPath)) { throw "Required V4 smoke path is missing: $requiredPath" }
}

$safeScenario = $Scenario -replace '[^A-Za-z0-9_-]', '_'
$bundleFullPath = Join-Path $projectFullPath `
    "Saved\Migration\CalystoDungeonDirectorV4\PackagedRuns\$RunTag"
if (Test-Path -LiteralPath $bundleFullPath) {
    throw "RunTag evidence already exists; refusing to overwrite: $bundleFullPath"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $OutputPath = Join-Path $bundleFullPath "RunnerReceipt.json" }
if ([string]::IsNullOrWhiteSpace($LogPath)) { $LogPath = Join-Path $bundleFullPath "Runtime.log" }
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$logFullPath = [IO.Path]::GetFullPath($LogPath)
$engineLogFullPath = Join-Path $bundleFullPath "EngineRuntime.log"
$preservedRuntimeReceiptPath = Join-Path $bundleFullPath "RuntimeReceipt.json"
$copiedScreenshotPath = if ($CaptureVisual) { Join-Path $bundleFullPath "Screenshot.png" } else { "" }
foreach ($artifactPath in @($outputFullPath, $logFullPath, $engineLogFullPath, $preservedRuntimeReceiptPath) +
    @($copiedScreenshotPath | Where-Object { ![string]::IsNullOrWhiteSpace($_) })) {
    if (Test-Path -LiteralPath $artifactPath) { throw "Refusing to overwrite V4 evidence: $artifactPath" }
}

$receiptFileName = "PackagedSmokeReceipt_${Configuration}_${safeScenario}_${RunTag}.json"
$projectTelemetryFileName = "PackagedSmokeTelemetry_${Configuration}_${safeScenario}_${RunTag}.log"
$screenshotFileName = "PackagedSmokeVisual_${Configuration}_${safeScenario}_${RunTag}.png"
$candidateSavedRoots = @(
    (Join-Path $windowsRoot "NoShellForWinter\Saved")
    if (![string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        Join-Path $env:LOCALAPPDATA "NoShellForWinter\Saved"
    }
) | Select-Object -Unique

function ConvertTo-ExtendedLengthLiteralPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ($Path.StartsWith('\\?\', [StringComparison]::Ordinal)) { return $Path }
    $fullPath = [IO.Path]::GetFullPath($Path)
    if ($fullPath.StartsWith('\\', [StringComparison]::Ordinal)) {
        return '\\?\UNC\' + $fullPath.Substring(2)
    }
    return '\\?\' + $fullPath
}

function Get-CandidateArtifacts {
    param([string]$FileName)
    $matches = @()
    foreach ($root in $candidateSavedRoots) {
        $candidate = Join-Path $root ("CalystoDungeonDirectorV4\{0}" -f $FileName)
        $literalCandidate = ConvertTo-ExtendedLengthLiteralPath $candidate
        if (Test-Path -LiteralPath $literalCandidate -PathType Leaf) {
            $item = Get-Item -LiteralPath $literalCandidate
            $matches += [pscustomobject]@{
                DisplayPath = $candidate
                LiteralPath = $literalCandidate
                LastWriteTimeUtc = $item.LastWriteTimeUtc
                Length = $item.Length
            }
        }
    }
    return @($matches | Sort-Object LastWriteTimeUtc -Descending)
}

foreach ($artifactName in @($receiptFileName, $projectTelemetryFileName, $screenshotFileName)) {
    if (@(Get-CandidateArtifacts $artifactName).Count -ne 0) {
        throw "RunTag collision in packaged Saved root for '$artifactName'."
    }
}
[void][IO.Directory]::CreateDirectory($bundleFullPath)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputFullPath))
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($logFullPath))

$arguments = @(
    "/Game/_Game/Hub/HUB",
    "-unattended", "-nop4", "-nosplash", "-NoSound", "-RenderOffscreen",
    "-ResX=1280", "-ResY=720", "-CalystoV4PackagedSmoke",
    "-CalystoV4SmokeRunTag=$RunTag", "-CalystoV4SmokeSeed=$RunSeed",
    "-CalystoV4SmokeMaxFloor=$MaxFloor", "-CalystoV4SmokeTimeout=$TimeoutSeconds",
    "-CalystoV4SmokeScenario=$Scenario", "-ABSLOG=`"$engineLogFullPath`""
)
if ($CaptureVisual) { $arguments += "-CalystoV4SmokeCapture" }
if ($DisableOutcomeTelemetry) { $arguments += "-CalystoV4DisableOutcomeTelemetry" }

$validationChecks = [ordered]@{}
$pendingChecks = [Collections.Generic.List[string]]::new()
$blockedLogFindings = [Collections.Generic.List[object]]::new()
$sequenceFindings = [Collections.Generic.List[object]]::new()
$telemetry = [ordered]@{
    available = $false
    complete_sequences = 0
    expected_sequences = if ($ExpectShippingRejection) { 0 } else { $MaxFloor }
    order_valid = $true
    generate_local_count = 0
    floor_event_count = 0
    completion_event_count = 0
}
$neutralSuppressionCount = 0
$runtimeReceiptSourcePath = ""
$projectTelemetrySourcePath = ""
$runtimeReceipt = $null
$runtimeChecks = @()
$runnerError = ""
$dormantV3Observed = $false
$legacyV3ReflectionFindings = @()
$process = $null
$startedUtc = [DateTime]::UtcNow

try {
    $manifestText = [IO.File]::ReadAllText($manifestPath).Replace("\", "/")
    $projectDescriptor = Get-Content -LiteralPath $projectDescriptorPath -Raw | ConvertFrom-Json
    foreach ($pluginName in @("DazToUnreal", "EFCharacterCreationDazBridge")) {
        $entry = @($projectDescriptor.Plugins | Where-Object { [string]$_.Name -ceq $pluginName })
        $validationChecks["${pluginName}_enabled_in_uproject"] = ($entry.Count -eq 1 -and [bool]$entry[0].Enabled)
    }
    $policyManifestPath = "NoShellForWinter/Content/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.uasset"
    $validationChecks["v4_policy_present_in_staged_manifest"] = Test-TextContains $manifestText $policyManifestPath
    $alwaysForbiddenTokens = @(
        "/CalystoDungeon/V1/", "/CalystoDungeon/V2/",
        "DT_CalystoFloorProfiles", "DT_CalystoSpawnerWeights", "DT_CalystoThemeWeights",
        "DT_CalystoGenerationOptions", "DT_CalystoSpawnerPresets", "DT_CalystoThemePresets",
        "PolicyV2", "PlanV2", "THEME_V2", "CALYSTO_PHASE2"
    )
    $validationChecks["no_v1_v2_or_phase2_in_staged_manifest"] = (
        @($alwaysForbiddenTokens | Where-Object { Test-TextContains $manifestText $_ }).Count -eq 0
    )
    $v3PolicyManifestPath = "NoShellForWinter/Content/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy.uasset"
    if ($ValidationMode -eq "PreCutover") {
        $dormantV3Observed = Test-TextContains $manifestText $v3PolicyManifestPath
        $validationChecks["precutover_v3_permitted_only_as_dormant_content"] = $true
    }
    else {
        $validationChecks["final_strict_no_v3_in_staged_manifest"] = -not (Test-TextContains $manifestText "/CalystoDungeon/V3/")
    }

    $legacyV3ReflectionPatterns = [ordered]@{
        director_policy = 'EFCalystoDungeonDirectorPolicy(?!V4)'
        director_intent = 'EFCalystoDirectorIntent(?!V4)'
        resolved_floor_intent = 'EFCalystoResolvedFloorIntent(?!V4)'
        realized_floor_manifest = 'EFCalystoRealizedFloorManifest(?!V4)'
        run_ecology_state = 'EFCalystoRunEcologyState(?!V4)'
        population_materializer = 'EFCalystoPopulationMaterializer(?!V4)'
        population_materialization_result = 'EFCalystoPopulationMaterializationResult(?!V4)'
        dungeon_math = 'EFCalystoDungeonDirectorMath(?!V4)'
        dungeon_types = 'EFCalystoDungeonTypes(?!V4)'
        dungeon_snapshot = 'EFCalystoDungeonSnapshot(?!V4)'
        floor_outcome = 'EFCalystoFloorOutcome(?!V4)'
        spawn_directive = 'EFCalystoSpawnDirective(?!V4)'
        companion_run_snapshot = 'EFCalystoCompanionRunSnapshot(?!V4)'
    }
    $runtimeBinaries = @(
        Get-ChildItem -LiteralPath (Split-Path -Parent $exePath) -File |
            Where-Object { $_.Extension -in @(".exe", ".dll") } |
            Sort-Object FullName
    )
    $legacyV3ReflectionFindings = @(
        foreach ($binary in @($runtimeBinaries + @(Get-Item -LiteralPath $globalUcasPath))) {
            foreach ($finding in @(Find-RegexesInBinary $binary.FullName $legacyV3ReflectionPatterns)) {
                [pscustomobject]@{
                    binary = $binary.FullName
                    label = $finding.label
                    pattern = $finding.pattern
                }
            }
        }
    )
    if ($ValidationMode -eq "FinalStrict") {
        $validationChecks["final_strict_no_legacy_v3_reflected_types"] = (
            $legacyV3ReflectionFindings.Count -eq 0
        )
    }
    else {
        $dormantV3Observed = $dormantV3Observed -or ($legacyV3ReflectionFindings.Count -gt 0)
    }
    $dazManifestPaths = @(
        "Engine/Plugins/DazToUnreal/DazToUnreal.uplugin",
        "NoShellForWinter/Plugins/EFCharacterCreationDazBridge/EFCharacterCreationDazBridge.uplugin"
    )
    $validationChecks["daz_descriptors_present_in_staged_manifest"] = (
        @($dazManifestPaths | Where-Object { !(Test-TextContains $manifestText $_) }).Count -eq 0
    )
    $dazReceiptOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $projectFullPath -TargetName NoShellForWinter `
        -Configuration $Configuration -VerifyOnly 2>&1 | Out-String
    $validationChecks["daz_receipt_verify_pass"] = ($LASTEXITCODE -eq 0)

    $process = Start-Process -FilePath $exePath -ArgumentList $arguments `
        -WorkingDirectory $windowsRoot -WindowStyle Hidden -PassThru
    $waitMilliseconds = [Math]::Min([int64]::MaxValue, ([int64]$TimeoutSeconds + 90L) * 1000L)
    if (!$process.WaitForExit([int]$waitMilliseconds)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Packaged V4 smoke exceeded the external timeout ($TimeoutSeconds + 90 seconds)."
    }
    $process.Refresh()

    $projectTelemetryCandidates = @(Get-CandidateArtifacts $projectTelemetryFileName | Where-Object {
        $_.LastWriteTimeUtc -ge $startedUtc.AddSeconds(-2) -and $_.Length -gt 0
    })
    if ($projectTelemetryCandidates.Count -ne 1) {
        throw "Expected exactly one fresh V4 project telemetry '$projectTelemetryFileName'; found $($projectTelemetryCandidates.Count)."
    }
    $projectTelemetrySourcePath = $projectTelemetryCandidates[0].DisplayPath
    Copy-Item -LiteralPath $projectTelemetryCandidates[0].LiteralPath -Destination $logFullPath

    $receiptCandidates = @(Get-CandidateArtifacts $receiptFileName | Where-Object {
        $_.LastWriteTimeUtc -ge $startedUtc.AddSeconds(-2)
    })
    if ($receiptCandidates.Count -ne 1) {
        throw "Expected exactly one fresh V4 runtime receipt '$receiptFileName'; found $($receiptCandidates.Count)."
    }
    $runtimeReceiptSourcePath = $receiptCandidates[0].DisplayPath
    Copy-Item -LiteralPath $receiptCandidates[0].LiteralPath -Destination $preservedRuntimeReceiptPath
    $runtimeReceipt = Get-Content -LiteralPath $preservedRuntimeReceiptPath -Raw | ConvertFrom-Json
    if ($null -ne $runtimeReceipt.checks) {
        $runtimeChecks = @($runtimeReceipt.checks.PSObject.Properties | Where-Object { -not [bool]$_.Value })
    }

    $expectedRuntimeStatus = if ($ExpectShippingRejection) { "FAIL" } else { "PASS" }
    $validationChecks["runner_completed"] = $true
    $validationChecks["process_exit_code_zero"] = ($process.HasExited -and $process.ExitCode -eq 0)
    $validationChecks["runtime_schema_4"] = ([int]$runtimeReceipt.schema_version -eq 4)
    $validationChecks["runtime_artifact_schema_3"] = ([int]$runtimeReceipt.artifact_schema_version -eq 3)
    $validationChecks["runtime_generator_4"] = ([int]$runtimeReceipt.generator_version -eq 4)
    $validationChecks["run_tag_matches"] = ([string]$runtimeReceipt.run_tag -ceq $RunTag)
    $validationChecks["expected_runtime_status"] = ([string]$runtimeReceipt.status -ceq $expectedRuntimeStatus)
    $validationChecks["configuration_matches"] = ([string]$runtimeReceipt.configuration -ceq $Configuration)
    $validationChecks["scenario_matches"] = ([string]$runtimeReceipt.scenario -ceq $Scenario)
    $validationChecks["seed_matches"] = (
        [string]$runtimeReceipt.run_seed -ceq $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    )
    $validationChecks["maximum_floor_matches"] = ([int]$runtimeReceipt.maximum_floor -eq $MaxFloor)
    $validationChecks["cooked_unattended_runtime_confirmed"] = (
        [bool]$runtimeReceipt.requires_cooked_data -and [bool]$runtimeReceipt.unattended
    )
    $validationChecks["build_configuration_flags_match"] = (
        [bool]$runtimeReceipt.shipping_build -eq ($Configuration -eq "Shipping") -and
        [bool]$runtimeReceipt.exact_population_controls_compiled -eq ($Configuration -eq "Development")
    )
    $validationChecks["project_telemetry_schema_1"] = ([int]$runtimeReceipt.project_telemetry_schema_version -eq 1)
    $validationChecks["project_telemetry_writer_healthy"] = (
        [bool]$runtimeReceipt.project_telemetry_initialized -and [bool]$runtimeReceipt.project_telemetry_healthy
    )
    $validationChecks["v4_authority_identity_exact"] = (
        [string]$runtimeReceipt.policy_path -ceq "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy" -and
        [string]$runtimeReceipt.policy_class -ceq "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4" -and
        [int]$runtimeReceipt.policy_schema_version -eq 4 -and
        [int]$runtimeReceipt.policy_generator_version -eq 4 -and
        -not [bool]$runtimeReceipt.legacy_authority_loaded
    )
    $validationChecks["policy_hash_is_sha256"] = Test-Sha256 $runtimeReceipt.policy_hash

    if ($ExpectShippingRejection) {
        $validationChecks["shipping_override_rejected"] = ([string]$runtimeReceipt.reason -ceq "SHIPPING_OVERRIDE_REJECTED")
        $validationChecks["shipping_rejection_generated_no_floor"] = (
            [int]$runtimeReceipt.completed_floor_count -eq 0 -and @($runtimeReceipt.floors).Count -eq 0
        )
        $validationChecks["shipping_exact_controls_absent"] = -not [bool]$runtimeReceipt.exact_population_controls_compiled
    }
    else {
        $floors = @($runtimeReceipt.floors)
        $requiredFloorFields = @(
            "generator_version", "floor_number", "generation_serial", "pcg_seed", "style", "theme",
            "size_x", "size_y", "size_z", "candidate_anchor_count", "enemy_count", "npc_count",
            "food_count", "chest_count", "loose_loot_count", "clothing_count", "special_event_count",
            "spawned_actor_count", "realized_threat_cost", "realized_resource_cost", "policy_hash",
            "ecology_hash", "outcome_hash", "frozen_outcome", "intent_hash", "anchor_topology_hash",
            "population_hash", "resource_hash", "companion_snapshot_hash", "manifest_hash"
        )
        $missingFloorFields = @(
            foreach ($floor in $floors) {
                foreach ($field in $requiredFloorFields) {
                    if (!(Test-JsonProperty $floor $field)) { "floor=$($floor.floor_number):$field" }
                }
            }
        )
        $validationChecks["runtime_checks_all_pass"] = ($runtimeChecks.Count -eq 0)
        $validationChecks["all_floors_recorded"] = (
            [int]$runtimeReceipt.completed_floor_count -eq $MaxFloor -and $floors.Count -eq $MaxFloor
        )
        $validationChecks["required_v4_floor_fields_present"] = ($missingFloorFields.Count -eq 0)
        $validationChecks["real_acf_door_count"] = (
            [int]$runtimeReceipt.door_interaction_count -eq [Math]::Max(0, $MaxFloor - 1)
        )
        $validationChecks["floor_and_serial_sequence"] = (
            @($floors | Where-Object {
                [int64]$_.floor_number -ne [int64]$_.generation_serial -or
                [int64]$_.floor_number -lt 1 -or [int64]$_.floor_number -gt $MaxFloor
            }).Count -eq 0 -and
            @($floors | ForEach-Object { [int64]$_.floor_number } | Sort-Object -Unique).Count -eq $MaxFloor
        )
        $validationChecks["all_floor_generators_are_4"] = (@($floors | Where-Object { [int]$_.generator_version -ne 4 }).Count -eq 0)
        $validationChecks["style_and_theme_are_v4"] = (
            @($floors | Where-Object {
                @("Standard", "Compact", "Branching") -cnotcontains [string]$_.style -or
                @("Default", "Forge", "Shrine") -cnotcontains [string]$_.theme
            }).Count -eq 0
        )
        $validationChecks["certified_sizes_only"] = (
            @($floors | Where-Object {
                [int]$_.size_x -lt 26 -or [int]$_.size_x -gt 30 -or
                [int]$_.size_y -ne [int]$_.size_x -or [int]$_.size_z -ne 1
            }).Count -eq 0
        )
        $validationChecks["candidate_anchors_remain_structural"] = (
            @($floors | Where-Object { [int]$_.candidate_anchor_count -le 0 }).Count -eq 0
        )
        $validationChecks["v4_hard_caps_and_sum_respected"] = (
            @($floors | Where-Object {
                [int]$_.enemy_count -lt 0 -or [int]$_.enemy_count -gt 25 -or
                [int]$_.npc_count -lt 0 -or [int]$_.npc_count -gt 4 -or
                [int]$_.food_count -lt 0 -or [int]$_.food_count -gt 30 -or
                [int]$_.chest_count -lt 0 -or [int]$_.chest_count -gt 10 -or
                [int]$_.loose_loot_count -lt 0 -or [int]$_.loose_loot_count -gt 4 -or
                [int]$_.clothing_count -lt 0 -or [int]$_.clothing_count -gt 10 -or
                [int]$_.special_event_count -lt 0 -or [int]$_.special_event_count -gt 6 -or
                [int]$_.spawned_actor_count -lt 0 -or [int]$_.spawned_actor_count -gt 89 -or
                [int]$_.spawned_actor_count -ne (
                    [int]$_.enemy_count + [int]$_.npc_count + [int]$_.food_count +
                    [int]$_.chest_count + [int]$_.loose_loot_count + [int]$_.clothing_count +
                    [int]$_.special_event_count)
            }).Count -eq 0
        )
        $hashFields = @(
            "policy_hash", "ecology_hash", "outcome_hash", "intent_hash", "anchor_topology_hash",
            "population_hash", "resource_hash", "companion_snapshot_hash", "manifest_hash"
        )
        $validationChecks["all_v4_hashes_present"] = (
            @($floors | Where-Object {
                $currentFloor = $_
                $bad = $false
                foreach ($hashField in $hashFields) {
                    if (!(Test-Sha256 $currentFloor.$hashField)) { $bad = $true }
                }
                $bad -or [string]$currentFloor.policy_hash -cne [string]$runtimeReceipt.policy_hash
            }).Count -eq 0
        )
        $validationChecks["frozen_outcome_shape_present"] = (
            @($floors | Where-Object {
                !(Test-JsonProperty $_.frozen_outcome "is_frozen") -or
                !(Test-JsonProperty $_.frozen_outcome "combat") -or
                !(Test-JsonProperty $_.frozen_outcome "survival") -or
                !(Test-JsonProperty $_.frozen_outcome "resources") -or
                !(Test-JsonProperty $_.frozen_outcome "pace") -or
                !(Test-JsonProperty $_.frozen_outcome "deaths_and_failures")
            }).Count -eq 0
        )
        if ($DisableOutcomeTelemetry) {
            $validationChecks["missing_telemetry_is_neutral"] = (
                @($floors | Where-Object {
                    [double]$_.frozen_outcome.combat -ne 0.5 -or
                    [double]$_.frozen_outcome.survival -ne 0.5 -or
                    [double]$_.frozen_outcome.resources -ne 0.5 -or
                    [double]$_.frozen_outcome.pace -ne 0.5 -or
                    [double]$_.frozen_outcome.deaths_and_failures -ne 0.0
                }).Count -eq 0
            )
        }
        if ($Scenario -in @("Zero", "ResourceMin")) {
            $validationChecks["${Scenario}_scenario_exact"] = (
                $floors.Count -eq 1 -and [int]$floors[0].spawned_actor_count -eq 0 -and
                @([int]$floors[0].enemy_count, [int]$floors[0].npc_count, [int]$floors[0].food_count,
                    [int]$floors[0].chest_count, [int]$floors[0].loose_loot_count,
                    [int]$floors[0].clothing_count, [int]$floors[0].special_event_count |
                    Where-Object { $_ -ne 0 }).Count -eq 0
            )
        }
        if ($Scenario -eq "EnemyCap25") {
            $validationChecks["enemy_cap_25_exact"] = (
                $floors.Count -eq 1 -and [int]$floors[0].enemy_count -eq 25 -and
                [int]$floors[0].spawned_actor_count -eq 25 -and
                @([int]$floors[0].npc_count, [int]$floors[0].food_count, [int]$floors[0].chest_count,
                    [int]$floors[0].loose_loot_count, [int]$floors[0].clothing_count,
                    [int]$floors[0].special_event_count | Where-Object { $_ -ne 0 }).Count -eq 0
            )
        }
        if ($Scenario -eq "ResourceMax") {
            $validationChecks["resource_max_exact"] = (
                $floors.Count -eq 1 -and [int]$floors[0].food_count -eq 30 -and
                [int]$floors[0].chest_count -eq 10 -and [int]$floors[0].spawned_actor_count -eq 40 -and
                @([int]$floors[0].enemy_count, [int]$floors[0].npc_count,
                    [int]$floors[0].loose_loot_count, [int]$floors[0].clothing_count,
                    [int]$floors[0].special_event_count | Where-Object { $_ -ne 0 }).Count -eq 0
            )
        }
        if ($Scenario -eq "NPCTotal4") {
            $validationChecks["npc_total_4_exact"] = (
                $floors.Count -eq 1 -and [int]$floors[0].npc_count -eq 4 -and
                [int]$floors[0].spawned_actor_count -eq 4 -and
                @([int]$floors[0].enemy_count, [int]$floors[0].food_count,
                    [int]$floors[0].chest_count, [int]$floors[0].loose_loot_count,
                    [int]$floors[0].clothing_count, [int]$floors[0].special_event_count |
                    Where-Object { $_ -ne 0 }).Count -eq 0
            )
        }
        if ($Scenario -eq "SpecialEvents6") {
            $validationChecks["special_events_6_exact"] = (
                $floors.Count -eq 1 -and [int]$floors[0].special_event_count -eq 6 -and
                [int]$floors[0].spawned_actor_count -eq 6 -and
                @([int]$floors[0].enemy_count, [int]$floors[0].npc_count,
                    [int]$floors[0].food_count, [int]$floors[0].chest_count,
                    [int]$floors[0].loose_loot_count, [int]$floors[0].clothing_count |
                    Where-Object { $_ -ne 0 }).Count -eq 0
            )
        }
    }

    $runtimeLogAvailable = Test-Path -LiteralPath $logFullPath -PathType Leaf
    $engineRuntimeLogAvailable = Test-Path -LiteralPath $engineLogFullPath -PathType Leaf
    $validationChecks["project_owned_runtime_telemetry_produced"] = $runtimeLogAvailable
    if ($Configuration -eq "Development") {
        $validationChecks["development_engine_runtime_log_produced"] = $engineRuntimeLogAvailable
    }

    $projectLogLines = if ($runtimeLogAvailable) { @(Get-Content -LiteralPath $logFullPath) } else { @() }
    $engineLogLines = if ($engineRuntimeLogAvailable) { @(Get-Content -LiteralPath $engineLogFullPath) } else { @() }
    $expectedHeader = "CALYSTO_V4_PROJECT_TELEMETRY schema=1 runTag=$RunTag configuration=$Configuration scenario=$Scenario"
    $validationChecks["project_telemetry_header_exact"] = (
        @($projectLogLines | Where-Object { [string]$_ -ceq $expectedHeader }).Count -eq 1
    )
    $sequenceNumbers = @(
        foreach ($line in $projectLogLines) {
            if ([string]$line -match ' sequence=([0-9]+) ') { [int64]$Matches[1] }
        }
    )
    $expectedSequenceNumbers = if ($sequenceNumbers.Count -gt 0) { @(1..$sequenceNumbers.Count | ForEach-Object { [int64]$_ }) } else { @() }
    $validationChecks["project_telemetry_global_sequence_exact"] = (
        $projectLogLines.Count -eq ($sequenceNumbers.Count + 1) -and
        @(Compare-Object -ReferenceObject $expectedSequenceNumbers -DifferenceObject $sequenceNumbers -SyncWindow 0).Count -eq 0
    )

    $blockedPatterns = @(
        "Object Transform", "GetAttributeFromPointIndex_0", "Blueprint Runtime Error",
        "Ensure condition failed", "Fatal error", "requested GenerateLocal more than once",
        "duplicate GenerateLocal", "CalystoV3PackagedSmoke", "CALYSTO_PHASE2"
    )
    foreach ($source in @(
        [pscustomobject]@{ name = "ProjectTelemetry"; lines = $projectLogLines },
        [pscustomobject]@{ name = "EngineRuntime"; lines = $engineLogLines }
    )) {
        foreach ($line in $source.lines) {
            foreach ($pattern in $blockedPatterns) {
                if (Test-TextContains $line $pattern) {
                    $blockedLogFindings.Add([pscustomobject]@{
                        source = $source.name; pattern = $pattern; line = [string]$line
                    })
                }
            }
        }
    }
    $validationChecks["blocked_runtime_log_findings_empty"] = ($blockedLogFindings.Count -eq 0)

    $authorityEvents = @($projectLogLines | Where-Object { Test-TextContains $_ " event=Authority " })
    $failureEvents = @($projectLogLines | Where-Object { Test-TextContains $_ " event=Failure " })
    $doorSelectedEvents = @($projectLogLines | Where-Object { Test-TextContains $_ " event=DoorSelected " })
    $doorInteractedEvents = @($projectLogLines | Where-Object { Test-TextContains $_ " event=DoorInteracted " })
    $floorEvents = @($projectLogLines | Where-Object { Test-TextContains $_ " event=FloorReady " })
    $completionEvents = @($projectLogLines | Where-Object { Test-TextContains $_ " event=Complete " })
    $neutralSuppressionCount = @($projectLogLines | Where-Object {
        Test-TextContains $_ " event=OutcomeTelemetrySuppressed "
    }).Count
    $telemetry.floor_event_count = $floorEvents.Count
    $telemetry.completion_event_count = $completionEvents.Count
    $validationChecks["authority_event_exact"] = (
        $authorityEvents.Count -eq 1 -and
        (Test-TextContains $authorityEvents[0] "policyPath=/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy") -and
        (Test-TextContains $authorityEvents[0] "policyClass=/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV4") -and
        (Test-TextContains $authorityEvents[0] "schema=4 generator=4 policyHash=$($runtimeReceipt.policy_hash) legacyAuthorityLoaded=false")
    )

    if (!$ExpectShippingRejection) {
        $stage = 0
        $traceFloor = 1
        $stageNames = @("GenerateLocal", "PCGComplete", "NavigationPathReady", "EnemyLevelsReady", "PopulationRealized", "CompanionRosterReady", "DoorEnabled")
        foreach ($line in $projectLogLines) {
            $observed = -1
            for ($stageIndex = 0; $stageIndex -lt $stageNames.Count; ++$stageIndex) {
                if (Test-TextContains $line "event=$($stageNames[$stageIndex]) source=PCGRuntimeTrace") {
                    $observed = $stageIndex
                    break
                }
            }
            if ($observed -ge 0) {
                $telemetry.available = $true
                if ($observed -eq 0) { ++$telemetry.generate_local_count }
                $lineFloor = if ([string]$line -match ' floor=([0-9]+)') { [int64]$Matches[1] } else { -1L }
                $lineSerial = if ([string]$line -match ' serial=([0-9]+)') { [int64]$Matches[1] } else { -1L }
                if ($observed -ne $stage -or $lineFloor -ne $traceFloor -or $lineSerial -ne $traceFloor) {
                    $telemetry.order_valid = $false
                    $sequenceFindings.Add([pscustomobject]@{
                        expected = $stageNames[$stage]
                        observed = $stageNames[$observed]
                        expected_floor = $traceFloor
                        observed_floor = $lineFloor
                        observed_serial = $lineSerial
                        line = [string]$line
                    })
                }
                if ($observed -eq ($stageNames.Count - 1)) {
                    if ($stage -eq ($stageNames.Count - 1)) { ++$telemetry.complete_sequences }
                    $stage = 0
                    ++$traceFloor
                }
                else { $stage = $observed + 1 }
            }
        }
        if ($stage -ne 0) { $telemetry.order_valid = $false }
        $validationChecks["real_pcg_runtime_readiness_sequence_exact"] = (
            [bool]$telemetry.available -and [bool]$telemetry.order_valid -and
            [int]$telemetry.complete_sequences -eq $MaxFloor -and $traceFloor -eq ($MaxFloor + 1)
        )
        $validationChecks["exactly_one_generate_local_per_floor"] = (
            [int]$telemetry.generate_local_count -eq $MaxFloor
        )
        $eventIdentityValid = $floorEvents.Count -eq $MaxFloor -and $doorSelectedEvents.Count -eq $MaxFloor -and
            $doorInteractedEvents.Count -eq [Math]::Max(0, $MaxFloor - 1)
        if ($eventIdentityValid) {
            for ($eventIndex = 0; $eventIndex -lt $MaxFloor; ++$eventIndex) {
                $expectedFloor = $eventIndex + 1
                if ([string]$floorEvents[$eventIndex] -notmatch " floor=$expectedFloor serial=$expectedFloor(?:\s|$)" -or
                    [string]$doorSelectedEvents[$eventIndex] -notmatch " floor=$expectedFloor(?:\s|$)") {
                    $eventIdentityValid = $false
                    break
                }
                if ($eventIndex -lt ($MaxFloor - 1)) {
                    $expectedDestination = $expectedFloor + 1
                    if ([string]$doorInteractedEvents[$eventIndex] -notmatch
                        " floor=$expectedFloor destination=$expectedDestination(?:\s|$)") {
                        $eventIdentityValid = $false
                        break
                    }
                }
            }
        }
        $validationChecks["project_telemetry_floor_and_door_identity_exact"] = $eventIdentityValid
        $validationChecks["project_telemetry_event_totals_exact"] = (
            $floorEvents.Count -eq $MaxFloor -and $doorSelectedEvents.Count -eq $MaxFloor -and
            $doorInteractedEvents.Count -eq [Math]::Max(0, $MaxFloor - 1) -and
            $completionEvents.Count -eq 1 -and $failureEvents.Count -eq 0
        )
        $validationChecks["project_telemetry_completion_identity_exact"] = (
            $completionEvents.Count -eq 1 -and
            (Test-TextContains $completionEvents[0] "event=Complete status=PASS tag=$RunTag") -and
            (Test-TextContains $completionEvents[0] "reason=PASS")
        )
        $validationChecks["receipt_telemetry_totals_match"] = (
            [int]$runtimeReceipt.project_telemetry_ready_sequence_count -eq $MaxFloor -and
            [int]$runtimeReceipt.project_telemetry_door_selected_count -eq $MaxFloor -and
            [int]$runtimeReceipt.project_telemetry_door_interacted_count -eq [Math]::Max(0, $MaxFloor - 1) -and
            [int]$runtimeReceipt.project_telemetry_failure_event_count -eq 0 -and
            [int]$runtimeReceipt.project_telemetry_complete_event_count -eq 1 -and
            [int64]$runtimeReceipt.project_telemetry_sequence_count -eq $sequenceNumbers.Count
        )
    }
    else {
        $validationChecks["shipping_rejection_project_telemetry_exact"] = (
            $floorEvents.Count -eq 0 -and $doorSelectedEvents.Count -eq 0 -and
            $doorInteractedEvents.Count -eq 0 -and $completionEvents.Count -eq 1 -and
            $failureEvents.Count -eq 1 -and
            (Test-TextContains $failureEvents[0] "reason=SHIPPING_OVERRIDE_REJECTED") -and
            (Test-TextContains $completionEvents[0] "event=Complete status=FAIL tag=$RunTag") -and
            (Test-TextContains $completionEvents[0] "reason=SHIPPING_OVERRIDE_REJECTED")
        )
    }

    if ($DisableOutcomeTelemetry) {
        $validationChecks["neutral_outcome_suppression_count"] = (
            $neutralSuppressionCount -eq [Math]::Max(0, $MaxFloor - 1)
        )
    }
    if ($CaptureVisual -and !$ExpectShippingRejection) {
        $screenshotCandidates = @(Get-CandidateArtifacts $screenshotFileName | Where-Object {
            $_.LastWriteTimeUtc -ge $startedUtc.AddSeconds(-2) -and $_.Length -gt 1024
        })
        $validationChecks["visual_capture_produced"] = ($screenshotCandidates.Count -eq 1)
        $validationChecks["visual_capture_is_final_floor"] = (
            [int]$runtimeReceipt.screenshot_floor -eq $MaxFloor -and
            [string]$runtimeReceipt.visual_capture_mode -ceq "SceneRenderTargetNoSlate" -and
            -not [bool]$runtimeReceipt.screenshot_show_ui
        )
        if ($screenshotCandidates.Count -eq 1) {
            Copy-Item -LiteralPath $screenshotCandidates[0].LiteralPath -Destination $copiedScreenshotPath
        }
    }
}
catch {
    $runnerError = $_.Exception.Message
    $validationChecks["runner_completed"] = $false
    if (!(Test-Path -LiteralPath $logFullPath -PathType Leaf)) {
        $telemetryRecoveryCandidates = @(Get-CandidateArtifacts $projectTelemetryFileName | Where-Object {
            $_.LastWriteTimeUtc -ge $startedUtc.AddSeconds(-2) -and $_.Length -gt 0
        })
        if ($telemetryRecoveryCandidates.Count -eq 1) {
            $projectTelemetrySourcePath = $telemetryRecoveryCandidates[0].DisplayPath
            Copy-Item -LiteralPath $telemetryRecoveryCandidates[0].LiteralPath -Destination $logFullPath
        }
    }
    if (!(Test-Path -LiteralPath $preservedRuntimeReceiptPath -PathType Leaf)) {
        $recoveryCandidates = @(Get-CandidateArtifacts $receiptFileName | Where-Object {
            $_.LastWriteTimeUtc -ge $startedUtc.AddSeconds(-2)
        })
        if ($recoveryCandidates.Count -eq 1) {
            $runtimeReceiptSourcePath = $recoveryCandidates[0].DisplayPath
            Copy-Item -LiteralPath $recoveryCandidates[0].LiteralPath -Destination $preservedRuntimeReceiptPath
        }
    }
    if (Test-Path -LiteralPath $preservedRuntimeReceiptPath -PathType Leaf) {
        try { $runtimeReceipt = Get-Content -LiteralPath $preservedRuntimeReceiptPath -Raw | ConvertFrom-Json }
        catch { $runnerError += "; runtime receipt parse failed: $($_.Exception.Message)" }
    }
}

$failedChecks = @($validationChecks.GetEnumerator() | Where-Object { -not [bool]$_.Value } | ForEach-Object { $_.Key })
if (![string]::IsNullOrWhiteSpace($runnerError) -and $failedChecks -notcontains "runner_exception") {
    $failedChecks += "runner_exception"
}
$status = if ($failedChecks.Count -gt 0) { "FAIL" } elseif ($pendingChecks.Count -gt 0) { "PENDING" } else { "PASS" }
$result = [ordered]@{
    schema_version = 4
    artifact_schema_version = 1
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = $status
    semantic = if ($ExpectShippingRejection) { "expected_shipping_override_rejection" } else { "v4_packaged_runtime_acceptance" }
    run_tag = $RunTag
    bundle_root = $bundleFullPath
    configuration = $Configuration
    validation_mode = $ValidationMode
    dormant_v3_observed = $dormantV3Observed
    legacy_v3_reflection_findings = $legacyV3ReflectionFindings
    scenario = $Scenario
    outcome_mode = if ($DisableOutcomeTelemetry) { "neutral_missing_telemetry" } else { "live" }
    archive_root = $archivePath
    startup_map = "/Game/_Game/Hub/HUB"
    executable = $exePath
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $exePath).Hash
    process_exit_code = if ($null -ne $process -and $process.HasExited) { $process.ExitCode } else { $null }
    runtime_receipt_source = $runtimeReceiptSourcePath
    project_telemetry_source = $projectTelemetrySourcePath
    runtime_receipt = if (Test-Path -LiteralPath $preservedRuntimeReceiptPath) { $preservedRuntimeReceiptPath } else { "" }
    runtime_receipt_sha256 = if (Test-Path -LiteralPath $preservedRuntimeReceiptPath) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $preservedRuntimeReceiptPath).Hash
    } else { "" }
    copied_screenshot = if (![string]::IsNullOrWhiteSpace($copiedScreenshotPath) -and (Test-Path $copiedScreenshotPath)) {
        $copiedScreenshotPath
    } else { "" }
    log = $logFullPath
    runtime_log_available = [bool](Test-Path -LiteralPath $logFullPath -PathType Leaf)
    log_sha256 = if (Test-Path -LiteralPath $logFullPath -PathType Leaf) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $logFullPath).Hash
    } else { "" }
    engine_log = $engineLogFullPath
    engine_runtime_log_available = [bool](Test-Path -LiteralPath $engineLogFullPath -PathType Leaf)
    engine_log_sha256 = if (Test-Path -LiteralPath $engineLogFullPath -PathType Leaf) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $engineLogFullPath).Hash
    } else { "" }
    runtime = $runtimeReceipt
    telemetry = $telemetry
    sequence_findings = $sequenceFindings
    neutral_suppression_count = $neutralSuppressionCount
    blocked_log_findings = $blockedLogFindings
    runner_error = $runnerError
    checks = $validationChecks
    pending_checks = $pendingChecks
    failed_checks = $failedChecks
}
$resultJson = ConvertTo-Json -InputObject $result -Depth 16
[IO.File]::WriteAllText($outputFullPath, $resultJson + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output $resultJson
if ($status -ne "PASS") {
    throw "Packaged V4 smoke status $status; failed=$($failedChecks -join ', '); pending=$($pendingChecks -join ', '); $runnerError"
}
