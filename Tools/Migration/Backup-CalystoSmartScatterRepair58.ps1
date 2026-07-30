[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$EvidenceRoot = 'D:\Projects UE5\NoShellForWinter\Saved\Migration\Phase4\BulkProjectContent\BULKREST_20260713_2015\CalystoSmartScatterRepair'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Snapshot {
    param([Parameter(Mandatory)][string]$Path)
    $item = Get-Item -LiteralPath $Path
    [ordered]@{
        file = $item.FullName
        length = $item.Length
        last_write_utc = $item.LastWriteTimeUtc.ToString('o')
        sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}

$project = [IO.Path]::GetFullPath($ProjectRoot)
$source = [IO.Path]::GetFullPath($SourceRoot)
$evidence = [IO.Path]::GetFullPath($EvidenceRoot)
if ($project.TrimEnd('\') -ieq $source.TrimEnd('\')) {
    throw 'Source and target roots overlap.'
}
if (-not (Test-Path -LiteralPath (Join-Path $project 'NoShellForWinter.uproject') -PathType Leaf)) {
    throw 'NoShellForWinter target project was not found.'
}
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw 'Read-only source project was not found.'
}

$relativeFiles = @(
    'Content\Calysto\Shared\Data\Structure\ST_SmartScatter.uasset',
    'Content\Calysto\Shared\Data\Structure\PDA_VegetationCalysto.uasset'
)
if ($relativeFiles.Count -ne 2 -or ($relativeFiles | Where-Object { $_ -like 'Content\ExportedAnimations\*' })) {
    throw 'The guarded two-package cohort is invalid.'
}

$backupRoot = Join-Path $evidence 'AssetBackup_PreRepair'
$evidenceFile = Join-Path $evidence 'CalystoSmartScatterPreRepairBackup.json'
if (Test-Path -LiteralPath $backupRoot) {
    throw "Backup already exists and will not be overwritten: $backupRoot"
}
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null

$targetBefore = [ordered]@{}
$sourceBefore = [ordered]@{}
$backupAfter = [ordered]@{}
foreach ($relative in $relativeFiles) {
    $targetFile = Join-Path $project $relative
    $sourceFile = Join-Path $source $relative
    if (-not (Test-Path -LiteralPath $targetFile -PathType Leaf)) {
        throw "Missing target package: $targetFile"
    }
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Missing source package: $sourceFile"
    }
    $targetBefore[$relative] = Get-Snapshot -Path $targetFile
    $sourceBefore[$relative] = Get-Snapshot -Path $sourceFile
    $backupFile = Join-Path $backupRoot $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($backupFile)) -Force | Out-Null
    Copy-Item -LiteralPath $targetFile -Destination $backupFile
    $backupAfter[$relative] = Get-Snapshot -Path $backupFile
    if ($backupAfter[$relative].sha256 -ne $targetBefore[$relative].sha256) {
        throw "Backup hash mismatch: $relative"
    }
}

$sourceAfter = [ordered]@{}
foreach ($relative in $relativeFiles) {
    $sourceAfter[$relative] = Get-Snapshot -Path (Join-Path $source $relative)
    if ($sourceAfter[$relative].sha256 -ne $sourceBefore[$relative].sha256) {
        throw "Source package changed during backup: $relative"
    }
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'CALYSTO_SMART_SCATTER_PRE_REPAIR_BACKUP_PASS'
    target_root = $project
    source_root_read_only = $source
    relative_files = $relativeFiles
    package_count = 2
    target_before = $targetBefore
    backup_after = $backupAfter
    source_before = $sourceBefore
    source_after = $sourceAfter
    source_hashes_unchanged = $true
    backup_hashes_match_target = $true
    exported_animations_excluded = $true
}
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $evidenceFile -Encoding UTF8
Write-Host "CODEX_CALYSTO_SMART_SCATTER_BACKUP_PASS: $evidenceFile"
