param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Parameter(Mandatory = $true)]
    [string]$BackupRoot,
    [Parameter(Mandatory = $true)]
    [string]$EvidencePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-FullPath {
    param([string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-IsUnder {
    param([string]$Path, [string]$Root)
    $resolvedPath = (Resolve-FullPath $Path).TrimEnd('\')
    $resolvedRoot = (Resolve-FullPath $Root).TrimEnd('\')
    return $resolvedPath.StartsWith($resolvedRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-Snapshot {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        file = $item.FullName
        length = [int64]$item.Length
        mtime_utc = $item.LastWriteTimeUtc.ToString('o')
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash.ToUpperInvariant()
    }
}

$projectRoot = Resolve-FullPath $ProjectRoot
$projectFile = Join-Path $projectRoot 'NoShellForWinter.uproject'
$contentRoot = Resolve-FullPath (Join-Path $projectRoot 'Content')
$savedMigrationRoot = Resolve-FullPath (Join-Path $projectRoot 'Saved\Migration')
$backupRoot = Resolve-FullPath $BackupRoot
$evidencePath = Resolve-FullPath $EvidencePath

if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw "NoShellForWinter.uproject is absent: $projectFile"
}
if (-not (Test-IsUnder $backupRoot $savedMigrationRoot)) {
    throw "BackupRoot must be under target Saved\Migration: $backupRoot"
}
if (-not (Test-IsUnder $evidencePath $savedMigrationRoot)) {
    throw "EvidencePath must be under target Saved\Migration: $evidencePath"
}
if (Test-Path -LiteralPath $backupRoot) {
    throw "BackupRoot already exists; refusing overwrite: $backupRoot"
}
if (Test-Path -LiteralPath $evidencePath) {
    throw "EvidencePath already exists; refusing overwrite: $evidencePath"
}

$specs = @(
    [ordered]@{
        package = '/Game/Procedural/Blueprints/Altar'
        relative_file = 'Procedural\Blueprints\Altar.uasset'
    },
    [ordered]@{
        package = '/Game/_Game/Lockpicking/Locked'
        relative_file = '_Game\Lockpicking\Locked.uasset'
    }
)
if ($specs.Count -ne 2) {
    throw 'The guarded null-parent backup cohort must contain exactly two packages.'
}
foreach ($spec in $specs) {
    $normalizedPackage = ([string]$spec.package).TrimEnd('/').ToLowerInvariant()
    if ($normalizedPackage -eq '/game/exportedanimations' -or $normalizedPackage.StartsWith('/game/exportedanimations/')) {
        throw "ExportedAnimations entered the guarded cohort: $($spec.package)"
    }
}

New-Item -ItemType Directory -Path $backupRoot | Out-Null
$rows = @()
foreach ($spec in $specs) {
    $sourcePath = Resolve-FullPath (Join-Path $contentRoot $spec.relative_file)
    if (-not (Test-IsUnder $sourcePath $contentRoot)) {
        throw "Source package escaped target Content: $sourcePath"
    }
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Target package is absent: $sourcePath"
    }

    $backupPath = Resolve-FullPath (Join-Path $backupRoot (Join-Path 'Content' $spec.relative_file))
    if (-not (Test-IsUnder $backupPath $backupRoot)) {
        throw "Backup package escaped BackupRoot: $backupPath"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $backupPath) -Force | Out-Null

    $sourceSnapshot = Get-Snapshot $sourcePath
    Copy-Item -LiteralPath $sourcePath -Destination $backupPath
    $backupSnapshot = Get-Snapshot $backupPath
    if ($sourceSnapshot.length -ne $backupSnapshot.length -or $sourceSnapshot.sha256 -ne $backupSnapshot.sha256) {
        throw "Backup bytes differ for $($spec.package)"
    }

    $rows += [ordered]@{
        package = $spec.package
        source = $sourceSnapshot
        backup = $backupSnapshot
        bytes_match = $true
    }
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'NULL_PARENT_GAMEPLAY_BLUEPRINT_BACKUP_PASS'
    project = Resolve-FullPath $projectFile
    content_root = $contentRoot
    backup_root = $backupRoot
    package_count = $rows.Count
    packages = @($rows.package)
    files = $rows
    exported_animations_excluded = $true
    source_asset_write_operations = @()
    backup_copy_operations = @($rows.backup.file)
}

New-Item -ItemType Directory -Path (Split-Path -Parent $evidencePath) -Force | Out-Null
$temporaryPath = $evidencePath + '.tmp'
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporaryPath -Encoding utf8
Move-Item -LiteralPath $temporaryPath -Destination $evidencePath
Write-Output "CODEX_NULL_PARENT_GAMEPLAY_BACKUP_PASS: $evidencePath"
