param([string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
if ($root -ne 'D:\Projects UE5\NoShellForWinter') { throw 'This archive belongs only to the authorized target project.' }
if (@(Get-Process UnrealEditor,UnrealEditor-Cmd,NoShellForWinter -ErrorAction SilentlyContinue).Count) { throw 'Close game/editor before archiving active saves.' }
$saveRoot=[IO.Path]::GetFullPath((Join-Path $root 'Saved/SaveGames')).TrimEnd('\')+'\'
$backupRoot=[IO.Path]::GetFullPath((Join-Path $root 'Saved/Backups/ProgressionV2_20260928_104559')).TrimEnd('\')+'\'
$manifest=Get-Content -LiteralPath ($backupRoot+'manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest) {
 $backup=[IO.Path]::GetFullPath($entry.Backup)
 if (!$backup.StartsWith($backupRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Backup path escaped its root.' }
 if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $entry.SHA256) { throw "Backup hash mismatch: $backup" }
}
$archiveRoot=$backupRoot+'ArchivedActiveSlots\'
[void][IO.Directory]::CreateDirectory($archiveRoot)
$receipt=@()
foreach ($name in @('ProjectCharacterBackground.sav','ProjectInnerDoctrineV1.sav','ProjectIntimacy.sav','ProjectTattooShop_v1.sav','SaveMetadata.sav','test.sav')) {
 $active=[IO.Path]::GetFullPath((Join-Path $saveRoot $name))
 $archived=[IO.Path]::GetFullPath((Join-Path $archiveRoot $name))
 if (!$active.StartsWith($saveRoot,[StringComparison]::OrdinalIgnoreCase) -or !$archived.StartsWith($archiveRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Archive path escaped its root.' }
 $entry=@($manifest | Where-Object { [IO.Path]::GetFullPath($_.Source) -eq $active })
 if ($entry.Count -ne 1) { throw "No unique original backup: $active" }
 if (Test-Path -LiteralPath $active) {
  if ((Get-FileHash -LiteralPath $active -Algorithm SHA256).Hash -ne $entry[0].SHA256) { throw "Active save changed since backup: $active" }
  if (Test-Path -LiteralPath $archived) { throw "Archive already exists while original is active: $archived" }
  Move-Item -LiteralPath $active -Destination $archived
 }
 if ((Get-FileHash -LiteralPath $archived -Algorithm SHA256).Hash -ne $entry[0].SHA256) { throw "Archive verification failed: $archived" }
 $receipt+=@{Original=$active;Archived=$archived;SHA256=$entry[0].SHA256}
}
@{utc=[DateTime]::UtcNow.ToString('o');archived=$receipt;cosmetic_presets_retained=(Test-Path -LiteralPath ($saveRoot+'CharacterCreationPresets.sav'));conversion_applied=$false} |
 ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($backupRoot+'archive_receipt.json') -Encoding UTF8
Write-Host "Six old active saves archived byte-exact; cosmetic presets retained: $archiveRoot"
