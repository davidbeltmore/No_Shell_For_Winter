[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [ValidateRange(60, 1800)]
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$project = Join-Path $projectRootPath 'NoShellForWinter.uproject'
$editorCmd = Join-Path $engineRootPath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = Join-Path $projectRootPath 'Tools\ClothingMorphV2\Create-EFClothingMorphDirector58.py'
$receiptGuard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$log = Join-Path $logDirectory "EFClothingMorphDirector_$stamp.log"

foreach ($required in @($project, $editorCmd, $script, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required path is missing: $required"
    }
}
if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -eq 'UnrealEditor' -or $_.ProcessName -eq 'UnrealEditor-Cmd'
    }).Count -ne 0) {
    throw 'Close Unreal Editor before creating or migrating Clothing Director assets.'
}

New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed before Director creation: $LASTEXITCODE"
}

$arguments = @(
    ('"' + $project + '"')
    '-run=pythonscript'
    ('-script="' + [System.IO.Path]::GetFullPath($script) + '"')
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
$process = Start-Process -FilePath $editorCmd -ArgumentList $arguments -WorkingDirectory $projectRootPath -WindowStyle Hidden -PassThru
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $process.Refresh()
}
if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force
    throw "Clothing Director creation exceeded $TimeoutSeconds seconds. Log: $log"
}

$receiptDirectory = Join-Path $projectRootPath 'Saved\ClothingMorphV2QA\Director'
$receipt = Get-ChildItem -LiteralPath $receiptDirectory -File -Filter 'EFClothingMorphDirector_*.json' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if ($null -eq $receipt) {
    throw "Clothing Director receipt is missing after exit $($process.ExitCode). Log: $log"
}
$payload = Get-Content -LiteralPath $receipt.FullName -Raw | ConvertFrom-Json
$succeeded = Select-String -LiteralPath $log -SimpleMatch 'Python script executed successfully' -Quiet
$failed = Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet
if (
    $process.ExitCode -notin @(0, 1) -or
    -not $succeeded -or
    $failed -or
    -not [bool]$payload.success -or
    $payload.status -ne 'PASS' -or
    -not [bool]$payload.protected_inputs_unchanged -or
    -not [bool]$payload.legacy_assets_retained_unchanged -or
    -not [bool]$payload.existing_authoring_fields_preserved -or
    [string]$payload.thickness_authoring_metadata_version -ne '2' -or
    [int]$payload.director_schema_version -ne 3 -or
    [int]$payload.garment_count -lt 1
) {
    throw "Clothing Director creation failed: exit=$($process.ExitCode) receipt=$($receipt.FullName) log=$log"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed after Director creation: $LASTEXITCODE"
}

[pscustomobject]@{
    Status = $payload.status
    Director = $payload.director
    SchemaVersion = $payload.director_schema_version
    MigrationMode = $payload.migration_mode
    GarmentIds = @($payload.garment_ids) -join ', '
    GarmentCount = $payload.garment_count
    RuntimeOffsetsCm = ($payload.garment_runtime_offsets_cm | ConvertTo-Json -Compress)
    ThicknessShells = ($payload.garment_thickness_shells | ConvertTo-Json -Compress)
    ExistingAuthoringFieldsPreserved = $payload.existing_authoring_fields_preserved
    LegacyAssetsRetainedUnchanged = $payload.legacy_assets_retained_unchanged
    CreatedAssets = @($payload.created_assets) -join ', '
    Receipt = $receipt.FullName
    Log = $log
} | Format-List
