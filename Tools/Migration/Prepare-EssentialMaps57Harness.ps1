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
    if (-not $Condition) { throw "ESSENTIAL_MAPS57_HARNESS_FAIL: $Message" }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    try {
        $fullPath = [System.IO.Path]::GetFullPath($Path)
        $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
        return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
    }
    catch { return $false }
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Convert-RelativeFileToPackage {
    param([string]$RelativeFile)
    $normalized = $RelativeFile.Replace('\', '/')
    $extension = [System.IO.Path]::GetExtension($normalized).ToLowerInvariant()
    Assert-True ($extension -in @('.uasset', '.umap')) "Not a primary package file: $RelativeFile"
    return '/Game/' + $normalized.Substring(0, $normalized.Length - $extension.Length)
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$manifestPath = (Resolve-Path -LiteralPath (Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv')).Path
$runsRoot = Join-Path $target 'Saved\Migration\Phase4\EssentialMaps\Runs'
$runRoot = Join-Path $runsRoot $RunId
$harnessRoot = Join-Path $runRoot 'Harness57'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'EssentialMaps57Harness.uproject'
$harnessConfig = Join-Path $harnessRoot 'Config\DefaultEngine.ini'
$receiptPath = Join-Path $runRoot 'EssentialMaps57HarnessReceipt.json'
$pluginRoot = Join-Path $target 'Saved\Migration\Phase4\ModernUI57Harness\Plugins'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'
Assert-True (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf) 'Target project descriptor is absent.'
Assert-True (Test-IsUnderRoot -Path $runRoot -Root $runsRoot) 'Run root escapes Saved/Migration.'
Assert-True (-not (Test-Path -LiteralPath $runRoot)) 'RunId already exists; evidence runs are immutable.'
Assert-True (Test-Path -LiteralPath $pluginRoot -PathType Container) 'Detached UE 5.7 project-plugin load set is absent.'

$mapSpecs = @(
    [pscustomobject]@{ Package = '/Game/_Game/Hub/HUB'; Relative = '_Game\Hub\HUB.umap' },
    [pscustomobject]@{ Package = '/Game/_Game/Locations/StorySelection'; Relative = '_Game\Locations\StorySelection.umap' },
    [pscustomobject]@{ Package = '/Game/_Game/Locations/PCGLevel'; Relative = '_Game\Locations\PCGLevel.umap' }
)
$manifestRows = @(Import-Csv -LiteralPath $manifestPath)
$copiedRelative = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$migrationRows = [System.Collections.Generic.List[object]]::new()
$supportRows = [System.Collections.Generic.List[object]]::new()
$mapRows = [System.Collections.Generic.List[object]]::new()
$migrationPackages = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$supportPackages = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

function Copy-VerifiedPackageFile {
    param(
        [string]$RelativeFile,
        [string]$Role,
        [string]$OwnerMap,
        [System.Collections.Generic.List[object]]$OutputRows
    )
    $sourceFile = Join-Path $sourceContent $RelativeFile
    $stagedFile = Join-Path $harnessContent $RelativeFile
    Assert-True (Test-IsUnderRoot -Path $sourceFile -Root $sourceContent) "Source file escapes Content: $RelativeFile"
    Assert-True (Test-IsUnderRoot -Path $stagedFile -Root $harnessContent) "Staged file escapes harness: $RelativeFile"
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source file is absent: $RelativeFile"
    if (-not $copiedRelative.Add($RelativeFile)) { return }
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile
    $sourceItem = Get-Item -LiteralPath $sourceFile
    $sourceHash = Get-Sha256 -Path $sourceFile
    Assert-True ((Get-Item -LiteralPath $stagedFile).Length -eq $sourceItem.Length) "Staged length differs: $RelativeFile"
    Assert-True ((Get-Sha256 -Path $stagedFile) -eq $sourceHash) "Staged hash differs: $RelativeFile"
    $OutputRows.Add([pscustomobject]@{
        relative_file = $RelativeFile.Replace('\', '/')
        role = $Role
        owner_map = $OwnerMap
        source = $sourceFile
        staged = $stagedFile
        length = [int64]$sourceItem.Length
        sha256 = $sourceHash
    })
}

function Copy-PackageAndSidecars {
    param(
        [string]$PrimaryRelative,
        [string]$Role,
        [string]$OwnerMap,
        [System.Collections.Generic.List[object]]$OutputRows
    )
    Copy-VerifiedPackageFile -RelativeFile $PrimaryRelative -Role $Role -OwnerMap $OwnerMap -OutputRows $OutputRows
    $base = Join-Path $sourceContent ([System.IO.Path]::ChangeExtension($PrimaryRelative, $null))
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        $sidecar = $base + $extension
        if (Test-Path -LiteralPath $sidecar -PathType Leaf) {
            $relativeSidecar = $sidecar.Substring($sourceContent.Length).TrimStart('\')
            Copy-VerifiedPackageFile -RelativeFile $relativeSidecar -Role ($Role + '_sidecar') -OwnerMap $OwnerMap -OutputRows $OutputRows
        }
    }
}

function Add-MigrationPrimary {
    param([string]$PrimaryRelative, [string]$Role, [string]$OwnerMap)
    $package = Convert-RelativeFileToPackage -RelativeFile $PrimaryRelative
    if ($migrationPackages.Add($package)) {
        Copy-PackageAndSidecars -PrimaryRelative $PrimaryRelative -Role $Role -OwnerMap $OwnerMap -OutputRows $migrationRows
    }
}

function Add-SupportPackage {
    param([string]$Package, [string]$OwnerMap)
    if (-not $supportPackages.Add($Package)) { return }
    Assert-True ($Package.StartsWith('/Game/', [System.StringComparison]::Ordinal)) "Support dependency is not /Game: $Package"
    $relativeBase = $Package.Substring(6).Replace('/', '\')
    $primary = $null
    foreach ($extension in @('.uasset', '.umap')) {
        $candidate = $relativeBase + $extension
        if (Test-Path -LiteralPath (Join-Path $sourceContent $candidate) -PathType Leaf) {
            $primary = $candidate
            break
        }
    }
    Assert-True (-not [string]::IsNullOrWhiteSpace($primary)) "Direct support dependency is absent in source Content: $Package"
    Copy-PackageAndSidecars -PrimaryRelative $primary -Role 'load_support_only_never_migrate' -OwnerMap $OwnerMap -OutputRows $supportRows
}

foreach ($spec in $mapSpecs) {
    $rows = @($manifestRows | Where-Object PackageName -eq $spec.Package)
    Assert-True ($rows.Count -eq 1) "Manifest must contain exactly one row for $($spec.Package)."
    $manifestRow = $rows[0]
    Assert-True ([string]$manifestRow.Presence -eq 'SOURCE_ONLY') "Map is no longer SOURCE_ONLY: $($spec.Package)"
    Assert-True ([string]$manifestRow.Classification -eq 'SOURCE_ONLY_PROJECT_CONTENT') "Map classification changed: $($spec.Package)"
    Assert-True ([string]$manifestRow.Action -eq 'MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE') "Map action is not authorized: $($spec.Package)"
    Assert-True ([string]$manifestRow.SourceFile -eq ('Content\' + $spec.Relative)) "Manifest SourceFile differs: $($spec.Package)"
    $sourceMap = Join-Path $sourceContent $spec.Relative
    Assert-True (Test-Path -LiteralPath $sourceMap -PathType Leaf) "Source map is absent: $($spec.Package)"
    Assert-True ((Get-Item -LiteralPath $sourceMap).Length -eq [int64]$manifestRow.SourceLength) "Source length differs from manifest: $($spec.Package)"
    Assert-True ((Get-Sha256 -Path $sourceMap) -eq [string]$manifestRow.SourceSHA256) "Source hash differs from manifest: $($spec.Package)"

    Add-MigrationPrimary -PrimaryRelative $spec.Relative -Role 'essential_map' -OwnerMap $spec.Package
    $relativeWithoutExtension = [System.IO.Path]::ChangeExtension($spec.Relative, $null)
    $externalPackageCounts = [ordered]@{ actors = 0; objects = 0 }
    foreach ($externalKind in @('__ExternalActors__', '__ExternalObjects__')) {
        $externalRoot = Join-Path $sourceContent (Join-Path $externalKind $relativeWithoutExtension)
        if (-not (Test-Path -LiteralPath $externalRoot -PathType Container)) { continue }
        $primaries = @(
            Get-ChildItem -LiteralPath $externalRoot -Recurse -File -Force |
                Where-Object { $_.Extension -in @('.uasset', '.umap') } |
                Sort-Object FullName
        )
        foreach ($file in $primaries) {
            $relative = $file.FullName.Substring($sourceContent.Length).TrimStart('\')
            Add-MigrationPrimary -PrimaryRelative $relative -Role $externalKind.Trim('_').ToLowerInvariant() -OwnerMap $spec.Package
        }
        if ($externalKind -eq '__ExternalActors__') { $externalPackageCounts.actors = $primaries.Count }
        else { $externalPackageCounts.objects = $primaries.Count }
    }

    $mapDirectory = Split-Path -Parent $spec.Relative
    $mapBaseName = [System.IO.Path]::GetFileNameWithoutExtension($spec.Relative)
    $builtDataRelative = Join-Path $mapDirectory ($mapBaseName + '_BuiltData.uasset')
    $builtDataPresent = Test-Path -LiteralPath (Join-Path $sourceContent $builtDataRelative) -PathType Leaf
    if ($builtDataPresent) {
        Add-MigrationPrimary -PrimaryRelative $builtDataRelative -Role 'map_built_data' -OwnerMap $spec.Package
    }

    $directDependencies = @(
        ([string]$manifestRow.SourceDependencies).Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_.StartsWith('/Game/', [System.StringComparison]::Ordinal) } |
            Sort-Object -Unique
    )
    foreach ($dependency in $directDependencies) {
        Add-SupportPackage -Package $dependency -OwnerMap $spec.Package
    }
    $mapRows.Add([pscustomobject]@{
        package = $spec.Package
        relative_file = $spec.Relative.Replace('\', '/')
        source_length = [int64]$manifestRow.SourceLength
        source_sha256 = [string]$manifestRow.SourceSHA256
        source_registry_present = [System.Convert]::ToBoolean([string]$manifestRow.SourceRegistryPresent)
        manifest_direct_dependencies = @(([string]$manifestRow.SourceDependencies).Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) | Sort-Object -Unique)
        direct_game_dependencies_staged_for_load_only = $directDependencies
        external_actor_package_count = [int]$externalPackageCounts.actors
        external_object_package_count = [int]$externalPackageCounts.objects
        built_data_present = [bool]$builtDataPresent
    })
}

Assert-True ($mapRows.Count -eq 3) 'Essential map count differs from three.'
Assert-True (@($migrationPackages | Where-Object { $_ -like '/Game/FullSample*' -or $_ -like '/Game/DazToUnreal*' }).Count -eq 0) 'Protected package entered the migration seed set.'

$pluginNames = @(
    Get-ChildItem -LiteralPath $pluginRoot -Directory -Force |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName ($_.Name + '.uplugin')) -PathType Leaf } |
        Select-Object -ExpandProperty Name |
        Sort-Object -Unique
)
Assert-True ($pluginNames -contains 'EFProjectSystems') 'UE 5.7 EFProjectSystems load plugin is absent.'

New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Detached UE 5.7 harness for three essential project maps and their exact OFPA/external package sets.'
    AdditionalPluginDirectories = @($pluginRoot.Replace('\', '/'))
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'PCG'; Enabled = $true },
        [ordered]@{ Name = 'Niagara'; Enabled = $true },
        [ordered]@{ Name = 'AscentCombatFramework'; Enabled = $true }
    ) + @($pluginNames | ForEach-Object { [ordered]@{ Name = $_; Enabled = $true } })
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

New-Item -ItemType Directory -Path (Split-Path -Parent $harnessConfig) -Force | Out-Null
@'
[/Script/Engine.CollisionProfile]
+Profiles=(Name="WaterBodyCollision",CollisionEnabled=QueryOnly,bCanModify=False,ObjectTypeName="Water",CustomResponses=((Channel="WorldDynamic",Response=ECR_Overlap),(Channel="Pawn",Response=ECR_Overlap),(Channel="Visibility",Response=ECR_Ignore),(Channel="Camera",Response=ECR_Ignore),(Channel="PhysicsBody",Response=ECR_Overlap),(Channel="Vehicle",Response=ECR_Overlap),(Channel="Destructible",Response=ECR_Overlap)),HelpMessage="Default Water Collision Profile (Created by Water Plugin)")
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel12,DefaultResponse=ECR_Overlap,bTraceType=False,bStaticObject=False,Name="Water")
'@ | Set-Content -LiteralPath $harnessConfig -Encoding UTF8

$migrationPackageRows = @()
foreach ($package in @($migrationPackages | Sort-Object)) {
    $primary = @($migrationRows | Where-Object {
        [System.IO.Path]::GetExtension([string]$_.relative_file).ToLowerInvariant() -in @('.uasset', '.umap') -and
        (Convert-RelativeFileToPackage -RelativeFile ([string]$_.relative_file).Replace('/', '\')) -eq $package
    })
    Assert-True ($primary.Count -eq 1) "Migration package has no unique primary file: $package"
    $stem = [System.IO.Path]::ChangeExtension(([string]$primary[0].relative_file), $null)
    $files = @($migrationRows | Where-Object {
        ([System.IO.Path]::ChangeExtension(([string]$_.relative_file), $null)).Equals($stem, [System.StringComparison]::OrdinalIgnoreCase)
    } | Sort-Object relative_file)
    $targetPrimary = Join-Path $targetContent ([string]$primary[0].relative_file).Replace('/', '\')
    $migrationPackageRows += [pscustomobject]@{
        package = $package
        role = [string]$primary[0].role
        owner_map = [string]$primary[0].owner_map
        primary_extension = [System.IO.Path]::GetExtension([string]$primary[0].relative_file).ToLowerInvariant()
        target_primary = $targetPrimary
        target_collision_at_stage = Test-Path -LiteralPath $targetPrimary -PathType Leaf
        files = $files
    }
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_ESSENTIAL_MAPS57_HARNESS_PASS'
    run_id = $RunId
    source_root = $source
    target_root = $target
    manifest = $manifestPath
    harness_root = $harnessRoot
    harness_content = $harnessContent
    harness_project = $harnessProject
    plugin_root = $pluginRoot
    maps = @($mapRows)
    migration_package_count = $migrationPackageRows.Count
    migration_packages = $migrationPackageRows
    support_package_count = $supportPackages.Count
    support_packages = @($supportPackages | Sort-Object)
    support_files = @($supportRows | Sort-Object relative_file)
    source_tree_mounted = $false
    source_package_saves = 0
    target_content_writes = 0
    migration_policy = 'Only the three maps plus exact BuiltData/__ExternalActors__/__ExternalObjects__ package families are migration seeds. Direct /Game dependencies are staged for UE 5.7 load only. AssetTools must run with ignore_dependencies=true and conflict=SKIP.'
}
$payload | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "ESSENTIAL_MAPS57_HARNESS_PASS: $harnessProject"
Write-Host "Migration seeds: $($migrationPackageRows.Count); load-only support: $($supportPackages.Count)"
Write-Host "Receipt: $receiptPath"
