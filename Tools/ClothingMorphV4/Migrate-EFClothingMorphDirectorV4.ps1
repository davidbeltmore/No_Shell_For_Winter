[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [switch]$ResetRuntimeFitDefaults,
    [ValidateRange(60, 1800)]
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$project = Join-Path $projectRootPath 'NoShellForWinter.uproject'
$editorCmd = Join-Path $engineRootPath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = Join-Path $projectRootPath 'Tools\ClothingMorphV4\Migrate-EFClothingMorphDirectorV4.py'
$guard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$receiptDirectory = Join-Path $projectRootPath 'Saved\ClothingMorphV4QA'
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$receipt = Join-Path $receiptDirectory "director_migration_$Stamp.json"
$log = Join-Path $logDirectory "EFClothingMorphV4DirectorMigration_$Stamp.log"

foreach ($requiredFile in @($project, $editorCmd, $script, $guard)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file is missing: $requiredFile"
    }
}
if (@(Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Close Unreal Editor before migrating the single Clothing Morph Director.'
}
New-Item -ItemType Directory -Path $receiptDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $guard -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed before migration: $LASTEXITCODE"
}

$arguments = @(
    ('"' + $project + '"')
    '-run=pythonscript'
    ('-script="' + $script + '"')
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
$env:EF_CLOTHING_V4_MIGRATION_RECEIPT = $receipt
$env:EF_CLOTHING_V4_RESET_RUNTIME_FIT_DEFAULTS = if ($ResetRuntimeFitDefaults) { '1' } else { '0' }
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
        throw "Director migration exceeded $TimeoutSeconds seconds. Log: $log"
    }
    $exitCode = $process.ExitCode
}
finally {
    Remove-Item Env:EF_CLOTHING_V4_MIGRATION_RECEIPT -ErrorAction SilentlyContinue
    Remove-Item Env:EF_CLOTHING_V4_RESET_RUNTIME_FIT_DEFAULTS -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
    throw "Director migration receipt is missing after exit $exitCode`: $receipt"
}
$result = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
$commandletSucceeded = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed successfully' -Quiet
)
$commandletFailed = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet
)
if (($exitCode -ne 0 -and $exitCode -ne 1) -or
    -not $commandletSucceeded -or $commandletFailed -or
    $result.status -ne 'UE58_EF_CLOTHING_MORPH_V4_DIRECTOR_MIGRATION_PASS' -or
    -not [bool]$result.success -or
    [int]$result.director_schema_version -ne 5 -or
    $result.director_id -ne 'EFClothingMorphV4' -or
    [int]$result.public_authoring_asset_count -ne 1 -or
    -not [bool]$result.clothing_names_valid -or
    -not [bool]$result.protected_inputs_unchanged) {
    throw "Director V4 migration gate failed: receipt=$receipt log=$log"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $guard `
    -ProjectRoot $projectRootPath -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed after migration: $LASTEXITCODE"
}

[pscustomobject]@{
    Status = $result.status
    Director = $result.director
    DirectorId = $result.director_id
    Schema = $result.director_schema_version
    Clothes = @($result.clothing_names) -join ', '
    AutoNamedClothes = @($result.auto_named_clothes) -join ', '
    ProtectedInputsUnchanged = $result.protected_inputs_unchanged
    Receipt = $receipt
    Log = $log
} | Format-List
