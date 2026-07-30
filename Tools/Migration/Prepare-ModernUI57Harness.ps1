[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "MODERNUI57_HARNESS_GATE_FAIL: $Message" }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

function Copy-VerifiedFile {
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
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4'
$harnessRoot = Join-Path $phaseRoot 'ModernUI57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessPlugins = Join-Path $harnessRoot 'Plugins'
$harnessProject = Join-Path $harnessRoot 'ModernUI57Harness.uproject'
$harnessConfig = Join-Path $harnessRoot 'Config\DefaultEngine.ini'
$receiptPath = Join-Path $phaseRoot 'ModernUI57HarnessReceipt.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target are identical.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $phaseRoot) 'Harness root escapes target Saved/Migration/Phase4.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $phaseRoot) 'Refusing to clean a harness outside the migration staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'ModernUI57Harness') 'Unexpected harness directory name.'
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

$rootCounts = [ordered]@{
    '/Game/_Game/Widgets/Chronicle' = 23
    '/Game/_Game/Widgets/InnerState' = 24
    '/Game/_Game/Widgets/Status' = 17
    '/Game/_Game/Widgets/Attributes' = 27
    '/Game/_Game/Widgets/SinfulAscensionAltar' = 36
}
$manifestRows = @(Import-Csv -LiteralPath $manifestPath)
$selectedRows = @()
$actualRootCounts = [ordered]@{}
foreach ($prefix in $rootCounts.Keys) {
    $rows = @(
        $manifestRows |
            Where-Object {
                ([string]$_.PackageName).StartsWith(
                    $prefix + '/',
                    [System.StringComparison]::Ordinal
                )
            } |
            Sort-Object PackageName
    )
    Assert-True ($rows.Count -eq $rootCounts[$prefix]) "Expected $($rootCounts[$prefix]) manifest packages below $prefix but found $($rows.Count)."
    $actualRootCounts[$prefix] = $rows.Count
    $selectedRows += $rows
}
Assert-True ($selectedRows.Count -eq 127) "Expected 127 manifest packages but found $($selectedRows.Count)."
Assert-True ((@($selectedRows.PackageName | Sort-Object -Unique)).Count -eq 127) 'Modern UI manifest package names are not unique.'

$stagedAssets = @()
$sourceByteTotal = [int64]0
foreach ($row in @($selectedRows | Sort-Object PackageName)) {
    $packageName = [string]$row.PackageName
    Assert-True ($row.Presence -eq 'SOURCE_ONLY') "Package is not source-only: $packageName ($($row.Presence))"
    Assert-True (-not [string]::IsNullOrWhiteSpace($row.SourceFile)) "Source file is absent in manifest: $packageName"
    Assert-True (-not [string]::IsNullOrWhiteSpace($row.SourceSHA256)) "Source hash is absent in manifest: $packageName"

    $sourceFile = Join-Path $source ([string]$row.SourceFile)
    Assert-True (Test-IsUnderRoot -Path $sourceFile -Root $sourceContent) "Source package escapes Content: $sourceFile"
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source package is absent: $sourceFile"
    $sourceItem = Get-Item -LiteralPath $sourceFile
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFile).Hash
    Assert-True ($sourceItem.Length -eq [int64]$row.SourceLength) "Source length differs from manifest: $packageName"
    Assert-True ($sourceHash -eq [string]$row.SourceSHA256) "Source hash differs from manifest: $packageName"

    $relative = $sourceFile.Substring($sourceContent.Length).TrimStart('\')
    $destination = Join-Path $harnessContent $relative
    Assert-True (Test-IsUnderRoot -Path $destination -Root $harnessContent) "Staged package escapes harness: $destination"
    $copied = Copy-VerifiedFile -Source $sourceFile -Destination $destination -Role 'modern_ui_exact_allowlist'
    $copied.package = $packageName
    $stagedAssets += $copied
    $sourceByteTotal += [int64]$sourceItem.Length
}
Assert-True ($sourceByteTotal -eq 12370672) 'Modern UI source-byte total differs from the audited baseline.'

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
    $stagedPluginFiles += Copy-VerifiedFile -Source $descriptorSource -Destination $descriptorTarget -Role 'plugin_descriptor'

    $modulesSource = Join-Path $sourcePlugin 'Binaries\Win64\UnrealEditor.modules'
    $modulesTarget = Join-Path $harnessPlugins "$pluginName\Binaries\Win64\UnrealEditor.modules"
    $stagedPluginFiles += Copy-VerifiedFile -Source $modulesSource -Destination $modulesTarget -Role 'ue57_module_manifest'

    foreach ($module in @($descriptor.Modules)) {
        $moduleName = [string]$module.Name
        $dllSource = Join-Path $sourcePlugin "Binaries\Win64\UnrealEditor-$moduleName.dll"
        $dllTarget = Join-Path $harnessPlugins "$pluginName\Binaries\Win64\UnrealEditor-$moduleName.dll"
        $stagedPluginFiles += Copy-VerifiedFile -Source $dllSource -Destination $dllTarget -Role 'ue57_precompiled_module'
    }
}

New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated UE 5.7 harness for the exact 127-package modern project UI batch.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true }
    ) + @($projectPluginNames | ForEach-Object { [ordered]@{ Name = $_; Enabled = $true } })
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

New-Item -ItemType Directory -Path (Split-Path -Parent $harnessConfig) -Force | Out-Null
@'
[/Script/Engine.CollisionProfile]
+Profiles=(Name="WaterBodyCollision",CollisionEnabled=QueryOnly,bCanModify=False,ObjectTypeName="Water",CustomResponses=((Channel="WorldDynamic",Response=ECR_Overlap),(Channel="Pawn",Response=ECR_Overlap),(Channel="Visibility",Response=ECR_Ignore),(Channel="Camera",Response=ECR_Ignore),(Channel="PhysicsBody",Response=ECR_Overlap),(Channel="Vehicle",Response=ECR_Overlap),(Channel="Destructible",Response=ECR_Overlap)),HelpMessage="Default Water Collision Profile (Created by Water Plugin)")
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel12,DefaultResponse=ECR_Overlap,bTraceType=False,bStaticObject=False,Name="Water")
'@ | Set-Content -LiteralPath $harnessConfig -Encoding UTF8

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_MODERN_UI_HARNESS_PASS'
    source_root = $source
    target_root = $target
    harness_root = $harnessRoot
    harness_project = $harnessProject
    expected_package_count = 127
    expected_source_bytes = 12370672
    root_counts = $actualRootCounts
    staged_assets = $stagedAssets
    staged_plugin_files = $stagedPluginFiles
    policy = 'Exactly 127 manifest-frozen packages from five project-owned modern UI roots plus the minimum precompiled UE 5.7 project-plugin load set; all staging remains below target Saved and source is never mounted or written.'
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "MODERNUI57_HARNESS_PASS: $harnessProject"
Write-Host "Packages: $($stagedAssets.Count); bytes: $sourceByteTotal; plugin files: $($stagedPluginFiles.Count)"
Write-Host "Receipt: $receiptPath"
