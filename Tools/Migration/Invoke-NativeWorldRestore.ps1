[CmdletBinding()]
param(
    [string]$SourceProject = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetProject = 'D:\Projects UE5\NoShellForWinter',
    [string]$Engine57Root = 'D:\Unreal Engine 5\Library\UE_5.7'
)

$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-RelativeContentPath([string]$Root, [string]$Path) {
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    Assert-True ($pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) "Path escaped Content root: $pathFull"
    return $pathFull.Substring($rootFull.Length).Replace('\', '/')
}

$sourceRoot = [System.IO.Path]::GetFullPath($SourceProject)
$targetRoot = [System.IO.Path]::GetFullPath($TargetProject)
$sourceContent = Join-Path $sourceRoot 'Content'
$targetContent = Join-Path $targetRoot 'Content'
$registryFile = Join-Path $targetRoot 'Saved\Migration\Phase2\SourceAssetRegistry57.json'
$editorCmd = Join-Path $Engine57Root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$migrationScript = Join-Path $targetRoot 'Tools\Migration\Restore-NativeWorldAssets57.py'

Assert-True ($sourceRoot -ne $targetRoot) 'Source and target roots must differ.'
Assert-True (Test-Path -LiteralPath $sourceContent -PathType Container) 'Source Content is absent.'
Assert-True (Test-Path -LiteralPath $targetContent -PathType Container) 'Target Content is absent.'
Assert-True (Test-Path -LiteralPath $registryFile -PathType Leaf) 'Frozen UE 5.7 Asset Registry JSON is absent.'
Assert-True (Test-Path -LiteralPath $editorCmd -PathType Leaf) 'UE 5.7 UnrealEditor-Cmd.exe is absent.'
Assert-True (Test-Path -LiteralPath $migrationScript -PathType Leaf) 'Migration Python script is absent.'
Assert-True (-not (Get-Process UnrealEditor, UnrealEditor-Cmd -ErrorAction SilentlyContinue)) 'Close Unreal Editor before restoring package files.'

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$runRoot = Join-Path $targetRoot "Saved\Migration\Phase4\NativeWorldRestore\$stamp"
$harnessRoot = Join-Path $runRoot 'Harness57'
$harnessContent = Join-Path $harnessRoot 'Content'
$backupRoot = Join-Path $runRoot 'TargetBackupBefore57Restore'
$manifestFile = Join-Path $runRoot 'NativeWorldRestoreManifest.json'
$receiptFile = Join-Path $runRoot 'NativeWorldRestore57Receipt.json'
$logFile = Join-Path $runRoot 'NativeWorldRestore57.log'
$harnessProject = Join-Path $harnessRoot 'NativeWorldRestore57.uproject'

New-Item -ItemType Directory -Path $harnessContent -Force | Out-Null

$registry = Get-Content -LiteralPath $registryFile -Raw | ConvertFrom-Json
$byPackage = @{}
foreach ($asset in $registry.assets) {
    if ($asset.package_name -and -not $byPackage.ContainsKey([string]$asset.package_name)) {
        $byPackage[[string]$asset.package_name] = $asset
    }
}

$packages = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)

# Altar visual closure: three source meshes plus their recursive /Game dependencies.
$altarSeeds = @(
    '/Game/Fantastic_Dungeon_Pack/meshes/props/fabrics/SM_PROP_altar_cloth_dungeon_02',
    '/Game/Fantastic_Dungeon_Pack/meshes/props/furniture/SM_PROP_altar_dungeon_01',
    '/Game/Fantastic_Dungeon_Pack/meshes/props/small_deco/SM_PROP_book_dungeon_07'
)
$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($seed in $altarSeeds) { $queue.Enqueue($seed) }
while ($queue.Count -gt 0) {
    $package = $queue.Dequeue()
    if (-not $packages.Add($package)) { continue }
    Assert-True ($byPackage.ContainsKey($package)) "Altar dependency is absent from source registry: $package"
    foreach ($dependency in $byPackage[$package].dependencies) {
        if ([string]$dependency -like '/Game/*' -and -not $packages.Contains([string]$dependency)) {
            $queue.Enqueue([string]$dependency)
        }
    }
}

# Material instances whose OneClickMaterials parent was stripped by the prior harness.
$oneClickInstances = @(
    '/Game/_Game/Textures/Stone_Wall/Stone_Wall',
    '/Game/ShareTextures/Wall/1K/MI_Stone_Wall_21',
    '/Game/ShareTextures/Wall/1K/MI_Sand_Stone_Texture_3',
    '/Game/ShareTextures/Plaster/1K/MI_Plaster_Sand',
    '/Game/ShareTextures/Ground/1K/MI_Snow_Covered_Ground',
    '/Game/ShareTextures/Ground/1K/MI_Ground_4'
)
foreach ($package in $oneClickInstances) { [void]$packages.Add($package) }

# Calysto is deliberately excluded. The user will install the native UE 5.8
# Fab release, so this restore must not overwrite any Calysto package.
Assert-True (-not @($packages | Where-Object { $_ -like '/Game/Calysto/*' }).Count) 'Calysto package entered the restore manifest.'
Assert-True ($packages.Count -eq 22) "Audited package count changed; expected 22, found $($packages.Count)."

$manifestRows = @()
foreach ($package in @($packages | Sort-Object)) {
    Assert-True ($package.StartsWith('/Game/')) "Only /Game packages are allowed: $package"
    Assert-True (-not $package.StartsWith('/Game/FullSample/')) "Protected FullSample package entered manifest: $package"
    $relativeStem = $package.Substring('/Game/'.Length).Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $candidateUasset = Join-Path $sourceContent ($relativeStem + '.uasset')
    $candidateUmap = Join-Path $sourceContent ($relativeStem + '.umap')
    $primary = if (Test-Path -LiteralPath $candidateUasset -PathType Leaf) {
        $candidateUasset
    } elseif (Test-Path -LiteralPath $candidateUmap -PathType Leaf) {
        $candidateUmap
    } else {
        throw "Source package file is absent: $package"
    }

    $sourceFiles = @($primary)
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        $sidecar = [System.IO.Path]::ChangeExtension($primary, $extension)
        if (Test-Path -LiteralPath $sidecar -PathType Leaf) { $sourceFiles += $sidecar }
    }

    $relativeFiles = @()
    $fileRecords = [ordered]@{}
    foreach ($sourceFile in $sourceFiles) {
        $relative = Get-RelativeContentPath $sourceContent $sourceFile
        $relativeFiles += $relative
        $destination = Join-Path $harnessContent $relative
        New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        Copy-Item -LiteralPath $sourceFile -Destination $destination -Force
        $record = [ordered]@{
            length = (Get-Item -LiteralPath $sourceFile).Length
            sha256 = Get-Sha256 $sourceFile
        }
        Assert-True ((Get-Sha256 $destination) -eq $record.sha256) "Staged copy hash differs: $relative"
        $fileRecords[$relative] = $record
    }
    $manifestRows += [ordered]@{
        package_name = $package
        relative_files = $relativeFiles
        source_files = $fileRecords
    }
}

$manifest = [ordered]@{
    schema = 1
    source_root = $sourceRoot
    target_root = $targetRoot
    package_count = $manifestRows.Count
    categories = [ordered]@{
        altar_visual_closure = 16
        oneclick_instances = 6
        calysto_excluded_for_native_ue58_fab_install = 0
    }
    packages = $manifestRows
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestFile -Encoding UTF8

$harnessDescriptor = [ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'Codex detached migration harness'
    Description = 'Audited native world asset restoration; never writes to source project.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'EditorScriptingUtilities'; Enabled = $true }
    )
}
$harnessDescriptor | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

$env:CODEX_NATIVE_WORLD_MANIFEST = $manifestFile
$env:CODEX_NATIVE_WORLD_TARGET_CONTENT = $targetContent
$env:CODEX_NATIVE_WORLD_BACKUP_DIR = $backupRoot
$env:CODEX_NATIVE_WORLD_RECEIPT = $receiptFile

& $editorCmd $harnessProject "-ExecutePythonScript=$migrationScript" -unattended -nop4 -nosplash -NullRHI -NoSound -stdout -FullStdOutLogOutput "-abslog=$logFile"
$exitCode = $LASTEXITCODE
Assert-True ($exitCode -eq 0) "UE 5.7 AssetTools restore failed with exit code $exitCode. See $logFile"
Assert-True (Test-Path -LiteralPath $receiptFile -PathType Leaf) 'UE 5.7 restore receipt is absent.'
$receipt = Get-Content -LiteralPath $receiptFile -Raw | ConvertFrom-Json
Assert-True ($receipt.status -eq 'PASS') "UE 5.7 restore receipt is not PASS: $($receipt.status)"

[pscustomobject]@{
    Status = 'PASS'
    RunRoot = $runRoot
    PackageCount = $manifestRows.Count
    Receipt = $receiptFile
    Log = $logFile
}
