[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [ValidateRange(60, 600)]
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$project = Join-Path $projectRootPath 'NoShellForWinter.uproject'
$editorCmd = Join-Path $engineRootPath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = Join-Path $projectRootPath 'Tools\ClothingMorphV2\Validate-EFClothingMorphDirector58.py'
$receiptGuard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$log = Join-Path $logDirectory "EFClothingMorphDirectorValidation_$stamp.log"

foreach ($required in @($project, $editorCmd, $script, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required path is missing: $required"
    }
}
if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -eq 'UnrealEditor' -or $_.ProcessName -eq 'UnrealEditor-Cmd'
    }).Count -ne 0) {
    throw 'Close Unreal Editor before validating the Clothing Director.'
}

New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed before Director validation: $LASTEXITCODE"
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
    throw "Clothing Director validation exceeded $TimeoutSeconds seconds. Log: $log"
}

$receiptDirectory = Join-Path $projectRootPath 'Saved\ClothingMorphV2QA\Director'
$receipt = Get-ChildItem -LiteralPath $receiptDirectory -File -Filter 'EFClothingMorphDirectorValidation_*.json' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if ($null -eq $receipt) {
    throw "Clothing Director validation receipt is missing after exit $($process.ExitCode). Log: $log"
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
    [int]$payload.director_schema_version -ne 3 -or
    [int]$payload.compiler_receipt_schema_version -ne 10 -or
    [int]$payload.thickness_shell_algorithm_version -ne 4 -or
    -not [bool]$payload.shell_intersection_policy_gate -or
    -not [bool]$payload.compiler_receipt.catalog_equality_gate -or
    -not [bool]$payload.compiler_receipt.protected_inputs_unchanged -or
    [int]$payload.compiler_receipt.residual_shell_intersection_pair_count -ne 0 -or
    (
        [int]$payload.compiler_receipt.tolerated_inherited_shell_intersection_pair_count +
        [int]$payload.compiler_receipt.tolerated_local_repair_shell_intersection_pair_count +
        [int]$payload.compiler_receipt.tolerated_excluded_region_shell_intersection_pair_count
    ) -ne [int]$payload.compiler_receipt.detected_shell_intersection_pair_count -or
    -not [bool]$payload.protected_inputs_unchanged
) {
    throw "Clothing Director validation failed: exit=$($process.ExitCode) receipt=$($receipt.FullName) log=$log"
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed after Director validation: $LASTEXITCODE"
}

[pscustomobject]@{
    Status = $payload.status
    Director = $payload.director
    Registry = $payload.registry
    SchemaVersion = $payload.director_schema_version
    GarmentIds = @($payload.garment_ids) -join ', '
    EnabledGarmentIds = @($payload.enabled_garment_ids) -join ', '
    RuntimeOffsetsCm = ($payload.garment_offsets_cm | ConvertTo-Json -Compress)
    ThicknessShells = ($payload.garment_thickness_shells | ConvertTo-Json -Compress)
    ThicknessShellAlgorithm = $payload.thickness_shell_algorithm_version
    DetectedShellIntersectionPairs = $payload.compiler_receipt.detected_shell_intersection_pair_count
    BaselineSourceIntersectionPairs = $payload.compiler_receipt.baseline_source_shell_intersection_pair_count
    ToleratedInheritedSourcePairs = $payload.compiler_receipt.tolerated_inherited_shell_intersection_pair_count
    ToleratedLocalRepairPairs = $payload.compiler_receipt.tolerated_local_repair_shell_intersection_pair_count
    ResidualNewIntersectionPairs = $payload.compiler_receipt.residual_shell_intersection_pair_count
    ToleratedExcludedRegionPairs = $payload.compiler_receipt.tolerated_excluded_region_shell_intersection_pair_count
    RuntimeOffsetLimitCm = $payload.runtime_offset_limit_cm
    PerGarmentOffsetsOnly = $payload.per_garment_runtime_offsets_only
    CompilerReceipt = $payload.compiler_receipt.path
    Receipt = $receipt.FullName
    Log = $log
} | Format-List
