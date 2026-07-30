[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{7,63}$')]
    [string]$RunId,
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "DOOR_TO_LEVEL57_HARNESS_GATE_FAIL: $Message"
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

function Get-TextSha256 {
    param([string]$Text)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
        return (-join ($algorithm.ComputeHash($bytes) | ForEach-Object { $_.ToString('X2') }))
    }
    finally {
        $algorithm.Dispose()
    }
}

function Assert-NoReparsePoints {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $items = @((Get-Item -LiteralPath $Path -Force)) + @(
        Get-ChildItem -LiteralPath $Path -Recurse -Force
    )
    foreach ($item in $items) {
        $isReparse = (
            [int]$item.Attributes -band [int][System.IO.FileAttributes]::ReparsePoint
        ) -ne 0
        Assert-True (-not $isReparse) "Reparse point, junction, or symlink is forbidden: $($item.FullName)"
    }
}

$expectedPackageCount = 4
$expectedSourceBytes = [int64]189704
$expectedSourceFingerprint = '4477D83F3722FA80674C18791BB2A85DCCE5DBE19FD57D17B52C20BE716212CC'
$legacyBlueprint = [pscustomobject]@{
    Package = '/Game/Procedural/DoorToLevel'
    RelativeFile = 'Procedural\DoorToLevel.uasset'
    Class = 'Blueprint'
    Length = [int64]53280
    Sha256 = '7EDF9F4A24D14F03AF2AE3F6A111696CF4AAC79052C225BCE90429D06935D016'
}
$assets = @(
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial'; RelativeFile = 'Calysto\Dungeon\Demo\LowPoly\Material\M_BaseMaterial.uasset'; Class = 'Material'; Length = [int64]15251; Sha256 = '1DA354F36752F99E8741529372A580CB4B174C390DB080A62037E55CC8771941'; Dependencies = @('/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette', '/Script/Engine') },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal'; RelativeFile = 'Calysto\Dungeon\Demo\LowPoly\Material\M_Metal.uasset'; Class = 'Material'; Length = [int64]19454; Sha256 = '8722C616F22B81315E266A471785B4169F45F2E9CCEF5229F08E27A6ED824B23'; Dependencies = @('/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette', '/Script/Engine') },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette'; RelativeFile = 'Calysto\Dungeon\Demo\LowPoly\Texture\T_Palette.uasset'; Class = 'Texture2D'; Length = [int64]100800; Sha256 = '515E0A851F19035D612620101F32133243442FFAEF0F0DC93FA049F0C931D26B'; Dependencies = @('/Script/Engine') },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors'; RelativeFile = 'Calysto\Dungeon\Mesh\DungeonMesh\SM_SquaredArchedWoodenDoors.uasset'; Class = 'StaticMesh'; Length = [int64]54199; Sha256 = '40AF92CF0E91C356B13AB171065C17A649CD874BBB6724A29793A7B91EB8A3A7'; Dependencies = @('/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial', '/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal', '/Script/Engine', '/Script/MeshDescription', '/Script/NavigationSystem', '/Script/PhysicsCore', '/Script/UnrealEd') }
)
$expectedLegacyDependencies = @(
    '/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors',
    '/Game/FullSample/Integrations/ACFBaseInteractableBP',
    '/Game/FullSample/Integrations/ATSIntegrations/ACFQuestTargetComponentBP',
    '/Game/Procedural/Maps/DungeonGeneration',
    '/Script/AscentCoreInterfaces',
    '/Script/AscentMapsSystem'
)

Assert-True ($assets.Count -eq $expectedPackageCount) 'Internal visual allowlist count differs.'
Assert-True (@($assets.Package | Sort-Object -Unique).Count -eq $expectedPackageCount) 'Internal visual allowlist contains duplicate packages.'
Assert-True (@($assets.RelativeFile | Sort-Object -Unique).Count -eq $expectedPackageCount) 'Internal visual allowlist contains duplicate files.'
Assert-True ((($assets | Measure-Object -Property Length -Sum).Sum) -eq $expectedSourceBytes) 'Internal visual source-byte total differs.'
$fingerprintText = @(
    $assets |
        Sort-Object Package |
        ForEach-Object { '{0}|{1}|{2}' -f $_.Package, $_.Length, $_.Sha256 }
) -join "`n"
Assert-True ((Get-TextSha256 -Text $fingerprintText) -eq $expectedSourceFingerprint) 'Internal visual source fingerprint differs.'

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4\DoorToLevel'
$runsRoot = Join-Path $phaseRoot 'Runs'
$runRoot = Join-Path $runsRoot $RunId
$harnessRoot = Join-Path $runRoot 'Harness57'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'DoorToLevelVisual57Harness.uproject'
$receiptPath = Join-Path $runRoot 'DoorToLevel57HarnessReceipt.json'
$manifestPath = Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv'
$registryEvidencePath = Join-Path $target 'Saved\Migration\Phase2\SourceAssetRegistry57.json'
$preStageSafetyEvidence = Join-Path $runRoot 'Gates\PRE_STAGE_SafetyGate.json'

Assert-True (-not $RunId.Contains('..')) 'RunId contains a forbidden traversal token.'
Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-IsUnderRoot -Path $runRoot -Root $runsRoot) 'Run evidence escapes target Saved/Migration.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $runRoot) 'Harness escapes its immutable run root.'
Assert-True (-not (Test-IsUnderRoot -Path $harnessRoot -Root $source)) 'Harness unexpectedly lives under the read-only source.'
Assert-True (-not (Test-Path -LiteralPath $harnessRoot)) 'Unique harness path unexpectedly already exists.'
Assert-True (-not (Test-Path -LiteralPath $receiptPath)) 'Immutable run receipt unexpectedly already exists.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'
Assert-True (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf) 'Target project descriptor is absent.'
Assert-True (Test-Path -LiteralPath $preStageSafetyEvidence -PathType Leaf) 'PRE_STAGE safety evidence was not produced.'
$preStageSafety = Get-Content -Raw -LiteralPath $preStageSafetyEvidence | ConvertFrom-Json
Assert-True ([string]$preStageSafety.status -eq 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS') 'PRE_STAGE source/protected evidence is not PASS.'
Assert-True ([string]$preStageSafety.stage -eq 'PRE_STAGE') 'PRE_STAGE evidence has the wrong stage.'
Assert-True ([string]$preStageSafety.run_id -eq $RunId) 'PRE_STAGE evidence belongs to a different run.'
Assert-True ([string]$preStageSafety.source_read_only.result -eq 'PASS') 'PRE_STAGE source gate is not PASS.'
Assert-True ([string]$preStageSafety.protected_invariants.result -eq 'PASS') 'PRE_STAGE protected gate is not PASS.'

Assert-True (Test-Path -LiteralPath $manifestPath -PathType Leaf) 'Migration manifest is absent.'
$manifest = @(Import-Csv -LiteralPath $manifestPath)
$manifestRows = @($manifest | Where-Object { $assets.Package -contains $_.PackageName })
Assert-True ($manifestRows.Count -eq $expectedPackageCount) 'Manifest does not contain exactly the visual allowlist.'
foreach ($asset in $assets) {
    $rows = @($manifestRows | Where-Object PackageName -eq $asset.Package)
    Assert-True ($rows.Count -eq 1) "Manifest row count differs for $($asset.Package)."
    $row = $rows[0]
    Assert-True ([string]$row.Presence -eq 'SOURCE_ONLY') "Manifest presence is not SOURCE_ONLY for $($asset.Package)."
    Assert-True ([int64]$row.SourceLength -eq $asset.Length) "Manifest length differs for $($asset.Package)."
    Assert-True ([string]$row.SourceSHA256 -eq $asset.Sha256) "Manifest hash differs for $($asset.Package)."
    Assert-True ([string]$row.Action -eq 'MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE') "Manifest action does not authorize AssetTools for $($asset.Package)."
}

Assert-True (Test-Path -LiteralPath $registryEvidencePath -PathType Leaf) 'UE 5.7 source Asset Registry evidence is absent.'
$registryEvidence = Get-Content -Raw -LiteralPath $registryEvidencePath | ConvertFrom-Json
$registryRows = @($registryEvidence.assets | Where-Object {
    ($assets.Package -contains $_.package_name -and $_.object_path -eq ($_.package_name + '.' + ($_.package_name -split '/')[-1])) -or
    ($_.package_name -eq $legacyBlueprint.Package -and $_.object_path -eq '/Game/Procedural/DoorToLevel.DoorToLevel')
})
Assert-True ($registryRows.Count -eq ($expectedPackageCount + 1)) 'Source Asset Registry does not contain the exact visual allowlist plus legacy Blueprint reference.'

foreach ($asset in $assets) {
    $rows = @($registryRows | Where-Object package_name -eq $asset.Package)
    Assert-True ($rows.Count -eq 1) "Source Asset Registry row count differs for $($asset.Package)."
    $row = $rows[0]
    Assert-True ([string]$row.class_path -match ('asset_name: "' + [regex]::Escape($asset.Class) + '"')) "Source class differs for $($asset.Package)."
    $actualDependencies = @($row.dependencies | Sort-Object -Unique)
    $expectedDependencies = @($asset.Dependencies | Sort-Object -Unique)
    $dependencyDelta = @(Compare-Object -ReferenceObject $expectedDependencies -DifferenceObject $actualDependencies -SyncWindow 0)
    Assert-True ($dependencyDelta.Count -eq 0) "Frozen dependency set differs for $($asset.Package): $($dependencyDelta -join ', ')"
}

$legacyRows = @($registryRows | Where-Object package_name -eq $legacyBlueprint.Package)
Assert-True ($legacyRows.Count -eq 1) 'Legacy DoorToLevel primary Asset Registry row count differs.'
$legacyRow = $legacyRows[0]
Assert-True ([string]$legacyRow.class_path -match 'asset_name: "Blueprint"') 'Legacy DoorToLevel is no longer a Blueprint.'
Assert-True ([string]$legacyRow.tags.GeneratedClass -eq "/Script/Engine.BlueprintGeneratedClass'/Game/Procedural/DoorToLevel.DoorToLevel_C'") 'Legacy DoorToLevel generated-class path changed.'
Assert-True ([string]$legacyRow.tags.ParentClass -eq "/Script/Engine.BlueprintGeneratedClass'/Game/FullSample/Integrations/ACFBaseInteractableBP.ACFBaseInteractableBP_C'") 'Legacy DoorToLevel parent-class audit changed.'
$legacyDependencyDelta = @(Compare-Object -ReferenceObject ($expectedLegacyDependencies | Sort-Object) -DifferenceObject @($legacyRow.dependencies | Sort-Object -Unique) -SyncWindow 0)
Assert-True ($legacyDependencyDelta.Count -eq 0) 'Legacy DoorToLevel dependency audit changed.'

$sourceRows = @()
foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    $targetFile = Join-Path $targetContent $asset.RelativeFile
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source visual asset is absent: $($asset.Package)"
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq $asset.Length) "Source visual length differs: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $sourceFile) -eq $asset.Sha256) "Source visual hash differs: $($asset.Package)"
    Assert-True (-not (Test-Path -LiteralPath $targetFile)) "Target visual collision exists: $($asset.Package)"
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($sourceFile, $extension)))) "Unexpected source sidecar exists for $($asset.Package): $extension"
        Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($targetFile, $extension)))) "Unexpected target sidecar exists for $($asset.Package): $extension"
    }
    $sourceRows += [ordered]@{
        package = $asset.Package
        class = $asset.Class
        source = $sourceFile
        target = $targetFile
        length = $asset.Length
        sha256 = $asset.Sha256
        dependencies = @($asset.Dependencies | Sort-Object)
    }
}

$legacySourceFile = Join-Path $sourceContent $legacyBlueprint.RelativeFile
$targetBlueprintFile = Join-Path $targetContent $legacyBlueprint.RelativeFile
Assert-True (Test-Path -LiteralPath $legacySourceFile -PathType Leaf) 'Legacy source DoorToLevel Blueprint is absent.'
Assert-True ((Get-Item -LiteralPath $legacySourceFile).Length -eq $legacyBlueprint.Length) 'Legacy source DoorToLevel length differs.'
Assert-True ((Get-Sha256 -Path $legacySourceFile) -eq $legacyBlueprint.Sha256) 'Legacy source DoorToLevel hash differs.'
Assert-True (-not (Test-Path -LiteralPath $targetBlueprintFile)) 'Target DoorToLevel collision exists before rebuild.'
foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
    Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($legacySourceFile, $extension)))) "Unexpected legacy source Blueprint sidecar exists: $extension"
    Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($targetBlueprintFile, $extension)))) "Unexpected target DoorToLevel sidecar exists: $extension"
}

$stagedRows = @()
foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    $stagedFile = Join-Path $harnessContent $asset.RelativeFile
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile
    Assert-True ((Get-Item -LiteralPath $stagedFile).Length -eq $asset.Length) "Staged visual length differs: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $stagedFile) -eq $asset.Sha256) "Staged visual hash differs: $($asset.Package)"
    $sourceRow = @($sourceRows | Where-Object package -eq $asset.Package)[0]
    $stagedRows += [ordered]@{
        package = $asset.Package
        class = $asset.Class
        source = $sourceRow.source
        staged = $stagedFile
        target = $sourceRow.target
        length = $asset.Length
        sha256 = $asset.Sha256
        dependencies = $sourceRow.dependencies
    }
}
Assert-NoReparsePoints -Path $harnessRoot

[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated UE 5.7 read-only load and AssetTools harness for the exact four DoorToLevel visual dependencies. The legacy Blueprint is never mounted or migrated.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true }
    )
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq $asset.Length) "Source visual length changed while staging: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $sourceFile) -eq $asset.Sha256) "Source visual hash changed while staging: $($asset.Package)"
}
Assert-True ((Get-Item -LiteralPath $legacySourceFile).Length -eq $legacyBlueprint.Length) 'Legacy source DoorToLevel length changed while staging.'
Assert-True ((Get-Sha256 -Path $legacySourceFile) -eq $legacyBlueprint.Sha256) 'Legacy source DoorToLevel hash changed while staging.'

[ordered]@{
    schema_version = 2
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_DOOR_VISUAL57_HARNESS_PASS'
    run_id = $RunId
    package_count = $expectedPackageCount
    source_bytes = $expectedSourceBytes
    source_fingerprint = $expectedSourceFingerprint
    fingerprint_algorithm = 'SHA256 of LF-joined sorted package|length|sha256 rows; no trailing LF'
    class_counts = [ordered]@{
        Texture2D = 1
        Material = 2
        StaticMesh = 1
    }
    source_root = $source
    target_root = $target
    run_root = $runRoot
    harness_root = $harnessRoot
    harness_content = $harnessContent
    harness_project = $harnessProject
    staged_assets = $stagedRows
    legacy_blueprint_reference = [ordered]@{
        package = $legacyBlueprint.Package
        source = $legacySourceFile
        target = $targetBlueprintFile
        length = $legacyBlueprint.Length
        sha256 = $legacyBlueprint.Sha256
        parent_class = '/Game/FullSample/Integrations/ACFBaseInteractableBP.ACFBaseInteractableBP_C'
        generated_class = '/Game/Procedural/DoorToLevel.DoorToLevel_C'
        dependencies = @($expectedLegacyDependencies | Sort-Object)
        decision = 'REBUILD_AS_THIN_PROJECT_OWNED_CHILD_OF_APROJECTLEVELDOOR'
        staged = $false
        migration_requested = $false
    }
    target_blueprint_contract = [ordered]@{
        package = '/Game/Procedural/DoorToLevel'
        object_path = '/Game/Procedural/DoorToLevel.DoorToLevel'
        generated_class = '/Game/Procedural/DoorToLevel.DoorToLevel_C'
        native_parent_class = '/Script/EFLevelFlowRuntime.ProjectLevelDoor'
        destination_level = '/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration'
        interactable_name = 'Interact'
        is_enabled = $true
        absolute_travel = $true
        travel_options = ''
        own_event_graph_allowed = $false
        runtime_travel_owner = 'AProjectLevelDoor::OnInteractedByPawn_Implementation'
        required_components = @('SceneRoot', 'InteractableComponent', 'StaticMesh', 'Sphere', 'QuestTargetComponent', 'MapMarkerComponent')
    }
    pre_stage_safety = [ordered]@{
        result = 'PASS'
        evidence = $preStageSafetyEvidence
        evidence_sha256 = Get-Sha256 -Path $preStageSafetyEvidence
        source_evidence = [string]$preStageSafety.source_read_only.evidence
        source_evidence_sha256 = [string]$preStageSafety.source_read_only.evidence_sha256
        protected_evidence = [string]$preStageSafety.protected_invariants.evidence
        protected_evidence_sha256 = [string]$preStageSafety.protected_invariants.evidence_sha256
    }
    source_tree_mounted = $false
    junctions_or_symlinks_present = $false
    source_package_saves = 0
    target_content_writes = 0
    raw_asset_policy = 'Only the exact four visual hashes are copied into target Saved/Migration for isolated UE 5.7 inspection. Live target Content may be populated only by UE 5.7 AssetTools. The legacy Blueprint remains a read-only audit reference and is rebuilt in UE 5.8.'
} | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "DOOR_TO_LEVEL57_HARNESS_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
