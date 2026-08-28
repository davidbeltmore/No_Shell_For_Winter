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
$compilerScript = Join-Path $projectRootPath `
    'Tools\ClothingMorphV4\Compile-EFClothingMorphV4Catalog58.py'
$receiptGuard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$receiptDirectory = Join-Path $projectRootPath 'Saved\ClothingMorphV4QA'
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$receipt = Join-Path $receiptDirectory "compiler_receipt_$Stamp.json"
$log = Join-Path $logDirectory "EFClothingMorphV4Compiler_$Stamp.log"

foreach ($requiredFile in @($project, $editorCmd, $compilerScript, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file is missing: $requiredFile"
    }
}
if (@(Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Close Unreal Editor before strict V4 clothing catalog certification.'
}
New-Item -ItemType Directory -Path $receiptDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

$arguments = @(
    ('"' + $project + '"')
    '-run=pythonscript'
    ('-script="' + $compilerScript + '"')
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
$env:EF_CLOTHING_V4_DIRECTOR = $DirectorPath
$env:EF_CLOTHING_V4_COMPATIBILITY = $CompatibilityPath
$env:EF_CLOTHING_V4_RECEIPT = $receipt
try {
    $process = Start-Process -FilePath $editorCmd -ArgumentList $arguments `
        -WorkingDirectory $projectRootPath -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        throw "V4 catalog compilation exceeded $TimeoutSeconds seconds. Log: $log"
    }
    $compileExitCode = $process.ExitCode
}
finally {
    Remove-Item Env:EF_CLOTHING_V4_DIRECTOR -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V4_COMPATIBILITY -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V4_RECEIPT -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
    throw "V4 compiler receipt is missing after exit $compileExitCode`: $receipt"
}
$result = Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
$commandletSucceeded = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed successfully' -Quiet
)
$commandletFailed = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet
)
if (($compileExitCode -ne 0 -and $compileExitCode -ne 1) -or
    -not $commandletSucceeded -or $commandletFailed -or
    $result.status -ne 'UE58_EF_CLOTHING_MORPH_V4_CATALOG_COMPILE_PASS' -or
    -not [bool]$result.success -or
    [int]$result.schema_version -ne 1 -or
    [int]$result.compiler_version -ne 28 -or
    [int]$result.binding_schema_version -ne 8 -or
    [int]$result.director_schema_version -ne 5 -or
    $result.director_id -ne 'EFClothingMorphV4' -or
    $result.output_root -ne '/EFClothingMorph/_Internal/Compiled/V4' -or
    -not [bool]$result.catalog_equality_gate -or
    [int]$result.failed_row_count -ne 0 -or
    [int]$result.published_row_count -ne [int]$result.enabled_row_count -or
    -not [bool]$result.strict_validation.fresh -or
    -not [bool]$result.protected_inputs_unchanged -or
    [int]$result.registry_profile_count -ne 0) {
    throw "V4 catalog compiler gate failed: exit=$compileExitCode receipt=$receipt log=$log"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $projectRootPath -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed after V4 compilation: $LASTEXITCODE"
}

[pscustomobject]@{
    Status = $result.status
    Clothes = @($result.clothing_names) -join ', '
    EnabledClothes = $result.enabled_row_count
    CompiledRows = $result.compiled_row_count
    ReusedRows = $result.reused_fresh_row_count
    PublishedRows = $result.published_row_count
    ValidBindings = $result.valid_binding_count
    RegistryProfiles = $result.registry_profile_count
    RegistryNativeBindings = $result.registry_native_binding_count
    ProtectedInputsUnchanged = $result.protected_inputs_unchanged
    Registry = $result.registry
    Receipt = $receipt
    Log = $log
} | Format-List
