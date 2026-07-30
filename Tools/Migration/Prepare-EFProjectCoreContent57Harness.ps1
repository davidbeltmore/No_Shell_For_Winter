[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "EFCORECONTENT57_HARNESS_GATE_FAIL: $Message" }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

function Copy-AllowlistedFile {
    param(
        [string]$Source,
        [string]$Destination,
        [string]$Role
    )

    Assert-True (Test-Path -LiteralPath $Source -PathType Leaf) "Missing allowlisted file: $Source"
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination
    $sourceItem = Get-Item -LiteralPath $Source
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Source).Hash
    $destinationHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
    Assert-True ($sourceHash -eq $destinationHash) "Staging hash mismatch: $Destination"
    return [ordered]@{
        source = $Source
        staged = $Destination
        role = $Role
        length = [int64]$sourceItem.Length
        sha256 = $sourceHash
    }
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$manifestPath = (Resolve-Path -LiteralPath (Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv')).Path
$migrationRoot = Join-Path $target 'Saved\Migration\Phase4'
$harnessRoot = Join-Path $migrationRoot 'EFProjectCoreContent57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessPlugins = Join-Path $harnessRoot 'Plugins'
$harnessProject = Join-Path $harnessRoot 'EFProjectCoreContent57Harness.uproject'
$harnessEngineConfig = Join-Path $harnessRoot 'Config\DefaultEngine.ini'
$receiptPath = Join-Path $migrationRoot 'EFProjectCoreContent57HarnessReceipt.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target are identical.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $migrationRoot) 'Harness root escapes target Saved/Migration/Phase4.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $migrationRoot) 'Refusing to clean a harness outside the migration staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'EFProjectCoreContent57Harness') 'Unexpected harness directory name.'
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

$expectedPackages = @(
    '/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses',
    '/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry',
    '/Game/Data/CharacterBackground/DT_ProjectBackstories',
    '/Game/Data/CharacterBackground/DT_ProjectProfessions',
    '/Game/UI/CharacterBackground/WBP_ProjectCharacterBackgroundCreationWidget',
    '/Game/UI/Defeat/Struggle/WBP_ProjectKnockoutStruggleWidget',
    '/Game/_Game/Icons/Bleeding',
    '/Game/_Game/Icons/Dirt',
    '/Game/_Game/Icons/Dizzy',
    '/Game/_Game/Icons/Exhausted',
    '/Game/_Game/Icons/extremepain_transparent',
    '/Game/_Game/Icons/Fear',
    '/Game/_Game/Icons/frenzy_transparent',
    '/Game/_Game/Icons/gracestep_transparent',
    '/Game/_Game/Icons/Hungry',
    '/Game/_Game/Icons/knockedout_transparent',
    '/Game/_Game/Icons/Orgasm',
    '/Game/_Game/Icons/SleepDeprived',
    '/Game/_Game/Icons/Thirst'
)
$seedPackages = @($expectedPackages | Select-Object -First 6)
$manifestRows = @(Import-Csv -LiteralPath $manifestPath)
$manifestLookup = @{}
foreach ($row in $manifestRows) { $manifestLookup[[string]$row.PackageName] = $row }

$stagedAssets = @()
foreach ($packageName in $expectedPackages) {
    Assert-True ($manifestLookup.ContainsKey($packageName)) "Package is absent from the Phase 2 manifest: $packageName"
    $row = $manifestLookup[$packageName]
    Assert-True ($row.Presence -eq 'SOURCE_ONLY') "Package is not source-only: $packageName ($($row.Presence))"
    Assert-True (-not [string]::IsNullOrWhiteSpace($row.SourceFile)) "Source file is absent in manifest: $packageName"
    Assert-True (-not [string]::IsNullOrWhiteSpace($row.SourceSHA256)) "Source hash is absent in manifest: $packageName"

    $sourceFile = Join-Path $source ([string]$row.SourceFile)
    Assert-True (Test-IsUnderRoot -Path $sourceFile -Root $sourceContent) "Source package escapes source Content: $sourceFile"
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source package is absent: $sourceFile"
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq [int64]$row.SourceLength) "Source length differs from manifest: $packageName"
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFile).Hash -eq [string]$row.SourceSHA256) "Source hash differs from manifest: $packageName"

    $relativeContentPath = $sourceFile.Substring($sourceContent.Length).TrimStart('\')
    $stagedFile = Join-Path $harnessContent $relativeContentPath
    Assert-True (Test-IsUnderRoot -Path $stagedFile -Root $harnessContent) "Staged package escapes harness Content: $stagedFile"
    $copied = Copy-AllowlistedFile -Source $sourceFile -Destination $stagedFile -Role $(if ($seedPackages -contains $packageName) { 'migration_seed' } else { 'verified_direct_dependency' })
    $copied.package = $packageName
    $stagedAssets += $copied
}
Assert-True ($stagedAssets.Count -eq 19) "Expected 19 staged packages but found $($stagedAssets.Count)."

$previewSource = Join-Path $source 'Content\_Game\Images\preview.png'
$previewStaged = Join-Path $harnessRoot 'Raw\Content\_Game\Images\preview.png'
$preview = Copy-AllowlistedFile -Source $previewSource -Destination $previewStaged -Role 'raw_preview_sidecar'
Assert-True ($preview.length -eq 749769) 'preview.png length differs from the audited baseline.'
Assert-True ($preview.sha256 -eq '6B4075152BB866EB6B05AB8E24AD68A1138756AA0899681B348FA38F8DE288D3') 'preview.png hash differs from the audited baseline.'

$projectPluginNames = @(
    'ACFTrainingSystem',
    'EFCharacterCreation',
    'EFLevelFlow',
    'EFProcedural',
    'CodeWidgetDesignerBridge',
    'DirtyPawnRuntime',
    'EFProjectSystems'
)
$stagedPluginFiles = @()
foreach ($pluginName in $projectPluginNames) {
    $sourcePlugin = Join-Path $source "Plugins\$pluginName"
    $descriptorSource = Join-Path $sourcePlugin "$pluginName.uplugin"
    Assert-True (Test-Path -LiteralPath $descriptorSource -PathType Leaf) "Missing source plugin descriptor: $pluginName"
    $descriptor = Get-Content -Raw -LiteralPath $descriptorSource | ConvertFrom-Json
    $descriptorTarget = Join-Path $harnessPlugins "$pluginName\$pluginName.uplugin"
    $stagedPluginFiles += Copy-AllowlistedFile -Source $descriptorSource -Destination $descriptorTarget -Role 'plugin_descriptor'

    $modulesFileSource = Join-Path $sourcePlugin 'Binaries\Win64\UnrealEditor.modules'
    $modulesFileTarget = Join-Path $harnessPlugins "$pluginName\Binaries\Win64\UnrealEditor.modules"
    $stagedPluginFiles += Copy-AllowlistedFile -Source $modulesFileSource -Destination $modulesFileTarget -Role 'ue57_module_manifest'

    foreach ($module in @($descriptor.Modules)) {
        $moduleName = [string]$module.Name
        $dllSource = Join-Path $sourcePlugin "Binaries\Win64\UnrealEditor-$moduleName.dll"
        $dllTarget = Join-Path $harnessPlugins "$pluginName\Binaries\Win64\UnrealEditor-$moduleName.dll"
        $stagedPluginFiles += Copy-AllowlistedFile -Source $dllSource -Destination $dllTarget -Role 'ue57_precompiled_module'
    }
}

$projectDescriptor = [ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated target-hosted UE 5.7 harness for six EFProjectSystems core-content seeds and thirteen direct icon dependencies.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true }
    ) + @($projectPluginNames | ForEach-Object { [ordered]@{ Name = $_; Enabled = $true } })
}
New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
$projectDescriptor | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8
New-Item -ItemType Directory -Path (Split-Path -Parent $harnessEngineConfig) -Force | Out-Null
@'
[/Script/Engine.CollisionProfile]
+Profiles=(Name="WaterBodyCollision",CollisionEnabled=QueryOnly,bCanModify=False,ObjectTypeName="Water",CustomResponses=((Channel="WorldDynamic",Response=ECR_Overlap),(Channel="Pawn",Response=ECR_Overlap),(Channel="Visibility",Response=ECR_Ignore),(Channel="Camera",Response=ECR_Ignore),(Channel="PhysicsBody",Response=ECR_Overlap),(Channel="Vehicle",Response=ECR_Overlap),(Channel="Destructible",Response=ECR_Overlap)),HelpMessage="Default Water Collision Profile (Created by Water Plugin)")
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel12,DefaultResponse=ECR_Overlap,bTraceType=False,bStaticObject=False,Name="Water")
'@ | Set-Content -LiteralPath $harnessEngineConfig -Encoding UTF8

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_CORE_CONTENT_HARNESS_PASS'
    source_root = $source
    harness_root = $harnessRoot
    harness_project = $harnessProject
    harness_engine_config = $harnessEngineConfig
    seed_packages = $seedPackages
    expected_package_count = 19
    staged_assets = $stagedAssets
    raw_preview = $preview
    staged_plugin_files = $stagedPluginFiles
    policy = 'Nineteen exact-manifest UE packages, one exact-hash PNG, and descriptor/base UE 5.7 DLL files only. Everything is staged below target Saved; the live source is never mounted or written.'
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "EFCORECONTENT57_HARNESS_PASS: $harnessProject"
Write-Host "Packages: $($stagedAssets.Count); plugin files: $($stagedPluginFiles.Count)"
Write-Host "Receipt: $receiptPath"
