param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName = 'PIE_02', [switch]$SaveUIQA, [switch]$TravelQA, [switch]$StorageQA, [switch]$ProceduralChestQA, [switch]$ChestTravelQA)
$ErrorActionPreference = 'Stop'
if (([int][bool]$SaveUIQA + [int][bool]$TravelQA + [int][bool]$StorageQA + [int][bool]$ProceduralChestQA + [int][bool]$ChestTravelQA) -gt 1) { throw 'Select only one isolated fixture.' }
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) { throw 'Inventory PIE requires the target editor to be closed.' }
$oldEnable = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldRun = $env:EF_EQUIPMENT_QA_RUN
$qaDirectory = Join-Path $root "Saved/EquipmentQA/$RunName"
if (Test-Path -LiteralPath $qaDirectory) { throw 'Use a fresh RunName; do not reuse visual markers or test receipts.' }
. (Join-Path $root 'Tools/Migration/QAUserSaveGuard.ps1')
$saveSnapshot = @(Backup-UnrealQAUserSaves -ProjectRoot $root -OutputDirectory $qaDirectory)
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $PSScriptRoot $(if ($ChestTravelQA) { 'Validate-ChestTravel.py' } elseif ($ProceduralChestQA) { 'Validate-ProceduralChests.py' } elseif ($StorageQA) { 'Validate-StorageRework.py' } elseif ($SaveUIQA) { 'Validate-SaveUI.py' } elseif ($TravelQA) { 'Validate-EquipmentTravel.py' } else { 'Validate-InventoryPIE.py' })
    $env:EF_EQUIPMENT_QA_RUN = $RunName
    Copy-Item -LiteralPath $env:CODEX_MIGRATION_PIE_SCRIPT -Destination (Join-Path $qaDirectory 'fixture.py')
    [ordered]@{
        script=$env:CODEX_MIGRATION_PIE_SCRIPT
        script_sha256=(Get-FileHash -LiteralPath $env:CODEX_MIGRATION_PIE_SCRIPT -Algorithm SHA256).Hash
        commit=(& git -C $root rev-parse HEAD)
        working_tree=@(& git -C $root status --short)
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $qaDirectory 'launch.json') -Encoding UTF8
    $logPath = Join-Path $root "Saved/EquipmentQA/$RunName/editor.log"
    & (Join-Path $root 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -Wait -AdditionalArguments ('-NoSplash -NoP4 -Unattended -DisablePlugins=BpGeneratorUltimate -d3d12 -sm6 -ResX=1920 -ResY=1080 -ABSLOG="{0}"' -f $logPath)
} finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldEnable
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:EF_EQUIPMENT_QA_RUN = $oldRun
    Restore-UnrealQAUserSaves -Snapshot $saveSnapshot -OutputDirectory $qaDirectory
}
$resultPath = Join-Path $qaDirectory 'result.json'
if (!(Test-Path -LiteralPath $resultPath)) { throw "Missing current PIE result: $resultPath" }
$result = Get-Content -Raw -LiteralPath $resultPath | ConvertFrom-Json
if ($result.status -ne 'PASS') { throw "PIE fixture failed: $resultPath ($($result.error))" }
Write-Host "Equipment PIE: PASS ($resultPath)"
