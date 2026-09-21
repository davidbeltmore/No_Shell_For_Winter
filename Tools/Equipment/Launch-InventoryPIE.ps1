param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName = 'PIE_02')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) { throw 'Inventory PIE requires the target editor to be closed.' }
$oldEnable = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldRun = $env:EF_EQUIPMENT_QA_RUN
$qaDirectory = Join-Path $root "Saved/EquipmentQA/$RunName"
$saveDirectory = Join-Path $root 'Saved/SaveGames'
$backupDirectory = Join-Path $qaDirectory 'UserSaveBackup'
if (Test-Path -LiteralPath $backupDirectory) { throw 'Use a fresh run: save backups must not be overwritten.' }
New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
$saveSnapshot = @(Get-ChildItem -LiteralPath $saveDirectory -File -Filter '*.sav' -ErrorAction SilentlyContinue | ForEach-Object {
    $backup = Join-Path $backupDirectory $_.Name
    Copy-Item -LiteralPath $_.FullName -Destination $backup
    [pscustomobject]@{ Path=$_.FullName; Backup=$backup; Hash=(Get-FileHash -LiteralPath $backup).Hash }
})
$saveSnapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $qaDirectory 'user_save_before.json')
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $PSScriptRoot 'Validate-InventoryPIE.py'
    $env:EF_EQUIPMENT_QA_RUN = $RunName
    $logPath = Join-Path $root "Saved/EquipmentQA/$RunName/editor.log"
    & (Join-Path $root 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -Wait -AdditionalArguments ('-NoSplash -NoP4 -Unattended -DisablePlugins=BpGeneratorUltimate -d3d12 -sm6 -ResX=1920 -ResY=1080 -ABSLOG="{0}"' -f $logPath)
} finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldEnable
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:EF_EQUIPMENT_QA_RUN = $oldRun
    # Existing gameplay subsystems save independently when PIE ends. Preserve
    # their QA output, then restore only the exact pre-run user files after exit.
    if (-not @(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) {
        $restored = @()
        foreach ($entry in $saveSnapshot) {
            if (-not (Test-Path -LiteralPath $entry.Path) -or (Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash) {
                if (Test-Path -LiteralPath $entry.Path) { Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $backupDirectory ([IO.Path]::GetFileName($entry.Path) + '.qa-output')) }
                Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path -Force
                $restored += $entry.Path
            }
            if ((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash) { throw "User save recovery failed: $($entry.Path)" }
        }
        [pscustomobject]@{ Status='PASS'; Checked=$saveSnapshot.Count; Restored=$restored } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $qaDirectory 'user_save_after.json')
    } else { throw 'QA editor is still active; preserve UserSaveBackup and restore its exact files after safe shutdown.' }
}
