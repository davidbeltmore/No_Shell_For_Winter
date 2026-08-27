[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [ValidateRange(60, 1800)]
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$project = Join-Path $projectRootPath 'NoShellForWinter.uproject'
$editorCmd = Join-Path $engineRootPath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = Join-Path $projectRootPath 'Tools\ClothingMorphV2\Finalize-EFClothingMorphSingleDirector58.py'
$receiptGuard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$receiptDirectory = Join-Path $projectRootPath 'Saved\ClothingMorphV2QA\Director'
$receipt = Join-Path $receiptDirectory "EFClothingMorphSingleDirectorFinalization_$Stamp.json"
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$log = Join-Path $logDirectory "EFClothingMorphSingleDirectorFinalization_$Stamp.log"

foreach ($required in @($project, $editorCmd, $script, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required path is missing: $required"
    }
}
if ([System.IO.Path]::GetFileName($project) -ne 'NoShellForWinter.uproject') {
    throw "Finalization is restricted to NoShellForWinter.uproject: $project"
}
$projectRootPrefix = $projectRootPath.TrimEnd('\') + '\'
$resolvedScriptPath = [System.IO.Path]::GetFullPath($script)
if (-not $resolvedScriptPath.StartsWith(
        $projectRootPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Finalization script escaped the target project: $resolvedScriptPath"
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
    throw "Close every Unreal Editor process before single-Director finalization: $summary"
}

if (Test-Path -LiteralPath $receipt) {
    throw "Refusing to overwrite an existing finalization receipt: $receipt"
}
New-Item -ItemType Directory -Path $receiptDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed before single-Director finalization: $LASTEXITCODE"
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

$env:EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE = '1'
$env:EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE_RECEIPT = $receipt
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
        throw "Single-Director finalization exceeded $TimeoutSeconds seconds. Log: $log"
    }
    $finalizationExitCode = $process.ExitCode
}
finally {
    Remove-Item Env:EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE_RECEIPT -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
    throw "Single-Director finalization receipt is missing after exit $finalizationExitCode`: $receipt"
}
$payload = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
$commandletSucceeded = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed successfully' -Quiet
)
$commandletFailed = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet
)
$acceptableExit = $finalizationExitCode -eq 0 -or $finalizationExitCode -eq 1
if (
    -not $acceptableExit -or
    -not $commandletSucceeded -or
    $commandletFailed -or
    -not [bool]$payload.success -or
    $payload.status -ne 'UE58_EF_CLOTHING_SINGLE_DIRECTOR_FINALIZE_PASS' -or
    [int]$payload.director_validation.schema_version -ne 2 -or
    [int]$payload.director_validation.garment_count -lt 1 -or
    [int]$payload.registry_validation.profile_count -lt 1 -or
    -not [bool]$payload.protected_inputs_unchanged -or
    -not [bool]$payload.legacy_assets_absent -or
    -not [bool]$payload.single_public_asset
) {
    throw "Single-Director finalization gate failed: exit=$finalizationExitCode receipt=$receipt log=$log"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed after single-Director finalization: $LASTEXITCODE"
}

[pscustomobject]@{
    Status = $payload.status
    Director = $payload.director
    SchemaVersion = $payload.director_validation.schema_version
    Garments = @($payload.director_validation.garment_ids) -join ', '
    Registry = $payload.registry
    Profiles = $payload.registry_validation.profile_count
    DeletedAssets = @($payload.deleted_assets).Count
    PublicAssetsAfter = @($payload.public_assets_after) -join ', '
    ProtectedInputsUnchanged = $payload.protected_inputs_unchanged
    Receipt = $receipt
    Log = $log
} | Format-List
