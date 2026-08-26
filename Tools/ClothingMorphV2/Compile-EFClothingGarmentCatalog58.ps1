[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$CatalogPath = '/Game/_Game/Data/EFClothingMorph/DT_EFClothingGarments',
    [string]$CompatibilityPath = '/Game/DazToUnreal/Multiple/Multiple',
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [ValidateRange(60, 21600)]
    [int]$TimeoutSeconds = 3600
)

$ErrorActionPreference = 'Stop'

$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$project = Join-Path $projectRootPath 'NoShellForWinter.uproject'
$editorCmd = Join-Path $engineRootPath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$compilerScript = Join-Path $projectRootPath 'Tools\ClothingMorphV2\Compile-EFClothingGarmentCatalog58.py'
$cleanupScript = Join-Path $projectRootPath 'Tools\ClothingMorphV2\Cleanup-EFClothingMorphV2Generated58.py'
$receiptGuard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$receiptDirectory = Join-Path $projectRootPath 'Saved\ClothingMorphV2QA'
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$receipt = Join-Path $receiptDirectory "compiler_receipt_FullCatalog_V26_$Stamp.json"
$cleanupReceipt = Join-Path $receiptDirectory "generated_cleanup_receipt_FullCatalog_V26_$Stamp.json"
$log = Join-Path $logDirectory "EFClothingMorphV26CatalogCompiler_$Stamp.log"
$cleanupLog = Join-Path $logDirectory "EFClothingMorphV26CatalogCleanup_$Stamp.log"

foreach ($requiredFile in @($project, $editorCmd, $compilerScript, $cleanupScript, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file is missing: $requiredFile"
    }
}
foreach ($assetPath in @($CatalogPath, $CompatibilityPath)) {
    if (-not $assetPath.StartsWith('/Game/', [System.StringComparison]::Ordinal)) {
        throw "Catalog and compatibility paths must remain under /Game: $assetPath"
    }
}
$projectRootPrefix = $projectRootPath.TrimEnd('\') + '\'
foreach ($scriptPath in @($compilerScript, $cleanupScript)) {
    $resolvedScriptPath = [System.IO.Path]::GetFullPath($scriptPath)
    if (-not $resolvedScriptPath.StartsWith(
            $projectRootPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Commandlet script escapes the target project: $resolvedScriptPath"
    }
}

$existingEditors = @(
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.ProcessName -eq 'UnrealEditor' -or
            $_.ProcessName -eq 'UnrealEditor-Cmd'
        }
)
if ($existingEditors.Count -ne 0) {
    $summary = ($existingEditors | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ', '
    throw "Close all Unreal Editor processes before atomic catalog compilation: $summary"
}

New-Item -ItemType Directory -Path $receiptDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
& powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

function Invoke-EFClothingPythonCommandlet {
    param(
        [Parameter(Mandatory = $true)][string]$PythonScript,
        [Parameter(Mandatory = $true)][string]$AbsoluteLog,
        [Parameter(Mandatory = $true)][int]$Timeout
    )
    $arguments = @(
        ('"' + $project + '"')
        '-run=pythonscript'
        ('-script="' + [System.IO.Path]::GetFullPath($PythonScript) + '"')
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
        ('-ABSLOG="' + $AbsoluteLog + '"')
    )
    $process = Start-Process `
        -FilePath $editorCmd `
        -ArgumentList $arguments `
        -WorkingDirectory $projectRootPath `
        -WindowStyle Hidden `
        -PassThru
    $deadline = (Get-Date).AddSeconds($Timeout)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        throw "EF Clothing commandlet exceeded $Timeout seconds. Log: $AbsoluteLog"
    }
    return $process.ExitCode
}

$env:EF_CLOTHING_V26_CATALOG = $CatalogPath
$env:EF_CLOTHING_V26_COMPATIBILITY = $CompatibilityPath
$env:EF_CLOTHING_V26_RECEIPT = $receipt
try {
    $compileExitCode = Invoke-EFClothingPythonCommandlet `
        -PythonScript $compilerScript `
        -AbsoluteLog $log `
        -Timeout $TimeoutSeconds
}
finally {
    Remove-Item Env:EF_CLOTHING_V26_CATALOG -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V26_COMPATIBILITY -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V26_RECEIPT -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
    throw "V26 catalog receipt is missing after exit $compileExitCode`: $receipt"
}
$compileResult = Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
$commandletSucceeded = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed successfully' -Quiet
)
$commandletFailed = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet
)
$acceptableExit = $compileExitCode -eq 0 -or $compileExitCode -eq 1
if (
    -not $acceptableExit -or
    -not $commandletSucceeded -or
    $commandletFailed -or
    $compileResult.status -ne 'UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_PASS' -or
    -not [bool]$compileResult.success -or
    -not [bool]$compileResult.catalog_equality_gate -or
    -not [bool]$compileResult.protected_inputs_unchanged
) {
    throw "V26 catalog compiler gate failed: exit=$compileExitCode receipt=$receipt log=$log"
}

$env:EF_CLOTHING_V2_CLEANUP_RECEIPT = $cleanupReceipt
try {
    $cleanupExitCode = Invoke-EFClothingPythonCommandlet `
        -PythonScript $cleanupScript `
        -AbsoluteLog $cleanupLog `
        -Timeout $TimeoutSeconds
}
finally {
    Remove-Item Env:EF_CLOTHING_V2_CLEANUP_RECEIPT -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $cleanupReceipt -PathType Leaf)) {
    throw "V26 generated cleanup receipt is missing after exit $cleanupExitCode`: $cleanupReceipt"
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
    throw "V26 generated cleanup gate failed: exit=$cleanupExitCode receipt=$cleanupReceipt log=$cleanupLog"
}

[pscustomobject]@{
    Status = $compileResult.status
    EnabledRows = $compileResult.enabled_row_count
    SurfaceWrapRows = $compileResult.surface_wrap_row_count
    CompiledRows = $compileResult.compiled_row_count
    ValidProfiles = $compileResult.valid_profile_count
    ValidBindings = $compileResult.valid_binding_count
    ProtectedInputsUnchanged = $compileResult.protected_inputs_unchanged
    Registry = $compileResult.registry
    Receipt = $receipt
    CleanupReceipt = $cleanupReceipt
    Log = $log
    CleanupLog = $cleanupLog
} | Format-List
