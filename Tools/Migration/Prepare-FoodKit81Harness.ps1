[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FOODKIT57_HARNESS_GATE_FAIL: $Message" }
}

function Get-RelativePath([string]$Root, [string]$Path) {
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    Assert-True ($fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) "Path escapes tree root: $fullPath"
    return $fullPath.Substring($fullRoot.Length).Replace('\', '/')
}

function Get-TreeRows([string]$Root) {
    return @(
        Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName | ForEach-Object {
            [ordered]@{
                relative = Get-RelativePath $Root $_.FullName
                length = [int64]$_.Length
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
            }
        }
    )
}

function Get-TreeFingerprint($Rows) {
    $text = (($Rows | ForEach-Object { '{0}|{1}|{2}' -f $_.relative, $_.length, $_.sha256 }) -join "`n")
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($text)
        return (-join ($algorithm.ComputeHash($bytes) | ForEach-Object { $_.ToString('X2') }))
    }
    finally { $algorithm.Dispose() }
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourcePack = Join-Path $source 'Content\Food_Props_Kit'
$phaseRoot = Join-Path $target 'Saved\Migration\FoodKitAlcohol'
$harnessRoot = Join-Path $phaseRoot 'FoodKit81Harness57'
$harnessContent = Join-Path $harnessRoot 'Content'
$stagedPack = Join-Path $harnessContent 'Food_Props_Kit'
$harnessProject = Join-Path $harnessRoot 'FoodKit81Harness57.uproject'
$manifestPath = Join-Path $phaseRoot 'FoodKit81Manifest.json'
$receiptPath = Join-Path $phaseRoot 'FoodKit81HarnessReceipt.json'

Assert-True (-not $source.Equals($target, [StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source descriptor is absent.'
Assert-True (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf) 'Target descriptor is absent.'
Assert-True (Test-Path -LiteralPath $sourcePack -PathType Container) 'Source Food_Props_Kit is absent.'
Assert-True (Test-Path -LiteralPath $manifestPath -PathType Leaf) '81-entry manifest is absent.'
Assert-True ([System.IO.Path]::GetFullPath($harnessRoot).StartsWith([System.IO.Path]::GetFullPath((Join-Path $target 'Saved\Migration')) + '\', [StringComparison]::OrdinalIgnoreCase)) 'Harness escapes target Saved/Migration.'

$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
Assert-True ([string]$manifest.status -eq 'FOOD_KIT_81_MANIFEST_PASS') 'Manifest status is not PASS.'
Assert-True ([int]$manifest.entry_count -eq 81) 'Manifest count is not 81.'
Assert-True (@($manifest.entries.mesh_package | Sort-Object -Unique).Count -eq 81) 'Manifest mesh set is not unique.'

$sourceRowsBefore = Get-TreeRows $sourcePack
$sourceFingerprintBefore = Get-TreeFingerprint $sourceRowsBefore
$sourceBytes = [int64]0
foreach ($row in $sourceRowsBefore) { $sourceBytes += [int64]$row['length'] }
Assert-True ($sourceRowsBefore.Count -eq 699) "Expected 699 source pack files, found $($sourceRowsBefore.Count)."

if (Test-Path -LiteralPath $harnessRoot) {
    $resolved = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True ($resolved.StartsWith([System.IO.Path]::GetFullPath($phaseRoot) + '\', [StringComparison]::OrdinalIgnoreCase)) 'Existing harness escapes phase root.'
    Assert-True ((Split-Path -Leaf $resolved) -eq 'FoodKit81Harness57') 'Unexpected harness leaf.'
    $reparse = @(Get-ChildItem -LiteralPath $resolved -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
    Assert-True ($reparse.Count -eq 0) 'Existing harness contains reparse points.'
    $quarantine = Join-Path $phaseRoot ('Quarantine\FoodKit81Harness57_' + [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff'))
    New-Item -ItemType Directory -Path (Split-Path -Parent $quarantine) -Force | Out-Null
    Move-Item -LiteralPath $resolved -Destination $quarantine
}

New-Item -ItemType Directory -Path $harnessContent -Force | Out-Null
Copy-Item -LiteralPath $sourcePack -Destination $harnessContent -Recurse

$stagedRows = Get-TreeRows $stagedPack
$stagedFingerprint = Get-TreeFingerprint $stagedRows
Assert-True ($stagedRows.Count -eq $sourceRowsBefore.Count) 'Staged file count differs.'
Assert-True ($stagedFingerprint -eq $sourceFingerprintBefore) 'Staged fingerprint differs from source.'
$reparse = @(Get-ChildItem -LiteralPath $harnessRoot -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
Assert-True ($reparse.Count -eq 0) 'Harness contains a reparse point.'

[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Detached UE 5.7 AssetTools harness for the exact 81 food meshes and their dependency closure.'
    Plugins = @([ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true })
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

$sourceRowsAfter = Get-TreeRows $sourcePack
$sourceFingerprintAfter = Get-TreeFingerprint $sourceRowsAfter
Assert-True ($sourceFingerprintAfter -eq $sourceFingerprintBefore) 'Source pack changed while staging.'

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_FOODKIT81_HARNESS_PASS'
    source_root = $source
    target_root = $target
    source_pack = $sourcePack
    harness_root = $harnessRoot
    harness_project = $harnessProject
    harness_content = $harnessContent
    staged_pack = $stagedPack
    manifest = $manifestPath
    manifest_fingerprint = [string]$manifest.fingerprint
    manifest_entry_count = 81
    staged_file_count = $stagedRows.Count
    staged_bytes = $sourceBytes
    source_fingerprint_before = $sourceFingerprintBefore
    source_fingerprint_after = $sourceFingerprintAfter
    staged_fingerprint = $stagedFingerprint
    source_tree_mounted = $false
    junctions_or_symlinks_present = $false
    source_package_saves = 0
    target_content_writes = 0
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "ISOLATED_FOODKIT81_HARNESS_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
