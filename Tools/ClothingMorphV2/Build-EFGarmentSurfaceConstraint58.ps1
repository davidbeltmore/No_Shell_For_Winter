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
$builder = Join-Path $projectRootPath 'Plugins\EFClothingMorph\Source\EFClothingMorphEditor\Scripts\Build-EFGarmentSurfaceConstraint58.py'
$receiptGuard = Join-Path $projectRootPath 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$logDirectory = Join-Path $projectRootPath 'Saved\Migration\Logs'
$log = Join-Path $logDirectory ('EFClothingMorphV26SurfaceGraph_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.log')

foreach ($requiredFile in @($project, $editorCmd, $builder, $receiptGuard)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file is missing: $requiredFile"
    }
}
$existingEditors = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -eq 'UnrealEditor' -or $_.ProcessName -eq 'UnrealEditor-Cmd'
})
if ($existingEditors.Count -ne 0) {
    throw 'Close every Unreal Editor process before rebuilding the immutable surface graph.'
}

New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
& powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

$arguments = @(
    ('"' + $project + '"')
    '-run=pythonscript'
    ('-script="' + $builder + '"')
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
$process = Start-Process -FilePath $editorCmd -ArgumentList $arguments `
    -WorkingDirectory $projectRootPath -WindowStyle Hidden -PassThru
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $process.Refresh()
}
if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force
    throw "Surface graph build exceeded $TimeoutSeconds seconds. Log: $log"
}
$commandletSucceeded = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed successfully' -Quiet
)
$commandletFailed = [bool](
    Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet
)
if (($process.ExitCode -ne 0 -and $process.ExitCode -ne 1) -or -not $commandletSucceeded -or $commandletFailed) {
    throw "Surface graph build failed: exit=$($process.ExitCode) log=$log"
}

$receipt = Get-ChildItem -LiteralPath (Join-Path $projectRootPath 'Saved\ClothingMorphV2QA\GraphBuilder') `
    -Filter 'DG_EFGarmentSurfaceConstraint_*.json' -File |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $receipt) {
    throw 'Surface graph builder produced no receipt.'
}
$result = Get-Content -LiteralPath $receipt.FullName -Raw | ConvertFrom-Json
if (-not [bool]$result.success -or $result.schema -ne 'EFClothingMorph.SurfaceGraph.26.6') {
    throw "Surface graph receipt is stale or failed: $($receipt.FullName)"
}

[pscustomobject]@{
    Status = 'UE58_EF_CLOTHING_MORPH_V26_SURFACE_GRAPH_PASS'
    Schema = $result.schema
    Receipt = $receipt.FullName
    Log = $log
} | Format-List
