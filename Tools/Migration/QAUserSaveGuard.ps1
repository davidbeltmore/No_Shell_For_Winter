# Dot-source from an owned PIE runner before launching its editor.
# Existing gameplay subsystems can save independently of isolated ALS metadata.
function Backup-UnrealQAUserSaves {
    param([Parameter(Mandatory)][string]$ProjectRoot, [Parameter(Mandatory)][string]$OutputDirectory)
    $backupDirectory = Join-Path $OutputDirectory 'UserSaveBackup'
    if (Test-Path -LiteralPath $backupDirectory) { throw 'Use a fresh QA run: user-save backups must not be overwritten.' }
    New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
    $snapshot = @(Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Saved/SaveGames') -File -Filter '*.sav' -ErrorAction SilentlyContinue | ForEach-Object {
        $backup = Join-Path $backupDirectory $_.Name
        Copy-Item -LiteralPath $_.FullName -Destination $backup
        [pscustomobject]@{ Path=$_.FullName; Backup=$backup; Hash=(Get-FileHash -LiteralPath $backup).Hash }
    })
    ConvertTo-Json -InputObject $snapshot | Set-Content -LiteralPath (Join-Path $OutputDirectory 'user_save_before.json')
    return $snapshot
}

function Restore-UnrealQAUserSaves {
    param([Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Snapshot, [Parameter(Mandatory)][string]$OutputDirectory)
    if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) {
        throw 'QA editor is still active; preserve UserSaveBackup and restore its exact files after safe shutdown.'
    }
    $restored = @()
    foreach ($entry in $Snapshot) {
        if ((Get-FileHash -LiteralPath $entry.Backup).Hash -ne $entry.Hash) { throw "QA backup integrity failed: $($entry.Backup)" }
        if (-not (Test-Path -LiteralPath $entry.Path) -or (Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash) {
            if (Test-Path -LiteralPath $entry.Path) { Copy-Item -LiteralPath $entry.Path -Destination ($entry.Backup + '.qa-output') }
            Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path -Force
            $restored += $entry.Path
        }
        if ((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash) { throw "User save recovery failed: $($entry.Path)" }
    }
    [pscustomobject]@{ Status='PASS'; Checked=$Snapshot.Count; Restored=$restored } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'user_save_after.json')
}
