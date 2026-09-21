[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration = 'Development',
    [ValidateSet('PreCutover', 'FinalStrict')]
    [string]$ValidationMode = 'FinalStrict',
    [ValidateRange(5, 60)]
    [int]$ObservationSeconds = 10,
    [string]$ResumeArchiveRoot = '',
    [string]$ResumeCookLog = ''
)

# Packages one configuration into a wrapper-owned, temporary archive. It preserves
# the archive whenever a gate fails and removes only that large temporary archive
# after every gate passes. Logs, reports, screenshots and their hashes remain as
# durable evidence regardless of outcome.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Test-DirectChildOf {
    param(
        [Parameter(Mandatory = $true)][string]$ChildPath,
        [Parameter(Mandatory = $true)][string]$ExpectedParentPath
    )

    $normalizedChild = [IO.Path]::GetFullPath($ChildPath).TrimEnd('\')
    $normalizedParent = [IO.Path]::GetFullPath($ExpectedParentPath).TrimEnd('\')
    if ([string]::IsNullOrWhiteSpace($normalizedChild) -or
        [string]::IsNullOrWhiteSpace($normalizedParent) -or
        [string]::Equals($normalizedChild, $normalizedParent, [StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    $actualParent = [IO.Path]::GetFullPath(
        [IO.Path]::GetDirectoryName($normalizedChild)
    ).TrimEnd('\')
    return [string]::Equals($actualParent, $normalizedParent, [StringComparison]::OrdinalIgnoreCase)
}

function Remove-ManagedPendingValidationArchive {
    param(
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$PendingValidationRoot
    )

    if (-not (Test-DirectChildOf -ChildPath $ArchivePath -ExpectedParentPath $PendingValidationRoot)) {
        throw "Refusing to remove an archive outside the wrapper-owned pending-validation root: $ArchivePath"
    }
    if (-not (Test-Path -LiteralPath $ArchivePath -PathType Container)) {
        throw "Wrapper-owned archive is missing before cleanup: $ArchivePath"
    }

    $rootItem = Get-Item -Force -LiteralPath $ArchivePath
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing to remove reparse-point archive root: $ArchivePath"
    }
    $reparsePoints = @(Get-ChildItem -Force -LiteralPath $ArchivePath -Recurse -ErrorAction Stop |
        Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 })
    if ($reparsePoints.Count -ne 0) {
        throw "Refusing to remove archive containing reparse points: $($reparsePoints.FullName -join '; ')"
    }

    Remove-Item -LiteralPath $ArchivePath -Recurse -Force -ErrorAction Stop
}

function Get-OptionalSha256 {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
    }
    return ''
}

function Invoke-LoggedPowerShell {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    # Native stderr is surfaced as ErrorRecord objects by Windows PowerShell.
    # With the orchestrator's fail-closed ErrorActionPreference those records
    # can terminate the pipeline before LASTEXITCODE and the final receipt are
    # captured. Stream them into the owned log under Continue, then enforce the
    # concrete child exit code at the call site.
    $previousErrorActionPreference = $ErrorActionPreference
    $childExitCode = 1
    try {
        $ErrorActionPreference = 'Continue'
        & powershell.exe @Arguments 2>&1 |
            Tee-Object -FilePath $LogPath |
            Out-Host
        $childExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    return [int]$childExitCode
}

$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$projectPath = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$packageScript = Join-Path $ProjectRoot 'Tools\Migration\Package-NoShellForWinter58.ps1'
$v6PackageValidator = Join-Path $ProjectRoot 'Tools\Migration\Validate-CalystoDungeonDirectorV6Package58.ps1'
$v6PackagedSmokeRunner = Join-Path $ProjectRoot `
    'Tools\Migration\Run-CalystoDungeonDirectorV6PackagedSmoke58.ps1'
$v6PackagedDeterminismRunner = Join-Path $ProjectRoot `
    'Tools\Migration\Run-CalystoDungeonDirectorV6PackagedDeterminism58.ps1'
$clothingStartupSmoke = Join-Path $ProjectRoot `
    'Tools\ClothingMorphV4\Test-EFClothingMorphV4PackagedStartup58.ps1'
foreach ($requiredPath in @(
        $projectPath, $packageScript, $v6PackageValidator, $clothingStartupSmoke)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required package-validation path not found: $requiredPath"
    }
}

$pendingValidationRoot = [IO.Path]::GetFullPath(
    (Join-Path $ProjectRoot 'Saved\Artifacts\PendingValidation')
).TrimEnd('\')
$evidenceRoot = Join-Path $ProjectRoot 'Docs\Migration\Evidence'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$hasResumeArchive = -not [string]::IsNullOrWhiteSpace($ResumeArchiveRoot)
$hasResumeCookLog = -not [string]::IsNullOrWhiteSpace($ResumeCookLog)
if ($hasResumeArchive -ne $hasResumeCookLog) {
    throw 'ResumeArchiveRoot and ResumeCookLog must be supplied together.'
}
$resumeMode = $hasResumeArchive -and $hasResumeCookLog
$sidecarSuffix = if ($resumeMode) { "${Configuration}_${stamp}_Resume" } else { "${Configuration}_$stamp" }
$sidecarPrefix = Join-Path $pendingValidationRoot $sidecarSuffix
if ($resumeMode) {
    $archiveRoot = (Resolve-Path -LiteralPath $ResumeArchiveRoot).Path
    $cookLog = (Resolve-Path -LiteralPath $ResumeCookLog).Path
}
else {
    $archiveRoot = Join-Path $pendingValidationRoot "${Configuration}_$stamp"
    # Package-NoShellForWinter58 owns the immutable UAT log beside its archive.
    # Keep this wrapper's process transcript separate so two Tee-Object writers
    # never truncate or contend for the same evidence file.
    $cookLog = "$archiveRoot.Package.log"
}
$packageOrchestrationLog = "$sidecarPrefix.PackageOrchestrator.log"
$packageValidationPath = "$sidecarPrefix.V6PackageValidation.json"
$packageValidationLog = "$sidecarPrefix.V6PackageValidation.log"
$packagingContractCopy = "$sidecarPrefix.CalystoV6PackagingContract.json"
$startupSmokeLog = "$sidecarPrefix.ClothingV4PackagedStartup.log"
$startupSummaryPath = Join-Path $ProjectRoot `
    "Saved\ClothingMorphV4QA\PackagedStartup_$stamp\Summary.json"
$receiptPath = Join-Path $evidenceRoot `
    "NoShellForWinterPackageValidation_${Configuration}_$stamp.json"
$calystoSmokeCases = if ($Configuration -eq 'Development') {
    @(
        [pscustomobject]@{ Name = 'Natural'; Scenario = 'Natural'; MaxFloor = 10; Capture = $true; Reject = $false },
        [pscustomobject]@{ Name = 'Zero'; Scenario = 'Zero'; MaxFloor = 1; Capture = $false; Reject = $false },
        [pscustomobject]@{ Name = 'EnemyCap25'; Scenario = 'EnemyCap25'; MaxFloor = 1; Capture = $false; Reject = $false },
        [pscustomobject]@{ Name = 'ResourceMin'; Scenario = 'ResourceMin'; MaxFloor = 1; Capture = $false; Reject = $false },
        [pscustomobject]@{ Name = 'ResourceMax'; Scenario = 'ResourceMax'; MaxFloor = 1; Capture = $false; Reject = $false },
        [pscustomobject]@{ Name = 'NPCTotal4'; Scenario = 'NPCTotal4'; MaxFloor = 1; Capture = $false; Reject = $false },
        [pscustomobject]@{ Name = 'SpecialEvents6'; Scenario = 'SpecialEvents6'; MaxFloor = 1; Capture = $false; Reject = $false }
    )
}
else {
    @(
        [pscustomobject]@{ Name = 'Natural'; Scenario = 'Natural'; MaxFloor = 10; Capture = $true; Reject = $false },
        [pscustomobject]@{ Name = 'OverrideRejected'; Scenario = 'EnemyCap25'; MaxFloor = 1; Capture = $false; Reject = $true }
    )
}

if (-not (Test-DirectChildOf -ChildPath $archiveRoot -ExpectedParentPath $pendingValidationRoot)) {
    throw "Wrapper generated an archive outside its pending-validation root: $archiveRoot"
}
$newPaths = @(
    $packageValidationPath, $packageValidationLog, $packagingContractCopy,
    $startupSmokeLog, $receiptPath
)
if (-not $resumeMode) {
    $newPaths += @($archiveRoot, $cookLog, $packageOrchestrationLog)
}
foreach ($newPath in $newPaths) {
    if (Test-Path -LiteralPath $newPath) {
        throw "Refusing to overwrite package-validation artifact: $newPath"
    }
}
[void][IO.Directory]::CreateDirectory($pendingValidationRoot)
[void][IO.Directory]::CreateDirectory($evidenceRoot)

$packageExitCode = $null
$packageValidationExitCode = $null
$startupSmokeExitCode = $null
$packagePassed = $false
$packageValidationPassed = $false
$startupSmokePassed = $false
$packageValidationStatus = 'NOT_RUN'
$startupSmokeStatus = 'NOT_RUN'
$calystoSmokePassed = $false
$calystoSmokeStatus = 'NOT_RUN'
$calystoSmokeResults = [Collections.Generic.List[object]]::new()
$calystoDeterminismPassed = $false
$calystoDeterminismStatus = 'NOT_RUN'
$calystoDeterminismResult = $null
$v6RuntimeRunnersAvailable =
    (Test-Path -LiteralPath $v6PackagedSmokeRunner -PathType Leaf) -and
    (Test-Path -LiteralPath $v6PackagedDeterminismRunner -PathType Leaf)
$orchestratorError = ''
$cleanupStatus = 'NOT_RUN'
$cleanupError = ''

try {
    if ($resumeMode) {
        $packageExitCode = 0
        $packagePassed =
            (Test-Path -LiteralPath $archiveRoot -PathType Container) -and
            (Test-Path -LiteralPath $cookLog -PathType Leaf)
    }
    else {
        $packageExitCode = Invoke-LoggedPowerShell -LogPath $packageOrchestrationLog -Arguments @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $packageScript,
            '-ProjectRoot', $ProjectRoot,
            '-EngineRoot', $EngineRoot,
            '-Configuration', $Configuration,
            '-ArchiveRoot', $archiveRoot
        )
    }
    $packagePassed = ($packageExitCode -eq 0) -and
        (Test-Path -LiteralPath $archiveRoot -PathType Container) -and
        (Test-Path -LiteralPath $cookLog -PathType Leaf)
    if (-not $packagePassed) {
        throw "Cook/package gate failed with exit code $packageExitCode. Archive retained: $archiveRoot"
    }
    $packagingContractInArchive = Join-Path $archiveRoot 'CalystoV6PackagingContract.json'
    if (-not (Test-Path -LiteralPath $packagingContractInArchive -PathType Leaf)) {
        throw "Package did not produce its V6 contract: $packagingContractInArchive"
    }
    Copy-Item -LiteralPath $packagingContractInArchive -Destination $packagingContractCopy -ErrorAction Stop
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $packagingContractInArchive).Hash -cne
        (Get-FileHash -Algorithm SHA256 -LiteralPath $packagingContractCopy).Hash) {
        throw 'Durable V6 packaging-contract copy does not match the archive original.'
    }

    $packageValidationExitCode = Invoke-LoggedPowerShell `
        -LogPath $packageValidationLog `
        -Arguments @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $v6PackageValidator,
            '-ArchiveRoot', $archiveRoot,
            '-CookLog', $cookLog,
            '-Configuration', $Configuration,
            '-ValidationMode', $ValidationMode,
            '-ProjectRoot', $ProjectRoot,
            '-EngineRoot', $EngineRoot,
            '-OutputPath', $packageValidationPath
        )
    if (Test-Path -LiteralPath $packageValidationPath -PathType Leaf) {
        $packageValidationStatus = [string](
            (Get-Content -Raw -LiteralPath $packageValidationPath | ConvertFrom-Json).status
        )
    }
    $packageValidationPassed = ($packageValidationExitCode -eq 0) -and
        ($packageValidationStatus -ceq 'PASS')
    if (-not $packageValidationPassed) {
        throw "Calysto V6 package validation gate failed with exit code $packageValidationExitCode and status $packageValidationStatus. Archive retained: $archiveRoot"
    }

    if ($v6RuntimeRunnersAvailable) {
        $smokeCaseIndex = 0
        foreach ($smokeCase in $calystoSmokeCases) {
            ++$smokeCaseIndex
            $runTag = "gate_${Configuration}_$($smokeCase.Name)_$stamp"
            $runnerLog = "$sidecarPrefix.V6Smoke.$($smokeCase.Name).log"
            $runnerReceipt = Join-Path $ProjectRoot `
                "Saved\Migration\CalystoDungeonDirectorV6\PackagedRuns\$runTag\RunnerReceipt.json"
            $runnerArguments = @(
                '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $v6PackagedSmokeRunner,
                '-Configuration', $Configuration,
                '-ArchiveRoot', $archiveRoot,
                '-ValidationMode', $ValidationMode,
                '-Scenario', [string]$smokeCase.Scenario,
                '-RunSeed', [string](202609040006L + $smokeCaseIndex),
                '-MaxFloor', [string]$smokeCase.MaxFloor,
                '-TimeoutSeconds', '600',
                '-RunTag', $runTag,
                '-ProjectRoot', $ProjectRoot
            )
            if ([bool]$smokeCase.Capture) { $runnerArguments += '-CaptureVisual' }
            if ([bool]$smokeCase.Reject) { $runnerArguments += '-ExpectShippingRejection' }

            $runnerExitCode = Invoke-LoggedPowerShell `
                -Arguments $runnerArguments `
                -LogPath $runnerLog
            $runnerStatus = if (Test-Path -LiteralPath $runnerReceipt -PathType Leaf) {
                [string]((Get-Content -Raw -LiteralPath $runnerReceipt | ConvertFrom-Json).status)
            }
            else { 'MISSING' }
            $calystoSmokeResults.Add([pscustomobject]@{
                name = [string]$smokeCase.Name
                scenario = [string]$smokeCase.Scenario
                run_tag = $runTag
                exit_code = $runnerExitCode
                status = $runnerStatus
                receipt = $runnerReceipt
                receipt_sha256 = Get-OptionalSha256 -Path $runnerReceipt
                orchestration_log = $runnerLog
            })
            if ($runnerExitCode -ne 0 -or $runnerStatus -cne 'PASS') {
                throw "Calysto V6 packaged smoke '$($smokeCase.Name)' failed with exit code $runnerExitCode and status $runnerStatus. Archive retained: $archiveRoot"
            }
        }
        $calystoSmokePassed = ($calystoSmokeResults.Count -eq $calystoSmokeCases.Count)
        $calystoSmokeStatus = if ($calystoSmokePassed) { 'PASS' } else { 'FAIL' }

        $determinismPairTag = "gate_${Configuration}_Determinism_$stamp"
        $determinismLog = "$sidecarPrefix.V6Determinism.log"
        $determinismReceipt = Join-Path $ProjectRoot `
            "Saved\Migration\CalystoDungeonDirectorV6\PackagedDeterminismPairs\$determinismPairTag\ComparisonReceipt.json"
        $determinismExitCode = Invoke-LoggedPowerShell `
            -LogPath $determinismLog `
            -Arguments @(
                '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', $v6PackagedDeterminismRunner,
                '-Configuration', $Configuration,
                '-ArchiveRoot', $archiveRoot,
                '-ValidationMode', $ValidationMode,
                '-RunSeed', '202609040006',
                '-MaxFloor', '10',
                '-TimeoutSeconds', '600',
                '-PairTag', $determinismPairTag,
                '-ProjectRoot', $ProjectRoot
            )
        $determinismStatus = if (Test-Path -LiteralPath $determinismReceipt -PathType Leaf) {
            [string]((Get-Content -Raw -LiteralPath $determinismReceipt | ConvertFrom-Json).status)
        }
        else { 'MISSING' }
        $calystoDeterminismResult = [pscustomobject]@{
            pair_tag = $determinismPairTag
            exit_code = $determinismExitCode
            status = $determinismStatus
            receipt = $determinismReceipt
            receipt_sha256 = Get-OptionalSha256 -Path $determinismReceipt
            orchestration_log = $determinismLog
        }
        $calystoDeterminismPassed = (
            $determinismExitCode -eq 0 -and $determinismStatus -ceq 'PASS'
        )
        $calystoDeterminismStatus = if ($calystoDeterminismPassed) { 'PASS' } else { 'FAIL' }
        if (-not $calystoDeterminismPassed) {
            throw "Calysto V6 packaged determinism failed with exit code $determinismExitCode and status $determinismStatus. Archive retained: $archiveRoot"
        }
    }
    else {
        $calystoSmokeStatus = 'PENDING_MISSING_DEFINITIVE_V6_RUNNER'
        $calystoDeterminismStatus = 'PENDING_MISSING_DEFINITIVE_V6_RUNNER'
    }

    $startupSmokeExitCode = Invoke-LoggedPowerShell `
        -LogPath $startupSmokeLog `
        -Arguments @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $clothingStartupSmoke,
            '-ArchiveRoot', $archiveRoot,
            '-ProjectRoot', $ProjectRoot,
            '-ObservationSeconds', [string]$ObservationSeconds,
            '-Stamp', $stamp
        )
    if (Test-Path -LiteralPath $startupSummaryPath -PathType Leaf) {
        $startupSmokeStatus = [string](
            (Get-Content -Raw -LiteralPath $startupSummaryPath | ConvertFrom-Json).status
        )
    }
    $startupSmokePassed = ($startupSmokeExitCode -eq 0) -and
        ($startupSmokeStatus -ceq 'UE58_EF_CLOTHING_MORPH_V4_PACKAGED_STARTUP_PASS')
    if (-not $startupSmokePassed) {
        throw "Clothing Morph V4 packaged startup gate failed with exit code $startupSmokeExitCode and status $startupSmokeStatus. Archive retained: $archiveRoot"
    }
}
catch {
    $orchestratorError = $_.Exception.Message
}

$allGatesPassed = $packagePassed -and $packageValidationPassed -and
    $calystoSmokePassed -and $calystoDeterminismPassed -and $startupSmokePassed
$nonRuntimeGatesPassed = $packagePassed -and $packageValidationPassed -and
    $startupSmokePassed -and [string]::IsNullOrWhiteSpace($orchestratorError)
$runtimeGatesPending = $nonRuntimeGatesPassed -and -not $v6RuntimeRunnersAvailable
$cookLogSha256 = Get-OptionalSha256 -Path $cookLog
$packageOrchestrationLogSha256 = Get-OptionalSha256 -Path $packageOrchestrationLog
$packagingContractCopySha256 = Get-OptionalSha256 -Path $packagingContractCopy
$packageValidationSha256 = Get-OptionalSha256 -Path $packageValidationPath
$startupSummarySha256 = Get-OptionalSha256 -Path $startupSummaryPath
$determinismOrchestrationLog = if ($null -ne $calystoDeterminismResult) {
    [string]$calystoDeterminismResult.orchestration_log
}
else { '' }
$calystoOrchestrationLogs = @(
    @($calystoSmokeResults | ForEach-Object { $_.orchestration_log }) +
    @($determinismOrchestrationLog)
)
$durableEvidenceSidecars = @(
    $cookLog, $packageOrchestrationLog, $packageValidationPath,
    $packageValidationLog, $packagingContractCopy, $startupSmokeLog
) + $calystoOrchestrationLogs
if ($allGatesPassed) {
    try {
        Remove-ManagedPendingValidationArchive `
            -ArchivePath $archiveRoot `
            -PendingValidationRoot $pendingValidationRoot
        $cleanupStatus = 'PASS'
    }
    catch {
        $cleanupStatus = 'FAIL'
        $cleanupError = $_.Exception.Message
    }
}
else {
    $cleanupStatus = if ($runtimeGatesPending) {
        'RETAINED_PENDING_DEFINITIVE_V6_RUNTIME_GATES'
    }
    else { 'RETAINED_AFTER_GATE_FAILURE' }
}

$overallStatus = if ($allGatesPassed -and $cleanupStatus -eq 'PASS') {
    'PASS'
}
elseif ($runtimeGatesPending) { 'PENDING' }
else { 'FAIL' }
$receipt = [ordered]@{
    schema_version = 2
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = $overallStatus
    target = 'NoShellForWinter'
    configuration = $Configuration
    validation_mode = $ValidationMode
    archive_root = $archiveRoot
    pending_validation_root = $pendingValidationRoot
    package = [ordered]@{
        mode = if ($resumeMode) { 'resume_existing_fresh_archive' } else { 'fresh_package' }
        status = if ($packagePassed) { 'PASS' } elseif ($null -eq $packageExitCode) { 'NOT_RUN' } else { 'FAIL' }
        exit_code = $packageExitCode
        cook_log = $cookLog
        cook_log_sha256 = $cookLogSha256
        orchestration_log = $packageOrchestrationLog
        orchestration_log_sha256 = $packageOrchestrationLogSha256
        durable_packaging_contract = $packagingContractCopy
        durable_packaging_contract_sha256 = $packagingContractCopySha256
    }
    calysto_v6_package_validation = [ordered]@{
        status = $packageValidationStatus
        exit_code = $packageValidationExitCode
        report = $packageValidationPath
        report_sha256 = $packageValidationSha256
        log = $packageValidationLog
    }
    calysto_v6_packaged_smoke = [ordered]@{
        status = $calystoSmokeStatus
        runner = $v6PackagedSmokeRunner
        runner_available = Test-Path -LiteralPath $v6PackagedSmokeRunner -PathType Leaf
        expected_case_count = $calystoSmokeCases.Count
        completed_case_count = $calystoSmokeResults.Count
        cases = $calystoSmokeResults
    }
    calysto_v6_packaged_determinism = [ordered]@{
        status = $calystoDeterminismStatus
        runner = $v6PackagedDeterminismRunner
        runner_available = Test-Path -LiteralPath $v6PackagedDeterminismRunner -PathType Leaf
        result = $calystoDeterminismResult
    }
    realistic_blood_distribution_entitlement = [ordered]@{
        status = 'PENDING'
        distribution_authorized = $false
        note = 'Local Marketplace license/entitlement evidence has not been confirmed by this technical package workflow.'
    }
    clothing_morph_v4_packaged_startup = [ordered]@{
        status = $startupSmokeStatus
        exit_code = $startupSmokeExitCode
        summary = $startupSummaryPath
        summary_sha256 = $startupSummarySha256
        log = $startupSmokeLog
    }
    cleanup = [ordered]@{
        status = $cleanupStatus
        archive_removed_after_pass = ($cleanupStatus -eq 'PASS') -and
            -not (Test-Path -LiteralPath $archiveRoot)
        archive_retained = Test-Path -LiteralPath $archiveRoot
        durable_evidence_sidecars = @($durableEvidenceSidecars | Where-Object {
            -not [string]::IsNullOrWhiteSpace([string]$_) -and
            (Test-Path -LiteralPath ([string]$_) -PathType Leaf)
        })
        error = $cleanupError
    }
    orchestrator_error = $orchestratorError
}
[IO.File]::WriteAllText(
    $receiptPath,
    ($receipt | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false)
)

Write-Output "NoShellForWinter package-validation receipt: $receiptPath"
if ($overallStatus -ne 'PASS') {
    if ($overallStatus -eq 'PENDING') {
        throw "NoShellForWinter $Configuration package validation is PENDING definitive Calysto V6 packaged smoke/determinism runners. Archive retained: $archiveRoot Receipt: $receiptPath"
    }
    throw "NoShellForWinter $Configuration package-validation workflow failed. Receipt: $receiptPath"
}
Write-Output 'NoShellForWinter package-validation workflow: PASS; temporary archive purged and evidence sidecars retained.'
