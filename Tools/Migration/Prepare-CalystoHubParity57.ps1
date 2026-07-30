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

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
if ($source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Source and target roots are identical.'
}

$manifestPath = Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv'
$pluginRoot = Join-Path $target 'Saved\Migration\Phase4\ModernUI57Harness\Plugins'
$runRoot = Join-Path $target ("Saved\Migration\CalystoHub\Runs\{0}" -f $RunId)
$harnessRoot = Join-Path $runRoot 'Harness57'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'CalystoHubParity57Harness.uproject'
$receiptPath = Join-Path $runRoot 'CalystoHubParity57HarnessReceipt.json'

if (Test-Path -LiteralPath $runRoot) { throw "Run already exists: $runRoot" }
if (-not (Test-Path -LiteralPath $pluginRoot -PathType Container)) {
    throw "Detached UE 5.7 plugin set is absent: $pluginRoot"
}

$packages = @(
    '/Game/ExportedAnimations/Together/0001Scene',
    '/Game/ExportedAnimations/M_SexAnimations/AS_DoggyClassic_1_Male_Corrected',
    '/Game/ExportedAnimations/SexAnimations/AS_DoggyClassic_1_Female'
)
$supportPackages = @(
    '/Game/DazToUnreal/Female/Female',
    '/Game/DazToUnreal/Female/Female_Skeleton',
    '/Game/DazToUnreal/Male/Male',
    '/Game/DazToUnreal/Male/Male_Skeleton',
    '/Game/FullSample/GASP/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin',
    '/Game/_Game/Textures/Cum/MilkySplash01'
)
$manifest = @(Import-Csv -LiteralPath $manifestPath)
$rows = @()
$supportRows = @()
foreach ($package in @($packages + $supportPackages)) {
    $matches = @($manifest | Where-Object PackageName -eq $package)
    if ($matches.Count -ne 1) { throw "Manifest row is not unique: $package" }
    $row = $matches[0]
    if ($package -in $packages -and [string]$row.Action -ne 'MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE') {
        throw "Manifest does not authorize AssetTools migration: $package"
    }
    $sourceFile = Join-Path $source ([string]$row.SourceFile)
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Source package file is absent: $sourceFile"
    }
    $sourceItem = Get-Item -LiteralPath $sourceFile
    $sourceHash = Get-Sha256 $sourceFile
    if ($sourceItem.Length -ne [int64]$row.SourceLength -or $sourceHash -ne [string]$row.SourceSHA256) {
        throw "Source package differs from the migration manifest: $package"
    }
    $relative = ([string]$row.SourceFile).Substring('Content\'.Length)
    $stagedFile = Join-Path $harnessContent $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile
    if ((Get-Sha256 $stagedFile) -ne $sourceHash) { throw "Staged hash differs: $package" }
    $outputRow = [pscustomobject]@{
        package = $package
        relative_file = $relative.Replace('\', '/')
        source = $sourceFile
        staged = $stagedFile
        target = Join-Path (Join-Path $target 'Content') $relative
        length = [int64]$sourceItem.Length
        sha256 = $sourceHash
    }
    if ($package -in $packages) { $rows += $outputRow }
    else { $supportRows += $outputRow }
}

$pluginNames = @(
    Get-ChildItem -LiteralPath $pluginRoot -Directory -Force |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName ($_.Name + '.uplugin')) } |
        Select-Object -ExpandProperty Name |
        Sort-Object -Unique
)
New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Detached UE 5.7 AssetTools harness for the exact HUB 0001Scene dependency closure.'
    AdditionalPluginDirectories = @($pluginRoot.Replace('\', '/'))
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'Niagara'; Enabled = $true },
        [ordered]@{ Name = 'ControlRig'; Enabled = $true },
        [ordered]@{ Name = 'ACLPlugin'; Enabled = $true },
        [ordered]@{ Name = 'AscentCombatFramework'; Enabled = $true }
    ) + @($pluginNames | ForEach-Object { [ordered]@{ Name = $_; Enabled = $true } })
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'CALYSTO_HUB_PARITY57_HARNESS_PASS'
    run_id = $RunId
    source_root = $source
    target_root = $target
    harness_root = $harnessRoot
    harness_project = $harnessProject
    package_count = $rows.Count
    packages = $rows
    support_package_count = $supportRows.Count
    support_packages = $supportRows
    source_tree_mounted = $false
    target_content_writes = 0
}
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
Write-Host "CALYSTO_HUB_PARITY57_HARNESS_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
