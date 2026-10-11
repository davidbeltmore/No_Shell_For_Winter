param([ValidateSet('Backup','Restore')][string]$Mode = 'Backup', [string]$Session = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $root 'Tools/Migration/QAUserSaveGuard.ps1')
if ([string]::IsNullOrWhiteSpace($Session)) {
    if ($Mode -eq 'Restore') { $Session = (Get-Content -LiteralPath (Join-Path $root 'Saved/NPCOutfits/latest-session.txt') -Raw).Trim() }
    else { $Session = Join-Path $root ('Saved/NPCOutfits/Session_' + (Get-Date -Format 'yyyyMMdd_HHmmss')) }
}
if ($Mode -eq 'Backup') {
    [void][IO.Directory]::CreateDirectory($Session)
    $snapshot = @(Backup-UnrealQAUserSaves -ProjectRoot $root -OutputDirectory $Session)
    Copy-Item -LiteralPath (Join-Path $root 'Content/_Game/Hub/HUB.umap') -Destination (Join-Path $Session 'HUB-user-saved.umap')
    Set-Content -LiteralPath (Join-Path $root 'Saved/NPCOutfits/latest-session.txt') -Value $Session
    @{status='PASS';session=$Session;save_count=$snapshot.Count} | ConvertTo-Json
} else {
    $snapshot = Get-Content -LiteralPath (Join-Path $Session 'user_save_before.json') -Raw | ConvertFrom-Json
    Restore-UnrealQAUserSaves -Snapshot @($snapshot) -OutputDirectory $Session
    Get-Content -LiteralPath (Join-Path $Session 'user_save_after.json')
}
