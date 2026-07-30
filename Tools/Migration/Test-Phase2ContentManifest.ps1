[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$ManifestPath = 'D:\Projects UE5\NoShellForWinter\Docs\Migration\04_Content_Migration_Manifest.csv',
    [string]$SummaryPath = 'D:\Projects UE5\NoShellForWinter\Docs\Migration\Evidence\Phase2_Content_Manifest_Summary.json'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw "PHASE2_MANIFEST_GATE_FAIL: $Message"
    }
}

function ConvertTo-PackageName {
    param(
        [string]$ContentRoot,
        [System.IO.FileInfo]$File
    )
    $relative = $File.FullName.Substring($ContentRoot.Length).TrimStart('\')
    $extension = [System.IO.Path]::GetExtension($relative)
    Assert-True (-not [string]::IsNullOrEmpty($extension)) "Package file has no extension: $relative"
    $withoutExtension = $relative.Substring(0, $relative.Length - $extension.Length)
    return '/Game/' + $withoutExtension.Replace('\', '/')
}

function Get-FilesystemPackages {
    param([string]$Root)
    $contentRoot = (Resolve-Path -LiteralPath (Join-Path $Root 'Content')).Path.TrimEnd('\')
    return @(
        Get-ChildItem -LiteralPath $contentRoot -Recurse -File -Force |
            Where-Object { $_.Extension -ieq '.uasset' -or $_.Extension -ieq '.umap' } |
            ForEach-Object { ConvertTo-PackageName -ContentRoot $contentRoot -File $_ }
    )
}

Assert-True (Test-Path -LiteralPath $ManifestPath -PathType Leaf) "Missing manifest: $ManifestPath"
Assert-True (Test-Path -LiteralPath $SummaryPath -PathType Leaf) "Missing summary: $SummaryPath"

$rows = @(Import-Csv -LiteralPath $ManifestPath)
$summary = Get-Content -Raw -LiteralPath $SummaryPath | ConvertFrom-Json
$sourcePackages = @(Get-FilesystemPackages -Root $SourceRoot)
$targetPackages = @(Get-FilesystemPackages -Root $TargetRoot)
$filesystemUnion = @($sourcePackages + $targetPackages | Sort-Object -Unique)

Assert-True ($rows.Count -eq $filesystemUnion.Count) "Manifest rows $($rows.Count) do not match filesystem union $($filesystemUnion.Count)."
Assert-True (@($rows.PackageName | Sort-Object -Unique).Count -eq $rows.Count) 'PackageName values are not unique.'
Assert-True (@($rows | Where-Object { $_.PackageName -notmatch '^/Game/.+[^\.]$' }).Count -eq 0) 'A package name is malformed or ends in a period.'
Assert-True (@($rows | Where-Object { [string]::IsNullOrWhiteSpace($_.System) -or [string]::IsNullOrWhiteSpace($_.Classification) -or [string]::IsNullOrWhiteSpace($_.Authority) -or [string]::IsNullOrWhiteSpace($_.Action) }).Count -eq 0) 'A policy field is blank.'
Assert-True (@($rows | Where-Object { $_.Action -match '(?i)bulk.?copy|raw.?copy|robocopy|filesystem.?copy' }).Count -eq 0) 'A prohibited filesystem copy action was generated.'

$manifestLookup = @{}
foreach ($row in $rows) {
    $manifestLookup[$row.PackageName] = $row
}

$filesystemMissing = @($filesystemUnion | Where-Object { -not $manifestLookup.ContainsKey($_) })
Assert-True ($filesystemMissing.Count -eq 0) "Filesystem packages are absent from the manifest: $($filesystemMissing -join ', ')"

$playerPath = '/Game/FullSample/Player'
Assert-True ($manifestLookup.ContainsKey($playerPath)) "Missing protected Player row: $playerPath"
Assert-True ($manifestLookup[$playerPath].Classification -eq 'TARGET_AUTHORITATIVE_PLAYER_COLLISION') 'Player classification is not target-authoritative.'
Assert-True ($manifestLookup[$playerPath].Action -eq 'KEEP_TARGET_RECOMPOSE_SOURCE_CONTRACT') 'Player action would not preserve the UE 5.8 target.'

$retiredDialogue = @(
    '/Game/FullSample/Integrations/ATSIntegrations/Dialogue/SampleDialogueButton_WBP',
    '/Game/FullSample/Integrations/ATSIntegrations/Dialogue/ACF_Dialogue_WB'
)
foreach ($package in $retiredDialogue) {
    if ($manifestLookup.ContainsKey($package)) {
        Assert-True ($manifestLookup[$package].Classification -eq 'RETIRED_LEGACY_DIALOGUE') "Legacy dialogue package was not retired: $package"
        Assert-True ($manifestLookup[$package].Action -eq 'RETIRE_DO_NOT_MIGRATE') "Legacy dialogue package could be migrated: $package"
    }
}
Assert-True ($manifestLookup.ContainsKey($retiredDialogue[0])) 'The known orphan dialogue asset is absent from the union manifest.'

$dazRows = @($rows | Where-Object { $_.PackageName.StartsWith('/Game/DazToUnreal/', [System.StringComparison]::OrdinalIgnoreCase) })
Assert-True ($dazRows.Count -gt 0) 'No DazToUnreal rows were classified.'
Assert-True (@($dazRows | Where-Object { $_.Action -notin @('KEEP_TARGET_DAZ_AUDIT_DEPENDENCIES', 'DO_NOT_AUTO_MIGRATE_DAZ_REMAP_REFERENCES') }).Count -eq 0) 'A DazToUnreal package has an unsafe automatic action.'

Assert-True ([int]$summary.source_package_count -eq $sourcePackages.Count) 'Summary source package count does not match the filesystem.'
Assert-True ([int]$summary.target_package_count -eq $targetPackages.Count) 'Summary target package count does not match the filesystem.'
Assert-True ([int]$summary.union_package_count -eq $rows.Count) 'Summary union count does not match the manifest.'
Assert-True ([int]$summary.counts_by_presence.BOTH -eq @($rows | Where-Object Presence -eq 'BOTH').Count) 'Summary BOTH count is stale.'
Assert-True ([int]$summary.counts_by_presence.SOURCE_ONLY -eq @($rows | Where-Object Presence -eq 'SOURCE_ONLY').Count) 'Summary SOURCE_ONLY count is stale.'
Assert-True ([int]$summary.counts_by_presence.TARGET_ONLY -eq @($rows | Where-Object Presence -eq 'TARGET_ONLY').Count) 'Summary TARGET_ONLY count is stale.'
Assert-True ($summary.manifest_sha256 -eq (Get-FileHash -Algorithm SHA256 -LiteralPath $ManifestPath).Hash) 'Summary manifest SHA-256 is stale.'
Assert-True ([int]$summary.source_packages_missing_registry -le 200) "Source registry coverage is unexpectedly low: $($summary.source_packages_missing_registry) missing packages."
Assert-True ([int]$summary.target_packages_missing_registry -le 10) "Target registry coverage is unexpectedly low: $($summary.target_packages_missing_registry) missing packages."

[ordered]@{
    status = 'PHASE2_MANIFEST_GATE_PASS'
    manifest_rows = $rows.Count
    source_packages = $sourcePackages.Count
    target_packages = $targetPackages.Count
    union_packages = $filesystemUnion.Count
    source_registry_missing = [int]$summary.source_packages_missing_registry
    target_registry_missing = [int]$summary.target_packages_missing_registry
    daz_rows = $dazRows.Count
    manifest_sha256 = $summary.manifest_sha256
} | ConvertTo-Json -Depth 3
