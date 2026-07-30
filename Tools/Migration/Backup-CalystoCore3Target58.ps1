[CmdletBinding()]
param(
    [string]$TargetRoot = $env:CODEX_CALYSTO_CORE3_TARGET_ROOT,
    [string]$BackupRoot = $env:CODEX_CALYSTO_CORE3_BACKUP_ROOT,
    [string]$EvidencePath = $env:CODEX_CALYSTO_CORE3_BACKUP_EVIDENCE
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-Snapshot([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    return [ordered]@{
        file = $item.FullName
        length = [int64]$item.Length
        last_write_utc = $item.LastWriteTimeUtc.ToString('o')
        sha256 = Get-Sha256 $item.FullName
    }
}

function Test-IsUnder([string]$Path, [string]$Root) {
    $pathFull = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    return $pathFull.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
        $pathFull.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)
}

if ([string]::IsNullOrWhiteSpace($TargetRoot) -or
    [string]::IsNullOrWhiteSpace($BackupRoot) -or
    [string]::IsNullOrWhiteSpace($EvidencePath)) {
    throw 'CODEX_CALYSTO_CORE3_TARGET_ROOT, CODEX_CALYSTO_CORE3_BACKUP_ROOT, and CODEX_CALYSTO_CORE3_BACKUP_EVIDENCE are required.'
}

$target = [IO.Path]::GetFullPath($TargetRoot)
$content = Join-Path $target 'Content'
$savedMigration = Join-Path $target 'Saved\Migration'
$backup = [IO.Path]::GetFullPath($BackupRoot)
$evidence = [IO.Path]::GetFullPath($EvidencePath)
if (-not (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf)) {
    throw 'Target root is not NoShellForWinter.'
}
if (-not (Test-IsUnder $backup $savedMigration) -or (Test-IsUnder $backup $content)) {
    throw 'BackupRoot must be below target Saved/Migration and outside live Content.'
}
if (-not (Test-IsUnder $evidence $savedMigration)) {
    throw 'Backup evidence must be below target Saved/Migration.'
}
if ($backup.Equals($savedMigration, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BackupRoot may not be the Saved/Migration root itself.'
}
if (Test-Path -LiteralPath $backup) {
    throw "BackupRoot already exists; use a fresh run directory: $backup"
}

$packages = @(
    '/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon',
    '/Game/Calysto/Shared/Blueprint/Controller/BP_CalystoController',
    '/Game/Calysto/Shared/Data/Structure/PDA_VegetationCalysto'
)
if ($packages.Count -ne 3 -or (@($packages | Select-Object -Unique)).Count -ne 3) {
    throw 'Guarded package cohort is not exactly three unique packages.'
}
if (@($packages | Where-Object { $_.ToLowerInvariant().StartsWith('/game/exportedanimations') }).Count -ne 0) {
    throw 'ExportedAnimations entered the backup cohort.'
}

$targetBefore = [ordered]@{}
foreach ($package in $packages) {
    $relative = $package.Substring('/Game/'.Length).Replace('/', '\') + '.uasset'
    $file = Join-Path $content $relative
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Target package is absent: $package"
    }
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        if (Test-Path -LiteralPath ([IO.Path]::ChangeExtension($file, $extension))) {
            throw "Target sidecar violates the exact three-file contract: $package"
        }
    }
    $targetBefore[$package] = Get-Snapshot $file
}

New-Item -ItemType Directory -Path $backup -Force | Out-Null
$backupFiles = [ordered]@{}
foreach ($package in $packages) {
    $source = $targetBefore[$package].file
    $relative = $package.Substring('/Game/'.Length).Replace('/', '\') + '.uasset'
    $destination = Join-Path (Join-Path $backup 'Content') $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $backupSnapshot = Get-Snapshot $destination
    if ($backupSnapshot.length -ne $targetBefore[$package].length -or
        $backupSnapshot.sha256 -ne $targetBefore[$package].sha256) {
        throw "Backup bytes differ from target: $package"
    }
    $backupFiles[$package] = $backupSnapshot
}

$targetAfter = [ordered]@{}
foreach ($package in $packages) {
    $targetAfter[$package] = Get-Snapshot $targetBefore[$package].file
    if ($targetAfter[$package].length -ne $targetBefore[$package].length -or
        $targetAfter[$package].sha256 -ne $targetBefore[$package].sha256) {
        throw "Live target changed while backing up: $package"
    }
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'EXACT_CALYSTO_CORE3_TARGET_BACKUP_PASS'
    target_root = $target
    backup_root = $backup
    package_count = 3
    packages = $packages
    target_before = $targetBefore
    backup_files = $backupFiles
    target_after = $targetAfter
    live_target_bytes_unchanged = $true
    exported_animations_excluded = $true
    source_project_touched = $false
}
New-Item -ItemType Directory -Path (Split-Path -Parent $evidence) -Force | Out-Null
$temporary = $evidence + '.tmp'
[IO.File]::WriteAllText(
    $temporary,
    (($payload | ConvertTo-Json -Depth 12) + [Environment]::NewLine),
    [Text.UTF8Encoding]::new($false)
)
Move-Item -LiteralPath $temporary -Destination $evidence -Force
Write-Output "CODEX_CALYSTO_CORE3_TARGET_BACKUP_PASS: $evidence"
