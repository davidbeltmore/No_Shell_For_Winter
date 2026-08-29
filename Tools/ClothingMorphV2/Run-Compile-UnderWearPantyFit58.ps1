[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [switch]$PreviewRestOnly,
    [ValidateRange(60, 21600)]
    [int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = 'Stop'

throw 'This single-garment compiler is retired because it can invalidate the atomic Clothing Director registry. Use Tools\ClothingMorphV2\Compile-EFClothingGarmentCatalog58.ps1.'

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$project = Join-Path $root 'NoShellForWinter.uproject'
$editorCmd = Join-Path $engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$pythonScript = Join-Path $root 'Tools\ClothingMorphV2\Compile-UnderWearPantyFit58.py'
$cleanupPythonScript = Join-Path $root 'Tools\ClothingMorphV2\Cleanup-EFClothingMorphV2Generated58.py'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$receiptDirectory = Join-Path $root 'Saved\ClothingMorphV2QA'
$logDirectory = Join-Path $root 'Saved\Migration\Logs'
$modeLabel = if ($PreviewRestOnly) { 'PreviewRestOnly' } else { 'FullCatalog' }
$receipt = Join-Path $receiptDirectory "compiler_receipt_${modeLabel}_$Stamp.json"
$cleanupReceipt = Join-Path $receiptDirectory "generated_cleanup_receipt_${modeLabel}_$Stamp.json"
$log = Join-Path $logDirectory "EFClothingMorphV2Compiler_${modeLabel}_$Stamp.log"
$cleanupLog = Join-Path $logDirectory "EFClothingMorphV2GeneratedCleanup_${modeLabel}_$Stamp.log"

foreach ($requiredPath in @($project, $editorCmd, $pythonScript, $cleanupPythonScript, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required path is missing: $requiredPath"
    }
}

$resolvedRootPrefix = $root.TrimEnd('\') + '\'
$resolvedCleanupPythonScript = [System.IO.Path]::GetFullPath($cleanupPythonScript)
if (-not $resolvedCleanupPythonScript.StartsWith(
        $resolvedRootPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Generated cleanup script escapes the target project: $resolvedCleanupPythonScript"
}

$resolvedPythonScript = [System.IO.Path]::GetFullPath($pythonScript)
if (-not $resolvedPythonScript.StartsWith(
        $resolvedRootPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Python compiler script escapes the target project: $resolvedPythonScript"
}

$existingEditors = @(
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProcessName -eq 'UnrealEditor' -or
            $_.ProcessName -eq 'UnrealEditor-Cmd'
        }
)
if ($existingEditors.Count -ne 0) {
    $processSummary = ($existingEditors | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ', '
    throw "Close all Unreal Editor processes before compiling generated clothing assets: $processSummary"
}

New-Item -ItemType Directory -Path $receiptDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

& powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $root
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

$arguments = @(
    ('"' + $project + '"')
    '-run=pythonscript'
    ('-script="' + $resolvedPythonScript + '"')
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    '-stdout'
    '-FullStdOutLogOutput'
    '-UTF8Output'
    '-EnablePlugin=DazToUnreal'
    '-EnablePlugin=EFCharacterCreationDazBridge'
    ('-ABSLOG="' + $log + '"')
)

$env:EF_CLOTHING_V2_RECEIPT = $receipt
$env:EF_CLOTHING_V2_PREVIEW_REST_ONLY = if ($PreviewRestOnly) { '1' } else { '0' }
try {
    $process = Start-Process `
        -FilePath $editorCmd `
        -ArgumentList $arguments `
        -WorkingDirectory $root `
        -WindowStyle Hidden `
        -PassThru

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        throw "EF Clothing Morph V2 compiler exceeded $TimeoutSeconds seconds. Log: $log"
    }
    $exitCode = $process.ExitCode
}
finally {
    Remove-Item Env:EF_CLOTHING_V2_RECEIPT -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V2_PREVIEW_REST_ONLY -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
    throw "Compiler receipt is missing after exit code ${exitCode}: $receipt (log: $log)"
}

$result = Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
$expectedStatus = 'UE58_EF_CLOTHING_MORPH_V2_COMPILE_PASS'
$commandletSucceeded = [bool](Select-String -LiteralPath $log -SimpleMatch 'Python script executed successfully' -Quiet)
$commandletFailed = [bool](Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet)
# This project currently reports pre-existing GameFeatureData load errors during
# commandlet shutdown, which maps the clean Python result to process exit 1.
# The commandlet's own result plus the fail-closed receipt remain authoritative.
$acceptableProcessExit = $exitCode -eq 0 -or $exitCode -eq 1
if (
    -not $acceptableProcessExit -or
    -not $commandletSucceeded -or
    $commandletFailed -or
    $result.status -ne $expectedStatus -or
    -not [bool]$result.success -or
    -not [bool]$result.compile_success -or
    -not [bool]$result.validation_success -or
    -not [bool]$result.protected_inputs_unchanged
) {
    throw (
        "EF Clothing Morph V2 compiler gate failed: exit=$exitCode; " +
        "status=$($result.status); receipt=$receipt; log=$log"
    )
}

$cleanupArguments = @(
    ('"' + $project + '"')
    '-run=pythonscript'
    ('-script="' + $resolvedCleanupPythonScript + '"')
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    '-stdout'
    '-FullStdOutLogOutput'
    '-UTF8Output'
    '-EnablePlugin=DazToUnreal'
    '-EnablePlugin=EFCharacterCreationDazBridge'
    ('-ABSLOG="' + $cleanupLog + '"')
)

$env:EF_CLOTHING_V2_CLEANUP_RECEIPT = $cleanupReceipt
try {
    $cleanupProcess = Start-Process `
        -FilePath $editorCmd `
        -ArgumentList $cleanupArguments `
        -WorkingDirectory $root `
        -WindowStyle Hidden `
        -PassThru

    $cleanupDeadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $cleanupProcess.HasExited -and (Get-Date) -lt $cleanupDeadline) {
        Start-Sleep -Milliseconds 500
        $cleanupProcess.Refresh()
    }
    if (-not $cleanupProcess.HasExited) {
        Stop-Process -Id $cleanupProcess.Id -Force
        throw "EF Clothing Morph V2 generated cleanup exceeded $TimeoutSeconds seconds. Log: $cleanupLog"
    }
    $cleanupExitCode = $cleanupProcess.ExitCode
}
finally {
    Remove-Item Env:EF_CLOTHING_V2_CLEANUP_RECEIPT -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $cleanupReceipt -PathType Leaf)) {
    throw "Generated cleanup receipt is missing after exit code ${cleanupExitCode}: $cleanupReceipt"
}
$cleanupResult = Get-Content -Raw -LiteralPath $cleanupReceipt | ConvertFrom-Json
$cleanupCommandletSucceeded = [bool](
    Select-String -LiteralPath $cleanupLog -SimpleMatch 'Python script executed successfully' -Quiet
)
$cleanupCommandletFailed = [bool](
    Select-String -LiteralPath $cleanupLog -SimpleMatch 'Python script executed with errors' -Quiet
)
$acceptableCleanupExit = $cleanupExitCode -eq 0 -or $cleanupExitCode -eq 1
if (
    -not $acceptableCleanupExit -or
    -not $cleanupCommandletSucceeded -or
    $cleanupCommandletFailed -or
    $cleanupResult.status -ne 'UE58_EF_CLOTHING_MORPH_V2_GENERATED_CLEANUP_PASS' -or
    -not [bool]$cleanupResult.success -or
    @($cleanupResult.remaining_registry_packages).Count -ne 0 -or
    @($cleanupResult.remaining_physical_packages).Count -ne 0
) {
    throw (
        "EF Clothing Morph V2 generated cleanup gate failed: exit=$cleanupExitCode; " +
        "status=$($cleanupResult.status); receipt=$cleanupReceipt; log=$cleanupLog"
    )
}

[pscustomobject]@{
    Status = $result.status
    ExitCode = $exitCode
    CommandletSucceeded = $commandletSucceeded
    DerivedGarment = $result.outputs.derived_garment
    Profile = $result.outputs.profile
    PenetratingBefore = $result.metrics.penetrating_vertex_count_before
    PenetratingAfter = $result.metrics.penetrating_vertex_count_after
    MinimumGapAfterCm = $result.metrics.minimum_signed_gap_after_cm
    ProtectedInputsUnchanged = $result.protected_inputs_unchanged
    ProfileMode = $result.profile_mode
    GeneratedCleanupStatus = $cleanupResult.status
    DeletedStaleGeneratedPackages = @($cleanupResult.deleted_packages).Count
    Receipt = $receipt
    CleanupReceipt = $cleanupReceipt
    Log = $log
    CleanupLog = $cleanupLog
} | Format-List
