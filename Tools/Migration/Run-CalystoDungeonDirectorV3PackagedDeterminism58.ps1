[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [Int64]$RunSeed = 202608140058,
    [ValidateRange(1, 100)]
    [int]$MaxFloor = 10,
    [ValidateRange(60, 1800)]
    [int]$TimeoutSeconds = 420,
    [string]$PairTag = "",
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$OutputPath = ""
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

function Test-Sha256 {
    param([object]$Value)
    return [string]$Value -cmatch '^[A-Fa-f0-9]{64}$'
}

if ($RunSeed -le 0) { throw "RunSeed must be positive." }
if ([string]::IsNullOrWhiteSpace($PairTag)) {
    $PairTag = "{0}_p{1}_{2}" -f `
        [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssfffZ", [Globalization.CultureInfo]::InvariantCulture), `
        $PID, `
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
}
if ($PairTag -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,89}$') {
    throw "PairTag must match ^[A-Za-z0-9][A-Za-z0-9_-]{0,89}$ exactly."
}

$projectFullPath = [IO.Path]::GetFullPath($ProjectRoot)
$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$runnerPath = Join-Path $projectFullPath `
    "Tools\Migration\Run-CalystoDungeonDirectorV3PackagedSmoke58.ps1"
if (!(Test-Path -LiteralPath $runnerPath -PathType Leaf)) {
    throw "Packaged smoke runner is missing: $runnerPath"
}

$pairBundlePath = Join-Path $projectFullPath `
    "Saved\Migration\CalystoDungeonDirectorV3\PackagedDeterminismPairs\$PairTag"
$leftTag = "${PairTag}_A"
$rightTag = "${PairTag}_B"
$packagedRunsRoot = Join-Path $projectFullPath `
    "Saved\Migration\CalystoDungeonDirectorV3\PackagedRuns"
$leftBundlePath = Join-Path $packagedRunsRoot $leftTag
$rightBundlePath = Join-Path $packagedRunsRoot $rightTag
if (Test-Path -LiteralPath $pairBundlePath) {
    throw "PairTag evidence already exists; refusing to overwrite: $pairBundlePath"
}
foreach ($childBundle in @($leftBundlePath, $rightBundlePath)) {
    if (Test-Path -LiteralPath $childBundle) {
        throw "Child RunTag evidence already exists; refusing to overwrite: $childBundle"
    }
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $pairBundlePath "ComparisonReceipt.json"
}
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $outputFullPath) {
    throw "Comparison receipt already exists; refusing to overwrite: $outputFullPath"
}
New-Item -ItemType Directory -Path $pairBundlePath | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $outputFullPath) -Force | Out-Null

function Invoke-DeterminismRun {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Tag,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedBundle
    )

    $invocationSucceeded = $false
    $invocationError = ""
    try {
        & $runnerPath `
            -Configuration Development `
            -ArchiveRoot $archivePath `
            -Scenario Natural `
            -RunSeed $RunSeed `
            -MaxFloor $MaxFloor `
            -TimeoutSeconds $TimeoutSeconds `
            -DisableOutcomeTelemetry `
            -RunTag $Tag `
            -ProjectRoot $projectFullPath | Out-Null
        $invocationSucceeded = $true
    }
    catch {
        $invocationError = $_.Exception.Message
    }

    $runnerReceiptPath = Join-Path $ExpectedBundle "RunnerReceipt.json"
    $runnerReceipt = $null
    $runnerReceiptError = ""
    if (Test-Path -LiteralPath $runnerReceiptPath -PathType Leaf) {
        try {
            $runnerReceipt = Get-Content -LiteralPath $runnerReceiptPath -Raw | ConvertFrom-Json
        }
        catch {
            $runnerReceiptError = $_.Exception.Message
        }
    }
    else {
        $runnerReceiptError = "RUNNER_RECEIPT_MISSING"
    }

    $runtimeReceiptPath = if ($null -ne $runnerReceipt) {
        [string]$runnerReceipt.runtime_receipt
    }
    else { "" }
    $runtimeReceipt = $null
    $runtimeReceiptError = ""
    if (![string]::IsNullOrWhiteSpace($runtimeReceiptPath) -and
        (Test-Path -LiteralPath $runtimeReceiptPath -PathType Leaf)) {
        try {
            $runtimeReceipt = Get-Content -LiteralPath $runtimeReceiptPath -Raw | ConvertFrom-Json
        }
        catch {
            $runtimeReceiptError = $_.Exception.Message
        }
    }
    else {
        $runtimeReceiptError = "RUNTIME_RECEIPT_MISSING"
    }

    return [pscustomobject]@{
        tag = $Tag
        bundle = $ExpectedBundle
        invocation_succeeded = $invocationSucceeded
        invocation_error = $invocationError
        runner_receipt_path = $runnerReceiptPath
        runner_receipt_sha256 = if (Test-Path -LiteralPath $runnerReceiptPath -PathType Leaf) {
            (Get-FileHash -Algorithm SHA256 -LiteralPath $runnerReceiptPath).Hash
        } else { "" }
        runner_receipt_error = $runnerReceiptError
        runner_receipt = $runnerReceipt
        runtime_receipt_path = $runtimeReceiptPath
        runtime_receipt_sha256 = if (![string]::IsNullOrWhiteSpace($runtimeReceiptPath) -and
            (Test-Path -LiteralPath $runtimeReceiptPath -PathType Leaf)) {
            (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeReceiptPath).Hash
        } else { "" }
        runtime_receipt_error = $runtimeReceiptError
        runtime_receipt = $runtimeReceipt
    }
}

# Always attempt B even when A fails so the pair receipt preserves both sides.
$left = Invoke-DeterminismRun -Tag $leftTag -ExpectedBundle $leftBundlePath
$right = Invoke-DeterminismRun -Tag $rightTag -ExpectedBundle $rightBundlePath
$leftRunner = $left.runner_receipt
$rightRunner = $right.runner_receipt
$leftRuntime = $left.runtime_receipt
$rightRuntime = $right.runtime_receipt

$checks = [ordered]@{
    left_runner_receipt_present = ($null -ne $leftRunner)
    right_runner_receipt_present = ($null -ne $rightRunner)
    left_runtime_receipt_present = ($null -ne $leftRuntime)
    right_runtime_receipt_present = ($null -ne $rightRuntime)
    left_runner_pass = ($null -ne $leftRunner -and [string]$leftRunner.status -eq "PASS")
    right_runner_pass = ($null -ne $rightRunner -and [string]$rightRunner.status -eq "PASS")
}

$floorComparisons = @()
$mismatches = [Collections.Generic.List[object]]::new()
$requiredFields = @(
    "floor_number", "generation_serial", "pcg_seed", "ecology_hash", "outcome_hash",
    "frozen_outcome", "intent_hash", "anchor_topology_hash", "population_hash",
    "resource_hash", "manifest_hash"
)
$hashFields = @(
    "ecology_hash", "outcome_hash", "intent_hash", "anchor_topology_hash",
    "population_hash", "resource_hash", "manifest_hash"
)
$comparisonFields = @(
    "pcg_seed", "ecology_hash", "outcome_hash", "intent_hash", "manifest_hash",
    "anchor_topology_hash", "population_hash", "resource_hash"
)

if ($null -ne $leftRuntime -and $null -ne $rightRuntime) {
    $checks["artifact_schema_versions_match"] = (
        [int]$leftRuntime.artifact_schema_version -eq 2 -and
        [int]$rightRuntime.artifact_schema_version -eq 2
    )
    $checks["run_tags_match_sides"] = (
        [string]$leftRuntime.run_tag -ceq $leftTag -and
        [string]$rightRuntime.run_tag -ceq $rightTag
    )
    $checks["development_natural_neutral"] = (
        [string]$leftRuntime.configuration -eq "Development" -and
        [string]$rightRuntime.configuration -eq "Development" -and
        [string]$leftRuntime.scenario -eq "Natural" -and
        [string]$rightRuntime.scenario -eq "Natural" -and
        [bool]$leftRuntime.unattended -and [bool]$rightRuntime.unattended -and
        [bool]$leftRuntime.outcome_telemetry_disabled -and
        [bool]$rightRuntime.outcome_telemetry_disabled -and
        [string]$leftRuntime.outcome_mode -eq "neutral_missing_telemetry" -and
        [string]$rightRuntime.outcome_mode -eq "neutral_missing_telemetry"
    )
    $checks["same_seed_and_maximum_floor"] = (
        [string]$leftRuntime.run_seed -eq $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture) -and
        [string]$rightRuntime.run_seed -eq [string]$leftRuntime.run_seed -and
        [int]$leftRuntime.maximum_floor -eq $MaxFloor -and
        [int]$rightRuntime.maximum_floor -eq $MaxFloor
    )
    $checks["same_executable"] = (
        $null -ne $leftRunner -and $null -ne $rightRunner -and
        (Test-Sha256 $leftRunner.executable_sha256) -and
        [string]$leftRunner.executable_sha256 -ceq [string]$rightRunner.executable_sha256
    )
    $checks["same_policy_hash"] = (
        (Test-Sha256 $leftRuntime.policy_hash) -and
        [string]$leftRuntime.policy_hash -ceq [string]$rightRuntime.policy_hash
    )

    $leftFloors = @($leftRuntime.floors)
    $rightFloors = @($rightRuntime.floors)
    $leftByFloor = @{}
    $rightByFloor = @{}
    $duplicateFloorIdentity = $false
    $missingRequiredFields = [Collections.Generic.List[string]]::new()
    $invalidHashes = [Collections.Generic.List[string]]::new()
    $neutralOutcomeDrift = [Collections.Generic.List[string]]::new()

    foreach ($side in @(
            [pscustomobject]@{ name = "A"; floors = $leftFloors; map = $leftByFloor },
            [pscustomobject]@{ name = "B"; floors = $rightFloors; map = $rightByFloor })) {
        foreach ($floor in $side.floors) {
            $floorNumber = [int64]$floor.floor_number
            if ($side.map.ContainsKey($floorNumber)) {
                $duplicateFloorIdentity = $true
            }
            else {
                $side.map[$floorNumber] = $floor
            }
            foreach ($field in $requiredFields) {
                if (!(Test-JsonProperty -Object $floor -Name $field)) {
                    $missingRequiredFields.Add("$($side.name):floor=${floorNumber}:$field")
                }
            }
            foreach ($field in $hashFields) {
                if (!(Test-Sha256 $floor.$field)) {
                    $invalidHashes.Add("$($side.name):floor=${floorNumber}:$field")
                }
            }
            if (!(Test-JsonProperty -Object $floor -Name "frozen_outcome") -or
                [bool]$floor.frozen_outcome.is_valid -or
                [double]$floor.frozen_outcome.combat -ne 0.5 -or
                [double]$floor.frozen_outcome.survival -ne 0.5 -or
                [double]$floor.frozen_outcome.resources -ne 0.5 -or
                [double]$floor.frozen_outcome.pace -ne 0.5 -or
                [int]$floor.frozen_outcome.deaths -ne 0 -or
                [int]$floor.frozen_outcome.failures -ne 0) {
                $neutralOutcomeDrift.Add("$($side.name):floor=$floorNumber")
            }
        }
    }

    $checks["floor_counts_complete"] = (
        $leftFloors.Count -eq $MaxFloor -and $rightFloors.Count -eq $MaxFloor -and
        [int]$leftRuntime.completed_floor_count -eq $MaxFloor -and
        [int]$rightRuntime.completed_floor_count -eq $MaxFloor
    )
    $checks["floor_identity_unique"] = (-not $duplicateFloorIdentity)
    $checks["required_floor_fields_present"] = ($missingRequiredFields.Count -eq 0)
    $checks["all_floor_hashes_valid"] = ($invalidHashes.Count -eq 0)
    $checks["all_frozen_outcomes_neutral"] = ($neutralOutcomeDrift.Count -eq 0)

    foreach ($floorNumber in 1..$MaxFloor) {
        if (!$leftByFloor.ContainsKey([int64]$floorNumber) -or
            !$rightByFloor.ContainsKey([int64]$floorNumber)) {
            $mismatches.Add([pscustomobject]@{
                floor = $floorNumber
                field = "floor_presence"
                left = $leftByFloor.ContainsKey([int64]$floorNumber)
                right = $rightByFloor.ContainsKey([int64]$floorNumber)
            })
            continue
        }
        $leftFloor = $leftByFloor[[int64]$floorNumber]
        $rightFloor = $rightByFloor[[int64]$floorNumber]
        $fieldMatches = [ordered]@{}
        if ([int64]$leftFloor.generation_serial -ne $floorNumber -or
            [int64]$rightFloor.generation_serial -ne $floorNumber) {
            $mismatches.Add([pscustomobject]@{
                floor = $floorNumber
                field = "generation_serial"
                left = [string]$leftFloor.generation_serial
                right = [string]$rightFloor.generation_serial
            })
        }
        foreach ($field in $comparisonFields) {
            $leftValue = [string]$leftFloor.$field
            $rightValue = [string]$rightFloor.$field
            $matches = $leftValue -ceq $rightValue
            $fieldMatches[$field] = $matches
            if (!$matches) {
                $mismatches.Add([pscustomobject]@{
                    floor = $floorNumber
                    field = $field
                    left = $leftValue
                    right = $rightValue
                })
            }
        }
        $floorComparisons += [pscustomobject]@{
            floor = $floorNumber
            generation_serial = [string]$leftFloor.generation_serial
            matches = $fieldMatches
        }
    }
    $checks["all_determinism_fields_match"] = ($mismatches.Count -eq 0)
}
else {
    foreach ($name in @(
            "artifact_schema_versions_match", "run_tags_match_sides", "development_natural_neutral",
            "same_seed_and_maximum_floor", "same_executable", "same_policy_hash",
            "floor_counts_complete", "floor_identity_unique", "required_floor_fields_present",
            "all_floor_hashes_valid", "all_frozen_outcomes_neutral", "all_determinism_fields_match")) {
        $checks[$name] = $false
    }
}

$failedChecks = @(
    $checks.GetEnumerator() |
        Where-Object { -not [bool]$_.Value } |
        ForEach-Object { $_.Key }
)
$status = if ($failedChecks.Count -eq 0) { "PASS" } else { "FAIL" }

function ConvertTo-RunEvidence {
    param([Parameter(Mandatory = $true)][object]$Run)
    $runnerReceipt = $Run.runner_receipt
    return [ordered]@{
        tag = $Run.tag
        bundle = $Run.bundle
        invocation_succeeded = $Run.invocation_succeeded
        invocation_error = $Run.invocation_error
        runner_receipt = $Run.runner_receipt_path
        runner_receipt_sha256 = $Run.runner_receipt_sha256
        runner_receipt_error = $Run.runner_receipt_error
        runner_status = if ($null -ne $runnerReceipt) { [string]$runnerReceipt.status } else { "" }
        runtime_receipt = $Run.runtime_receipt_path
        runtime_receipt_sha256 = $Run.runtime_receipt_sha256
        runtime_receipt_error = $Run.runtime_receipt_error
        log = if ($null -ne $runnerReceipt) { [string]$runnerReceipt.log } else { "" }
        log_sha256 = if ($null -ne $runnerReceipt) { [string]$runnerReceipt.log_sha256 } else { "" }
        executable_sha256 = if ($null -ne $runnerReceipt) { [string]$runnerReceipt.executable_sha256 } else { "" }
    }
}

$result = [ordered]@{
    schema_version = 3
    artifact_schema_version = 2
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = $status
    semantic = "packaged_same_seed_neutral_ab_determinism"
    pair_tag = $PairTag
    configuration = "Development"
    scenario = "Natural"
    outcome_mode = "neutral_missing_telemetry"
    run_seed = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    maximum_floor = $MaxFloor
    archive_root = $archivePath
    left = ConvertTo-RunEvidence $left
    right = ConvertTo-RunEvidence $right
    compared_fields = $comparisonFields
    floor_comparisons = $floorComparisons
    mismatches = $mismatches
    checks = $checks
    failed_checks = $failedChecks
}
$resultJson = ConvertTo-Json -InputObject $result -Depth 14
[IO.File]::WriteAllText(
    $outputFullPath,
    $resultJson + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
$resultJson
if ($status -ne "PASS") {
    throw "Packaged V3 same-seed neutral A/B comparison failed: $($failedChecks -join ', ')"
}
