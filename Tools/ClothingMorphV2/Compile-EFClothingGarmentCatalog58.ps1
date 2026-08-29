[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$DirectorPath = '/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector',
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
$receiptGuard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$receiptDirectory = Join-Path $projectRootPath 'Saved\ClothingMorphV2QA'
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$receipt = Join-Path $receiptDirectory "compiler_receipt_FullCatalog_V26_$Stamp.json"
$log = Join-Path $logDirectory "EFClothingMorphV26DirectorCompiler_$Stamp.log"

foreach ($requiredFile in @($project, $editorCmd, $compilerScript, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file is missing: $requiredFile"
    }
}
foreach ($assetPath in @($DirectorPath, $CompatibilityPath)) {
    if (-not $assetPath.StartsWith('/Game/', [System.StringComparison]::Ordinal)) {
        throw "Director and compatibility paths must remain under /Game: $assetPath"
    }
}
$projectRootPrefix = $projectRootPath.TrimEnd('\') + '\'
$resolvedScriptPath = [System.IO.Path]::GetFullPath($compilerScript)
if (-not $resolvedScriptPath.StartsWith(
        $projectRootPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Commandlet script escapes the target project: $resolvedScriptPath"
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
    throw "Close all Unreal Editor processes before atomic Director compilation: $summary"
}

New-Item -ItemType Directory -Path $receiptDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

$arguments = @(
    ('"' + $project + '"')
    '-run=pythonscript'
    ('-script="' + $resolvedScriptPath + '"')
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

$env:EF_CLOTHING_V26_DIRECTOR = $DirectorPath
$env:EF_CLOTHING_V26_COMPATIBILITY = $CompatibilityPath
$env:EF_CLOTHING_V26_RECEIPT = $receipt
try {
    $process = Start-Process `
        -FilePath $editorCmd `
        -ArgumentList $arguments `
        -WorkingDirectory $projectRootPath `
        -WindowStyle Hidden `
        -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        throw "EF Clothing Director compilation exceeded $TimeoutSeconds seconds. Log: $log"
    }
    $compileExitCode = $process.ExitCode
}
finally {
    Remove-Item Env:EF_CLOTHING_V26_DIRECTOR -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V26_COMPATIBILITY -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V26_RECEIPT -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
    throw "V26 Director receipt is missing after exit $compileExitCode`: $receipt"
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
    [int]$compileResult.schema_version -ne 10 -or
    [int]$compileResult.compiler_version -ne 26 -or
    [int]$compileResult.thickness_shell_algorithm_version -ne 4 -or
    [int]$compileResult.director_schema_version -ne 3 -or
    $compileResult.output_root -ne '/EFClothingMorph/_Internal/Compiled/V26' -or
    -not [bool]$compileResult.catalog_equality_gate -or
    -not [bool]$compileResult.shell_intersection_policy_gate -or
    [int]$compileResult.baseline_source_shell_intersection_pair_count -lt 0 -or
    [int]$compileResult.tolerated_inherited_shell_intersection_pair_count -lt 0 -or
    [int]$compileResult.tolerated_local_repair_shell_intersection_pair_count -lt 0 -or
    [int]$compileResult.tolerated_excluded_region_shell_intersection_pair_count -lt 0 -or
    [int]$compileResult.residual_shell_intersection_pair_count -ne (
        [int]$compileResult.detected_shell_intersection_pair_count -
        [int]$compileResult.tolerated_inherited_shell_intersection_pair_count -
        [int]$compileResult.tolerated_local_repair_shell_intersection_pair_count -
        [int]$compileResult.tolerated_excluded_region_shell_intersection_pair_count
    ) -or
    (
        [int]$compileResult.tolerated_inherited_shell_intersection_pair_count +
        [int]$compileResult.tolerated_local_repair_shell_intersection_pair_count +
        [int]$compileResult.tolerated_excluded_region_shell_intersection_pair_count
    ) -ne [int]$compileResult.detected_shell_intersection_pair_count -or
    [int]$compileResult.residual_shell_intersection_pair_count -ne 0 -or
    -not [bool]$compileResult.protected_inputs_unchanged
) {
    throw "V26 Director compiler gate failed: exit=$compileExitCode receipt=$receipt log=$log"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed after Director compilation: $LASTEXITCODE"
}

# Generated cleanup is deliberately not chained here.  Schema-1 tables and the
# old /Game/_Generated publication remain available until a separately audited
# retirement step proves that all live references use the internal plugin root.
[pscustomobject]@{
    Status = $compileResult.status
    Director = $compileResult.director
    GarmentIds = @($compileResult.garment_ids) -join ', '
    EnabledGarmentIds = @($compileResult.enabled_garment_ids) -join ', '
    SurfaceWrapRows = $compileResult.surface_wrap_row_count
    CompiledRows = $compileResult.compiled_row_count
    ValidProfiles = $compileResult.valid_profile_count
    ValidBindings = $compileResult.valid_binding_count
    ThicknessShells = "$($compileResult.valid_thickness_shell_count)/$($compileResult.requested_thickness_shell_count)"
    ThicknessShellAlgorithm = $compileResult.thickness_shell_algorithm_version
    DetectedShellIntersectionPairs = $compileResult.detected_shell_intersection_pair_count
    BaselineSourceIntersectionPairs = $compileResult.baseline_source_shell_intersection_pair_count
    ToleratedInheritedSourcePairs = $compileResult.tolerated_inherited_shell_intersection_pair_count
    ToleratedLocalRepairPairs = $compileResult.tolerated_local_repair_shell_intersection_pair_count
    ResidualNewIntersectionPairs = $compileResult.residual_shell_intersection_pair_count
    ToleratedExcludedRegionPairs = $compileResult.tolerated_excluded_region_shell_intersection_pair_count
    InheritedShellIds = @($compileResult.inherited_shell_intersection_ids) -join ', '
    LocalRepairShellIds = @($compileResult.local_repair_shell_intersection_ids) -join ', '
    ExcludedRegionShellIds = @($compileResult.excluded_region_shell_intersection_ids) -join ', '
    ProtectedInputsUnchanged = $compileResult.protected_inputs_unchanged
    Registry = $compileResult.registry
    OutputRoot = $compileResult.output_root
    LegacyCleanup = 'DEFERRED'
    Receipt = $receipt
    Log = $log
} | Format-List
