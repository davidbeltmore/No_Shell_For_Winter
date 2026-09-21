param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName = 'PIE_02', [switch]$SaveUIQA)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) { throw 'Inventory PIE requires the target editor to be closed.' }
$oldEnable = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldRun = $env:EF_EQUIPMENT_QA_RUN
$qaDirectory = Join-Path $root "Saved/EquipmentQA/$RunName"
. (Join-Path $root 'Tools/Migration/QAUserSaveGuard.ps1')
$saveSnapshot = @(Backup-UnrealQAUserSaves -ProjectRoot $root -OutputDirectory $qaDirectory)
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $PSScriptRoot $(if ($SaveUIQA) { 'Validate-SaveUI.py' } else { 'Validate-InventoryPIE.py' })
    $env:EF_EQUIPMENT_QA_RUN = $RunName
    $logPath = Join-Path $root "Saved/EquipmentQA/$RunName/editor.log"
    & (Join-Path $root 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -Wait -AdditionalArguments ('-NoSplash -NoP4 -Unattended -DisablePlugins=BpGeneratorUltimate -d3d12 -sm6 -ResX=1920 -ResY=1080 -ABSLOG="{0}"' -f $logPath)
} finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldEnable
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:EF_EQUIPMENT_QA_RUN = $oldRun
    Restore-UnrealQAUserSaves -Snapshot $saveSnapshot -OutputDirectory $qaDirectory
}
