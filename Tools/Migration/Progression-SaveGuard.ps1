function Invoke-ProgressionSaveGuard {
 param([string]$ProjectRoot, [string]$EvidenceDirectory, [scriptblock]$Action)
 $saveRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot 'Saved\SaveGames')).TrimEnd('\') + '\'
 $backupRoot = Join-Path $EvidenceDirectory 'BeforeSaveGames'
 $afterRoot = Join-Path $EvidenceDirectory 'AfterSaveGames'
 [void][IO.Directory]::CreateDirectory($backupRoot)
 $before = @{}
 if (Test-Path -LiteralPath $saveRoot) {
  foreach ($file in Get-ChildItem -LiteralPath $saveRoot -Filter '*.sav' -File -Recurse) {
   $relative = $file.FullName.Substring($saveRoot.Length)
   $destination = Join-Path $backupRoot $relative
   [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
   Copy-Item -LiteralPath $file.FullName -Destination $destination
   $before[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
  }
 }
 try { & $Action }
 finally {
  if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) {
   throw "Editor still running; originals preserved at $backupRoot. Restore only after it exits."
  }
  $changes = @()
  if (Test-Path -LiteralPath $saveRoot) {
   foreach ($file in Get-ChildItem -LiteralPath $saveRoot -Filter '*.sav' -File -Recurse) {
    $resolved = [IO.Path]::GetFullPath($file.FullName)
    if (!$resolved.StartsWith($saveRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Save path escaped root.' }
    $relative = $resolved.Substring($saveRoot.Length)
    $hash = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
    if (!$before.ContainsKey($relative) -or $hash -ne $before[$relative]) {
     $destination = Join-Path $afterRoot $relative
     [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
     # Preserve generated output before restoring originals; never discard it.
     Move-Item -LiteralPath $resolved -Destination $destination
     $changes += $relative
    }
   }
  }
  foreach ($relative in $before.Keys) {
   $destination = Join-Path $saveRoot $relative
   [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
   Copy-Item -LiteralPath (Join-Path $backupRoot $relative) -Destination $destination -Force
   if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $before[$relative]) { throw 'Save restore hash mismatch.' }
  }
  @{ restored = $true; original_hashes = $before; isolated_outputs = $changes } |
   ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'SaveGuard.json') -Encoding UTF8
 }
}
