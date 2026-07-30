[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "DUNGEON_GENERATION57_HARNESS_GATE_FAIL: $Message"
    }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith(
        $fullRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-NoReparsePoints {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $items = @((Get-Item -LiteralPath $Path -Force)) + @(Get-ChildItem -LiteralPath $Path -Recurse -Force)
    foreach ($item in $items) {
        $isReparse = ([int]$item.Attributes -band [int][System.IO.FileAttributes]::ReparsePoint) -ne 0
        Assert-True (-not $isReparse) "Reparse point, junction, or symlink is forbidden in the harness: $($item.FullName)"
    }
}

$package = '/Game/Procedural/Maps/DungeonGeneration'
$relativeMap = 'Procedural\Maps\DungeonGeneration.umap'
$expectedLength = [int64]58016
$expectedSha256 = 'B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F'
$expectedDependencies = @(
    '/Engine/EngineMaterials/WorldGridMaterial',
    '/Script/NavigationSystem',
    '/Script/PCG'
)
$expectedReferencerPackages = @('/Game/Procedural/DoorToLevel')
$expectedActorClasses = @('PlayerStart', 'NavMeshBoundsVolume', 'PCGWorldActor')

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$sourceMap = Join-Path $sourceContent $relativeMap
$targetMap = Join-Path $targetContent $relativeMap
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4\DungeonGeneration'
$harnessRoot = Join-Path $phaseRoot 'DungeonGeneration57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessMap = Join-Path $harnessContent $relativeMap
$harnessProject = Join-Path $harnessRoot 'DungeonGeneration57Harness.uproject'
$receiptPath = Join-Path $phaseRoot 'DungeonGeneration57HarnessReceipt.json'
$manifestPath = Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv'
$dependencyEvidencePath = Join-Path $target 'Saved\Migration\Phase3\EFProceduralDependencies57.json'
$registryEvidencePath = Join-Path $target 'Saved\Migration\Phase2\SourceAssetRegistry57.json'
$safetyScript = Join-Path $target 'Tools\Migration\Test-DungeonGenerationMigrationGates.ps1'
$preStageSafetyEvidence = Join-Path $phaseRoot 'Gates\PRE_STAGE_SafetyGate.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root (Join-Path $target 'Saved\Migration')) 'Harness escapes target Saved/Migration.'
Assert-True (-not (Test-IsUnderRoot -Path $harnessRoot -Root $source)) 'Harness unexpectedly lives under the read-only source.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'
Assert-True (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf) 'Target project descriptor is absent.'
Assert-True (Test-Path -LiteralPath $safetyScript -PathType Leaf) 'Batch safety-gate script is absent.'

& $safetyScript -Stage PRE_STAGE -SourceRoot $source -TargetRoot $target
Assert-True (Test-Path -LiteralPath $preStageSafetyEvidence -PathType Leaf) 'PRE_STAGE safety evidence was not produced.'
$preStageSafety = Get-Content -Raw -LiteralPath $preStageSafetyEvidence | ConvertFrom-Json
Assert-True ([string]$preStageSafety.status -eq 'DUNGEON_GENERATION_SOURCE_PROTECTED_SAFETY_PASS') 'PRE_STAGE source/protected evidence is not PASS.'

Assert-True (Test-Path -LiteralPath $sourceMap -PathType Leaf) 'Source map is absent.'
Assert-True ((Get-Item -LiteralPath $sourceMap).Length -eq $expectedLength) 'Source map length differs from the frozen baseline.'
Assert-True ((Get-Sha256 -Path $sourceMap) -eq $expectedSha256) 'Source map hash differs from the frozen baseline.'

foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
    $sidecar = [System.IO.Path]::ChangeExtension($sourceMap, $extension)
    Assert-True (-not (Test-Path -LiteralPath $sidecar)) "Unexpected source package sidecar exists: $sidecar"
}
$sourceForbiddenPaths = @(
    (Join-Path $sourceContent 'Procedural\Maps\DungeonGeneration_BuiltData.uasset'),
    (Join-Path $sourceContent '__ExternalActors__\Procedural\Maps\DungeonGeneration'),
    (Join-Path $sourceContent '__ExternalObjects__\Procedural\Maps\DungeonGeneration')
)
foreach ($path in $sourceForbiddenPaths) {
    Assert-True (-not (Test-Path -LiteralPath $path)) "Map is not a one-package batch; related source content exists: $path"
}
Assert-True (-not (Test-Path -LiteralPath $targetMap)) 'Target map collision exists.'

Assert-True (Test-Path -LiteralPath $manifestPath -PathType Leaf) 'Migration manifest is absent.'
$manifestRows = @(Import-Csv -LiteralPath $manifestPath | Where-Object PackageName -eq $package)
Assert-True ($manifestRows.Count -eq 1) 'Expected exactly one manifest row for DungeonGeneration.'
$manifestRow = $manifestRows[0]
Assert-True ([string]$manifestRow.Presence -eq 'SOURCE_ONLY') 'Manifest no longer classifies DungeonGeneration as SOURCE_ONLY.'
Assert-True ([int64]$manifestRow.SourceLength -eq $expectedLength) 'Manifest source length differs.'
Assert-True ([string]$manifestRow.SourceSHA256 -eq $expectedSha256) 'Manifest source hash differs.'
Assert-True ([string]$manifestRow.Action -eq 'MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE') 'Manifest action does not authorize the gated AssetTools path.'

Assert-True (Test-Path -LiteralPath $dependencyEvidencePath -PathType Leaf) 'UE 5.7 registry dependency evidence is absent.'
$dependencyEvidence = Get-Content -Raw -LiteralPath $dependencyEvidencePath | ConvertFrom-Json
$seedRows = @($dependencyEvidence.seeds | Where-Object package -eq $package)
Assert-True ($seedRows.Count -eq 1) 'UE 5.7 registry evidence does not contain exactly one DungeonGeneration seed.'
$actualDependencies = @($seedRows[0].direct_dependencies | Sort-Object -Unique)
$expectedDependenciesSorted = @($expectedDependencies | Sort-Object -Unique)
$dependencyDelta = @(Compare-Object -ReferenceObject $expectedDependenciesSorted -DifferenceObject $actualDependencies -SyncWindow 0)
Assert-True ($dependencyDelta.Count -eq 0) 'DungeonGeneration dependency set differs from the frozen three-package closure.'
Assert-True (@($actualDependencies | Where-Object { $_ -like '/Game/*' }).Count -eq 0) 'DungeonGeneration unexpectedly depends on project content.'

Assert-True (Test-Path -LiteralPath $registryEvidencePath -PathType Leaf) 'Phase 2 source Asset Registry evidence is absent.'
$registryEvidence = Get-Content -Raw -LiteralPath $registryEvidencePath | ConvertFrom-Json
$referencerPackages = @(
    $registryEvidence.assets |
        Where-Object { $_.dependencies -contains $package } |
        ForEach-Object { [string]$_.package_name } |
        Sort-Object -Unique
)
$referencerDelta = @(Compare-Object -ReferenceObject $expectedReferencerPackages -DifferenceObject $referencerPackages -SyncWindow 0)
Assert-True ($referencerDelta.Count -eq 0) 'Source referencer set differs; stop and re-audit the batch.'

$ascii = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($sourceMap))
$partitionMarkers = @('WorldPartition', 'ExternalActors', 'ExternalObjects', 'LevelInstance', 'HLOD')
$foundPartitionMarkers = @($partitionMarkers | Where-Object { $ascii.Contains($_) })
Assert-True ($foundPartitionMarkers.Count -eq 0) 'World Partition, external package, LevelInstance, or HLOD marker found in the source map.'

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $phaseRoot) 'Refusing to clean a harness outside the batch staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'DungeonGeneration57Harness') 'Unexpected harness directory name.'
    Assert-NoReparsePoints -Path $resolvedHarness
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

New-Item -ItemType Directory -Path (Split-Path -Parent $harnessMap) -Force | Out-Null
Copy-Item -LiteralPath $sourceMap -Destination $harnessMap
Assert-True ((Get-Item -LiteralPath $harnessMap).Length -eq $expectedLength) 'Staged map length differs.'
Assert-True ((Get-Sha256 -Path $harnessMap) -eq $expectedSha256) 'Staged map hash differs.'
Assert-NoReparsePoints -Path $harnessRoot

[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated UE 5.7 load-and-AssetTools harness for exactly /Game/Procedural/Maps/DungeonGeneration.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'PCG'; Enabled = $true }
    )
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

Assert-True ((Get-Item -LiteralPath $sourceMap).Length -eq $expectedLength) 'Source map length changed while staging.'
Assert-True ((Get-Sha256 -Path $sourceMap) -eq $expectedSha256) 'Source map hash changed while staging.'
Assert-True (-not (Test-Path -LiteralPath $targetMap)) 'Target map appeared while staging.'

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_DUNGEON_GENERATION57_HARNESS_PASS'
    package_count = 1
    package = $package
    source_root = $source
    target_root = $target
    harness_root = $harnessRoot
    harness_content = $harnessContent
    harness_project = $harnessProject
    source_map = [ordered]@{
        file = $sourceMap
        length = $expectedLength
        sha256 = $expectedSha256
        last_write_utc = (Get-Item -LiteralPath $sourceMap).LastWriteTimeUtc.ToString('o')
    }
    staged_map = [ordered]@{
        file = $harnessMap
        length = $expectedLength
        sha256 = $expectedSha256
    }
    destination_map = $targetMap
    direct_dependencies = $actualDependencies
    game_dependency_count = 0
    source_referencer_packages = $referencerPackages
    source_referencer_direction = 'INVERSE_ONLY_NOT_REQUIRED_BY_MAP'
    expected_actor_classes_for_load_gate = $expectedActorClasses
    source_sidecars_absent = @('.uexp', '.ubulk', '.uptnl', 'DungeonGeneration_BuiltData.uasset')
    source_external_actor_root_absent = $true
    source_external_object_root_absent = $true
    static_world_partition_markers = $foundPartitionMarkers
    runtime_world_partition_probe = 'PENDING_UE57_LOAD'
    target_collision_absent = $true
    pre_stage_safety = [ordered]@{
        result = 'PASS'
        evidence = $preStageSafetyEvidence
        evidence_sha256 = Get-Sha256 -Path $preStageSafetyEvidence
    }
    source_tree_mounted = $false
    junctions_or_symlinks_present = $false
    source_package_saves = 0
    target_content_writes = 0
    policy = 'One exact-hash map is copied only into target Saved/Migration for UE 5.7 inspection. Live target Content may be populated later only by Unreal AssetTools after the read-only load gate passes.'
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "DUNGEON_GENERATION57_HARNESS_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
