[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration,
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [ValidateSet("Natural", "Zero", "EnemyCap25")]
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
    param(
        [object]$Object,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )
    return $null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]
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
    throw "-DisableOutcomeTelemetry requires an unattended Development Natural run."
}
if ($Scenario -ne "Natural" -and $MaxFloor -ne 1 -and !$ExpectShippingRejection) {
    throw "Exact population scenarios must run as isolated one-floor processes."
}

if ([string]::IsNullOrWhiteSpace($RunTag)) {
    $RunTag = "{0}_p{1}_{2}" -f `
        [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssfffZ", [Globalization.CultureInfo]::InvariantCulture), `
        $PID, `
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
}
if ($RunTag -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$') {
    throw "RunTag must match ^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$ exactly."
}

$projectFullPath = [IO.Path]::GetFullPath($ProjectRoot)
$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$windowsRoot = Join-Path $archivePath "Windows"
$exePath = if ($Configuration -eq "Shipping") {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter-Win64-Shipping.exe"
}
else {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter.exe"
}
if (!(Test-Path -LiteralPath $exePath -PathType Leaf)) {
    throw "Packaged executable is missing: $exePath"
}

$safeScenario = $Scenario -replace '[^A-Za-z0-9_-]', '_'
$expectation = if ($ExpectShippingRejection) { "Rejected" } else { "Pass" }
$bundleFullPath = Join-Path $projectFullPath `
    "Saved\Migration\CalystoDungeonDirectorV3\PackagedRuns\$RunTag"
if (Test-Path -LiteralPath $bundleFullPath) {
    throw "RunTag bundle already exists; refusing to overwrite evidence: $bundleFullPath"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $bundleFullPath "RunnerReceipt.json"
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $bundleFullPath "Runtime.log"
}
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
$logFullPath = [IO.Path]::GetFullPath($LogPath)
$preservedRuntimeReceiptPath = Join-Path $bundleFullPath "RuntimeReceipt.json"
$copiedScreenshotPath = if ($CaptureVisual) { Join-Path $bundleFullPath "Screenshot.png" } else { "" }

foreach ($artifactPath in @(
        $outputFullPath,
        $logFullPath,
        $preservedRuntimeReceiptPath,
        $copiedScreenshotPath | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })) {
    if (Test-Path -LiteralPath $artifactPath) {
        throw "Refusing to overwrite packaged evidence: $artifactPath"
    }
}

$receiptFileName = "PackagedSmokeReceipt_${Configuration}_${safeScenario}_${RunTag}.json"
$screenshotFileName = "PackagedSmokeVisual_${Configuration}_${safeScenario}_${RunTag}.png"
$candidateSavedRoots = @(
    (Join-Path $windowsRoot "NoShellForWinter\Saved")
    if (![string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        Join-Path $env:LOCALAPPDATA "NoShellForWinter\Saved"
    }
) | Select-Object -Unique

function ConvertTo-ExtendedLengthLiteralPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if ($Path.StartsWith('\\?\', [StringComparison]::Ordinal)) {
        return $Path
    }

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
        $candidate = Join-Path $root ("CalystoDungeonDirectorV3\{0}" -f $FileName)
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

foreach ($artifactName in @($receiptFileName, $screenshotFileName)) {
    if (@(Get-CandidateArtifacts $artifactName).Count -ne 0) {
        throw "RunTag collision found in a packaged Saved root for '$artifactName'."
    }
}

New-Item -ItemType Directory -Path $bundleFullPath | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $outputFullPath) -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $logFullPath) -Force | Out-Null

$arguments = @(
    "-unattended",
    "-nop4",
    "-nosplash",
    "-NoSound",
    "-RenderOffscreen",
    "-ResX=1280",
    "-ResY=720",
    "-CalystoV3PackagedSmoke",
    "-CalystoV3SmokeRunTag=$RunTag",
    "-CalystoV3SmokeSeed=$RunSeed",
    "-CalystoV3SmokeMaxFloor=$MaxFloor",
    "-CalystoV3SmokeTimeout=$TimeoutSeconds",
    "-CalystoV3SmokeScenario=$Scenario",
    "-ABSLOG=`"$logFullPath`""
)
if ($CaptureVisual) { $arguments += "-CalystoV3SmokeCapture" }
if ($DisableOutcomeTelemetry) { $arguments += "-CalystoV3DisableOutcomeTelemetry" }

$startedUtc = [DateTime]::UtcNow
$process = $null
$runtimeReceiptSourcePath = ""
$runtimeReceipt = $null
$runtimeChecks = @()
$runnerError = ""
$validationChecks = [ordered]@{}
$blockedLogFindings = [Collections.Generic.List[object]]::new()
$telemetry = [ordered]@{
    available = $false
    complete_sequences = 0
    expected_sequences = if ($ExpectShippingRejection) { 0 } else { $MaxFloor }
    order_valid = $true
}
$neutralSuppressionCount = 0

try {
    $process = Start-Process -FilePath $exePath -ArgumentList $arguments `
        -WorkingDirectory $windowsRoot -WindowStyle Hidden -PassThru
    $waitMilliseconds = [Math]::Min([int64]::MaxValue, ([int64]$TimeoutSeconds + 90L) * 1000L)
    if (!$process.WaitForExit([int]$waitMilliseconds)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Packaged smoke exceeded the external timeout ($TimeoutSeconds + 90 seconds)."
    }
    $process.Refresh()

    $receiptCandidates = @(Get-CandidateArtifacts $receiptFileName | Where-Object {
        $_.LastWriteTimeUtc -ge $startedUtc.AddSeconds(-2)
    })
    if ($receiptCandidates.Count -ne 1) {
        throw "Expected exactly one fresh packaged receipt '$receiptFileName'; found $($receiptCandidates.Count)."
    }
    $runtimeReceiptSourcePath = $receiptCandidates[0].DisplayPath
    Copy-Item -LiteralPath $receiptCandidates[0].LiteralPath -Destination $preservedRuntimeReceiptPath
    $runtimeReceipt = Get-Content -LiteralPath $preservedRuntimeReceiptPath -Raw | ConvertFrom-Json
    if ($null -ne $runtimeReceipt.checks) {
        $runtimeChecks = @($runtimeReceipt.checks.PSObject.Properties | Where-Object { -not [bool]$_.Value })
    }

    $expectedStatus = if ($ExpectShippingRejection) { "FAIL" } else { "PASS" }
    $validationChecks = [ordered]@{
        runner_completed = $true
        process_exited = $process.HasExited
        process_exit_code_zero = ($process.ExitCode -eq 0)
        artifact_schema_version_matches = ([int]$runtimeReceipt.artifact_schema_version -eq 2)
        run_tag_matches = ([string]$runtimeReceipt.run_tag -ceq $RunTag)
        expected_runtime_status = ([string]$runtimeReceipt.status -eq $expectedStatus)
        configuration_matches = ([string]$runtimeReceipt.configuration -eq $Configuration)
        scenario_matches = ([string]$runtimeReceipt.scenario -eq $Scenario)
        outcome_mode_matches = (
            [bool]$runtimeReceipt.outcome_telemetry_disabled -eq [bool]$DisableOutcomeTelemetry -and
            [string]$runtimeReceipt.outcome_mode -eq $(if ($DisableOutcomeTelemetry) {
                "neutral_missing_telemetry"
            } else { "live" })
        )
        seed_matches = ([string]$runtimeReceipt.run_seed -eq $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture))
        maximum_floor_matches = ([int]$runtimeReceipt.maximum_floor -eq $MaxFloor)
        cooked_runtime_confirmed = ([bool]$runtimeReceipt.requires_cooked_data)
        unattended_confirmed = ([bool]$runtimeReceipt.unattended)
    }

    if ($ExpectShippingRejection) {
        $validationChecks["shipping_override_rejected"] = ([string]$runtimeReceipt.reason -eq "SHIPPING_OVERRIDE_REJECTED")
        $validationChecks["no_floor_generated"] = (
            [int]$runtimeReceipt.completed_floor_count -eq 0 -and @($runtimeReceipt.floors).Count -eq 0
        )
        $validationChecks["exact_controls_absent"] = (-not [bool]$runtimeReceipt.exact_population_controls_compiled)
    }
    else {
        $floors = @($runtimeReceipt.floors)
        $requiredFloorFields = @(
            "floor_number", "generation_serial", "pcg_seed", "ecology_hash", "outcome_hash",
            "frozen_outcome", "intent_hash", "anchor_topology_hash", "population_hash",
            "resource_hash", "manifest_hash"
        )
        $missingFloorFields = @(
            foreach ($floor in $floors) {
                foreach ($field in $requiredFloorFields) {
                    if (!(Test-JsonProperty -Object $floor -Name $field)) {
                        "floor=$($floor.floor_number):$field"
                    }
                }
            }
        )
        $validationChecks["runtime_checks_all_pass"] = ($runtimeChecks.Count -eq 0)
        $validationChecks["all_floors_recorded"] = (
            [int]$runtimeReceipt.completed_floor_count -eq $MaxFloor -and $floors.Count -eq $MaxFloor
        )
        $validationChecks["required_floor_fields_present"] = ($missingFloorFields.Count -eq 0)
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
        $validationChecks["certified_sizes_only"] = (
            @($floors | Where-Object {
                [int]$_.size_x -lt 26 -or [int]$_.size_x -gt 30 -or
                [int]$_.size_y -ne [int]$_.size_x -or [int]$_.size_z -ne 1
            }).Count -eq 0
        )
        $validationChecks["hard_caps_respected"] = (
            @($floors | Where-Object {
                [int]$_.enemy_count -gt 25 -or [int]$_.food_count -gt 8 -or
                [int]$_.chest_count -gt 3 -or [int]$_.spawned_actor_count -gt 36
            }).Count -eq 0
        )
        $validationChecks["hashes_present"] = (
            @($floors | Where-Object {
                @($_.ecology_hash, $_.outcome_hash, $_.intent_hash, $_.anchor_topology_hash,
                    $_.population_hash, $_.resource_hash, $_.manifest_hash) |
                    Where-Object { [string]$_ -notmatch '^[A-Fa-f0-9]{64}$' }
            }).Count -eq 0
        )
        if ($DisableOutcomeTelemetry) {
            $validationChecks["missing_telemetry_is_neutral"] = (
                @($floors | Where-Object {
                    !(Test-JsonProperty -Object $_ -Name "frozen_outcome") -or
                    [bool]$_.frozen_outcome.is_valid -or
                    [double]$_.frozen_outcome.combat -ne 0.5 -or
                    [double]$_.frozen_outcome.survival -ne 0.5 -or
                    [double]$_.frozen_outcome.resources -ne 0.5 -or
                    [double]$_.frozen_outcome.pace -ne 0.5 -or
                    [int]$_.frozen_outcome.deaths -ne 0 -or
                    [int]$_.frozen_outcome.failures -ne 0
                }).Count -eq 0
            )
        }
        if ($Scenario -eq "Zero") {
            $validationChecks["zero_scenario_exact"] = (
                $floors.Count -eq 1 -and [int]$floors[0].enemy_count -eq 0 -and
                [int]$floors[0].food_count -eq 0 -and [int]$floors[0].chest_count -eq 0 -and
                [int]$floors[0].loot_count -eq 0 -and [int]$floors[0].special_event_count -eq 0 -and
                [int]$floors[0].spawned_actor_count -eq 0
            )
        }
        if ($Scenario -eq "EnemyCap25") {
            $validationChecks["enemy_cap_25_exact"] = (
                $floors.Count -eq 1 -and [int]$floors[0].enemy_count -eq 25 -and
                [int]$floors[0].food_count -eq 0 -and [int]$floors[0].chest_count -eq 0 -and
                [int]$floors[0].loot_count -eq 0 -and [int]$floors[0].special_event_count -eq 0 -and
                [int]$floors[0].spawned_actor_count -eq 25
            )
        }
        if ($Configuration -eq "Shipping") {
            $validationChecks["shipping_natural_and_no_exact_controls"] = (
                $Scenario -eq "Natural" -and -not [bool]$runtimeReceipt.exact_population_controls_compiled
            )
        }
    }

    $runtimeLogAvailable = Test-Path -LiteralPath $logFullPath -PathType Leaf
    if ($Configuration -eq "Development") {
        $validationChecks["development_runtime_log_produced"] = $runtimeLogAvailable
    }
    else {
        # Stock Shipping targets compile UE_LOG output out unless logging is
        # explicitly enabled. Do not alter the production target merely for
        # acceptance; the signed runtime receipt remains mandatory.
        $validationChecks["shipping_runtime_log_optional_by_build_policy"] = $true
    }
    if ($runtimeLogAvailable) {
        $logLines = @(Get-Content -LiteralPath $logFullPath)
        $blockedPatterns = @(
            "Object Transform",
            "GetAttributeFromPointIndex_0",
            "Blueprint Runtime Error",
            "Ensure condition failed",
            "Fatal error",
            "requested GenerateLocal more than once"
        )
        foreach ($line in $logLines) {
            foreach ($pattern in $blockedPatterns) {
                if ($line.IndexOf($pattern, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    $blockedLogFindings.Add([pscustomobject]@{
                        pattern = [string]$pattern
                        line = [string]$line
                    })
                }
            }
        }
        if (!$ExpectShippingRejection) {
            $stage = 0
            foreach ($line in $logLines) {
                if ($line -match 'LogEFProceduralPCGRuntime: PCGComplete world=') {
                    if ($stage -ne 0) { $telemetry.order_valid = $false }
                    $stage = 1
                    $telemetry.available = $true
                }
                elseif ($line -match 'LogEFProceduralPCGRuntime: NavigationPathReady world=') {
                    if ($stage -ne 1) { $telemetry.order_valid = $false }
                    $stage = 2
                }
                elseif ($line -match 'LogEFCalystoPopulation: PopulationRealized floor=') {
                    if ($stage -ne 2) { $telemetry.order_valid = $false }
                    $stage = 3
                }
                elseif ($line -match 'LogEFProceduralPCGRuntime: DoorEnabled world=') {
                    if ($stage -ne 3) { $telemetry.order_valid = $false }
                    if ($stage -eq 3) { $telemetry.complete_sequences++ }
                    $stage = 0
                }
                if ($line.IndexOf(
                        "outcome for the unattended Director V3 neutral-telemetry fixture",
                        [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    ++$neutralSuppressionCount
                }
            }
            if ($stage -ne 0) { $telemetry.order_valid = $false }
        }
    }
    $validationChecks["blocked_log_findings_empty_when_available"] = ($blockedLogFindings.Count -eq 0)
    if (!$ExpectShippingRejection) {
        if ($runtimeLogAvailable) {
            $validationChecks["telemetry_order_and_count"] = (
                [bool]$telemetry.available -and [bool]$telemetry.order_valid -and
                [int]$telemetry.complete_sequences -eq $MaxFloor
            )
        }
        elseif ($Configuration -eq "Shipping") {
            # FloorReady is emitted only after the native PCG/Nav/Manifest/
            # Population conjunction enables the sole ACF door. Development
            # smokes retain the explicit ordered telemetry scan.
            $validationChecks["shipping_ready_contract_without_runtime_log"] = (
                [int]$runtimeReceipt.completed_floor_count -eq $MaxFloor -and
                [int]$runtimeReceipt.door_interaction_count -eq [Math]::Max(0, $MaxFloor - 1) -and
                -not [bool]$runtimeReceipt.exact_population_controls_compiled
            )
        }
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
			(Test-JsonProperty -Object $runtimeReceipt -Name "screenshot_floor") -and
			[int]$runtimeReceipt.screenshot_floor -eq $MaxFloor
		)
		$validationChecks["visual_capture_excludes_ui_overlay"] = (
			(Test-JsonProperty -Object $runtimeReceipt -Name "visual_capture_mode") -and
			(Test-JsonProperty -Object $runtimeReceipt -Name "screenshot_show_ui") -and
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
        try {
            $runtimeReceipt = Get-Content -LiteralPath $preservedRuntimeReceiptPath -Raw | ConvertFrom-Json
        }
        catch {
            $runnerError += "; preserved runtime receipt could not be parsed: $($_.Exception.Message)"
        }
    }
}

$failedChecks = @(
    $validationChecks.GetEnumerator() |
        Where-Object { -not [bool]$_.Value } |
        ForEach-Object { $_.Key }
)
if (![string]::IsNullOrWhiteSpace($runnerError) -and $failedChecks -notcontains "runner_exception") {
    $failedChecks += "runner_exception"
}
$status = if ($failedChecks.Count -eq 0) { "PASS" } else { "FAIL" }
$result = [ordered]@{
    schema_version = 3
    artifact_schema_version = 2
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = $status
    semantic = if ($ExpectShippingRejection) { "expected_shipping_override_rejection" } else { "packaged_runtime_acceptance" }
    run_tag = $RunTag
    bundle_root = $bundleFullPath
    configuration = $Configuration
    scenario = $Scenario
    outcome_mode = if ($DisableOutcomeTelemetry) { "neutral_missing_telemetry" } else { "live" }
    outcome_telemetry_disabled = [bool]$DisableOutcomeTelemetry
    archive_root = $archivePath
    executable = $exePath
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $exePath).Hash
    process_exit_code = if ($null -ne $process -and $process.HasExited) { $process.ExitCode } else { $null }
    runtime_receipt_source = $runtimeReceiptSourcePath
    runtime_receipt = if (Test-Path -LiteralPath $preservedRuntimeReceiptPath -PathType Leaf) {
        $preservedRuntimeReceiptPath
    } else { "" }
    runtime_receipt_sha256 = if (Test-Path -LiteralPath $preservedRuntimeReceiptPath -PathType Leaf) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $preservedRuntimeReceiptPath).Hash
    } else { "" }
    copied_screenshot = if (![string]::IsNullOrWhiteSpace($copiedScreenshotPath) -and
        (Test-Path -LiteralPath $copiedScreenshotPath -PathType Leaf)) {
        $copiedScreenshotPath
    } else { "" }
    copied_screenshot_sha256 = if (![string]::IsNullOrWhiteSpace($copiedScreenshotPath) -and
        (Test-Path -LiteralPath $copiedScreenshotPath -PathType Leaf)) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $copiedScreenshotPath).Hash
    } else { "" }
    log = $logFullPath
    runtime_log_available = [bool](Test-Path -LiteralPath $logFullPath -PathType Leaf)
    runtime_log_policy = if ($Configuration -eq "Shipping") {
        "OptionalWhenCompiledOut"
    } else { "Required" }
    log_sha256 = if (Test-Path -LiteralPath $logFullPath -PathType Leaf) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $logFullPath).Hash
    } else { "" }
    runtime = $runtimeReceipt
    telemetry = $telemetry
    neutral_suppression_count = $neutralSuppressionCount
    blocked_log_findings = $blockedLogFindings
    runner_error = $runnerError
    checks = $validationChecks
    failed_checks = $failedChecks
}
$resultJson = ConvertTo-Json -InputObject $result -Depth 14
[IO.File]::WriteAllText(
    $outputFullPath,
    $resultJson + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
$resultJson
if ($status -ne "PASS") {
    $runnerErrorSuffix = if ($runnerError) { "; $runnerError" } else { "" }
    throw "Packaged V3 smoke failed: $($failedChecks -join ', ')$runnerErrorSuffix"
}
