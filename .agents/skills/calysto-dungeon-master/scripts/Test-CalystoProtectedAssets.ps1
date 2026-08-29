[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$BaselinePath = '',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRootFull = [System.IO.Path]::GetFullPath($ProjectRoot)
$uproject = Join-Path $projectRootFull 'NoShellForWinter.uproject'
if (!(Test-Path -LiteralPath $uproject)) {
    throw "Expected NoShellForWinter.uproject under $projectRootFull"
}

$relativePaths = @(
    'Content\Calysto\Dungeon\Blueprint\BP_MassiveDungeon.uasset',
    'Content\Calysto\Dungeon\Blueprint\Utility\BP_EndPoint.uasset',
    'Content\Calysto\Dungeon\Data\DataAsset\Dungeon\DA_DungeonMesh.uasset',
    'Content\Calysto\Dungeon\Data\DataAsset\Dungeon\DA_RoomTheme.uasset',
    'Content\Calysto\Dungeon\Data\DataAsset\Spawner\DA_DemoSpawner.uasset',
    'Content\Calysto\Dungeon\PCG\PCG_MassiveDungeonMaster.uasset',
    'Content\Calysto\Dungeon\PCG\PCG_MassiveDungeonShape.uasset',
    'Content\Calysto\Dungeon\PCG\Function\PCG_SpawnStartAndEnd.uasset',
    'Content\Calysto\Dungeon\PCG\Function\PCG_SetRoomTheme.uasset',
    'Content\Calysto\Dungeon\PCG\Function\PCG_DungeonSpawner.uasset',
    'Content\Calysto\Dungeon\PCG\Function\PCG_SetDungeonMesh.uasset',
    'Content\Procedural\Maps\DungeonGeneration.umap',
    'Content\Procedural\DoorToLevel.uasset'
)

$records = [ordered]@{}
foreach ($relativePath in $relativePaths) {
    $fullPath = Join-Path $projectRootFull $relativePath
    if (!(Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Protected Calysto path is absent: $fullPath"
    }
    $item = Get-Item -LiteralPath $fullPath
    $records[$relativePath.Replace('\', '/')] = [ordered]@{
        length = [int64]$item.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $fullPath).Hash.ToUpperInvariant()
    }
}

$mismatches = @()
if (-not [string]::IsNullOrWhiteSpace($BaselinePath)) {
    $baselineFull = [System.IO.Path]::GetFullPath($BaselinePath)
    $baseline = Get-Content -LiteralPath $baselineFull -Raw | ConvertFrom-Json
    foreach ($property in $records.GetEnumerator()) {
        $baselineRecord = $baseline.assets.PSObject.Properties[$property.Key].Value
        if ($null -eq $baselineRecord) {
            $mismatches += [ordered]@{ path = $property.Key; reason = 'MISSING_BASELINE' }
            continue
        }
        if ([string]$baselineRecord.sha256 -ne [string]$property.Value.sha256 -or
            [int64]$baselineRecord.length -ne [int64]$property.Value.length) {
            $mismatches += [ordered]@{
                path = $property.Key
                reason = 'HASH_OR_LENGTH'
                expected_sha256 = [string]$baselineRecord.sha256
                actual_sha256 = [string]$property.Value.sha256
                expected_length = [int64]$baselineRecord.length
                actual_length = [int64]$property.Value.length
            }
        }
    }
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    project = $uproject
    status = if ($mismatches.Count -eq 0) { 'PASS' } else { 'FAIL' }
    assets = $records
    mismatches = $mismatches
}
$json = $payload | ConvertTo-Json -Depth 10

if (-not [string]::IsNullOrWhiteSpace($OutputPath)) {
    $outputFull = [System.IO.Path]::GetFullPath($OutputPath)
    $savedRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRootFull 'Saved'))
    if (!$outputFull.StartsWith($savedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "OutputPath must stay under $savedRoot"
    }
    $parent = Split-Path -Parent $outputFull
    $null = New-Item -ItemType Directory -Path $parent -Force
    Set-Content -LiteralPath $outputFull -Value $json -Encoding UTF8
}

$json
if ($mismatches.Count -ne 0) {
    exit 1
}
