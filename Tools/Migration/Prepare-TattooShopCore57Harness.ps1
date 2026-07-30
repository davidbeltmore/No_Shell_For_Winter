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
    if (-not $Condition) { throw "TATTOO_SHOP57_HARNESS_FAIL: $Message" }
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

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$manifestPath = (Resolve-Path -LiteralPath (Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv')).Path
$packageListPath = (Resolve-Path -LiteralPath (Join-Path $target 'Tools\Migration\TattooShopCorePackages.txt')).Path
$runsRoot = Join-Path $target 'Saved\Migration\Phase4\TattooShop\Runs'
$runRoot = Join-Path $runsRoot $RunId
$harnessRoot = Join-Path $runRoot 'Harness57'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'TattooShopCore57Harness.uproject'
$receiptPath = Join-Path $runRoot 'TattooShopCore57HarnessReceipt.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'
Assert-True (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf) 'Target project descriptor is absent.'
Assert-True (Test-IsUnderRoot -Path $runRoot -Root $runsRoot) 'Run root escapes target Saved/Migration.'
Assert-True (-not (Test-Path -LiteralPath $runRoot)) 'RunId already exists; evidence runs are immutable.'

$packages = @(
    Get-Content -LiteralPath $packageListPath |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -and -not $_.StartsWith('#') } |
        Sort-Object -Unique
)
Assert-True ($packages.Count -eq 50) "Expected 50 packages but found $($packages.Count)."
Assert-True (@($packages | Where-Object { $_ -notlike '/Game/TattooShop/*' }).Count -eq 0) 'A package escapes /Game/TattooShop.'
$allowedTemplateInput = @(
    '/Game/TattooShop/ThirdPersonTemplateAssets/Input/IMC_Default',
    '/Game/TattooShop/ThirdPersonTemplateAssets/Input/Actions/IA_Jump',
    '/Game/TattooShop/ThirdPersonTemplateAssets/Input/Actions/IA_Look',
    '/Game/TattooShop/ThirdPersonTemplateAssets/Input/Actions/IA_MouseLook',
    '/Game/TattooShop/ThirdPersonTemplateAssets/Input/Actions/IA_Move'
)
Assert-True ($packages -contains '/Game/TattooShop/Blueprints/TSCharacter/BP_TSChar') 'BP_TSChar is required for 1:1 LADS runtime behavior.'
Assert-True (@($packages | Where-Object { $_ -like '/Game/TattooShop/ThirdPersonTemplateAssets*' -and $_ -notin $allowedTemplateInput }).Count -eq 0) 'Unapproved ThirdPersonTemplateAssets content entered the exact batch.'
Assert-True (@($packages | Where-Object { $_ -like '/Game/TattooShop/MultiplayerTestMap*' }).Count -eq 0) 'Demo MultiplayerTestMap content entered the exact batch.'

$manifestRows = @(Import-Csv -LiteralPath $manifestPath)
$stagedRows = [System.Collections.Generic.List[object]]::new()
New-Item -ItemType Directory -Path $harnessContent -Force | Out-Null

foreach ($package in $packages) {
    $rows = @($manifestRows | Where-Object PackageName -eq $package)
    Assert-True ($rows.Count -eq 1) "Manifest must contain exactly one row for $package."
    $row = $rows[0]
    Assert-True ([string]$row.Classification -eq 'SOURCE_ONLY_DEPENDENCY_CANDIDATE') "Unexpected classification for $package."
    Assert-True ([string]$row.Action -eq 'REVIEW_SOURCE_ONLY_DEPENDENCY') "Unexpected action for $package."
    Assert-True ([string]$row.Result -eq 'PENDING') "Manifest result is no longer PENDING for $package."
    $relative = ([string]$row.SourceFile).Replace('/', '\')
    Assert-True ($relative.StartsWith('Content\', [System.StringComparison]::OrdinalIgnoreCase)) "SourceFile is not under Content for $package."
    $contentRelative = $relative.Substring('Content\'.Length)
    $expectedRelative = $package.Substring('/Game/'.Length).Replace('/', '\') + '.uasset'
    Assert-True ($contentRelative.Equals($expectedRelative, [System.StringComparison]::OrdinalIgnoreCase)) "Package and SourceFile differ for $package."
    $sourceFile = Join-Path $sourceContent $contentRelative
    $targetFile = Join-Path $targetContent $contentRelative
    $stagedFile = Join-Path $harnessContent $contentRelative
    Assert-True (Test-IsUnderRoot -Path $sourceFile -Root $sourceContent) "Source file escapes Content for $package."
    Assert-True (Test-IsUnderRoot -Path $targetFile -Root $targetContent) "Target file escapes Content for $package."
    Assert-True (Test-IsUnderRoot -Path $stagedFile -Root $harnessContent) "Staged file escapes harness for $package."
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source file is absent for $package."
    Assert-True (-not (Test-Path -LiteralPath $targetFile)) "Target collision exists for $package."
    $sourceItem = Get-Item -LiteralPath $sourceFile
    $sourceHash = Get-Sha256 -Path $sourceFile
    Assert-True ($sourceItem.Length -eq [int64]$row.SourceLength) "Source length differs from manifest for $package."
    Assert-True ($sourceHash -eq ([string]$row.SourceSHA256).ToUpperInvariant()) "Source hash differs from manifest for $package."
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile
    Assert-True ((Get-Item -LiteralPath $stagedFile).Length -eq $sourceItem.Length) "Staged length differs for $package."
    Assert-True ((Get-Sha256 -Path $stagedFile) -eq $sourceHash) "Staged hash differs for $package."
    $files = @([ordered]@{
        relative_file = $contentRelative.Replace('\', '/')
        source = $sourceFile
        staged = $stagedFile
        target = $targetFile
        length = [int64]$sourceItem.Length
        sha256 = $sourceHash
    })
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        $sourceSidecar = [System.IO.Path]::ChangeExtension($sourceFile, $extension)
        if (-not (Test-Path -LiteralPath $sourceSidecar -PathType Leaf)) { continue }
        $sidecarRelative = [System.IO.Path]::ChangeExtension($contentRelative, $extension)
        $stagedSidecar = Join-Path $harnessContent $sidecarRelative
        $targetSidecar = Join-Path $targetContent $sidecarRelative
        Assert-True (-not (Test-Path -LiteralPath $targetSidecar)) "Target sidecar collision exists for $package."
        New-Item -ItemType Directory -Path (Split-Path -Parent $stagedSidecar) -Force | Out-Null
        Copy-Item -LiteralPath $sourceSidecar -Destination $stagedSidecar
        $sidecarItem = Get-Item -LiteralPath $sourceSidecar
        $sidecarHash = Get-Sha256 -Path $sourceSidecar
        Assert-True ((Get-Sha256 -Path $stagedSidecar) -eq $sidecarHash) "Staged sidecar hash differs for $package."
        $files += [ordered]@{
            relative_file = $sidecarRelative.Replace('\', '/')
            source = $sourceSidecar
            staged = $stagedSidecar
            target = $targetSidecar
            length = [int64]$sidecarItem.Length
            sha256 = $sidecarHash
        }
    }
    $stagedRows.Add([ordered]@{
        package = $package
        class = [string]$row.AssetClass
        source_dependencies = @(([string]$row.SourceDependencies).Split(';', [System.StringSplitOptions]::RemoveEmptyEntries) | Sort-Object -Unique)
        files = $files
    })
}

[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Detached read-only source harness for the exact TattooShop BP_TSChar runtime closure.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'EditorScriptingUtilities'; Enabled = $true },
        [ordered]@{ Name = 'EnhancedInput'; Enabled = $true },
        [ordered]@{ Name = 'Synthesis'; Enabled = $true }
    )
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_TATTOO_SHOP_BP_TSCHAR57_HARNESS_PASS'
    run_id = $RunId
    source_root = $source
    target_root = $target
    manifest = $manifestPath
    manifest_sha256 = Get-Sha256 -Path $manifestPath
    package_list = $packageListPath
    package_list_sha256 = Get-Sha256 -Path $packageListPath
    harness_root = $harnessRoot
    harness_content = $harnessContent
    harness_project = $harnessProject
    package_count = $stagedRows.Count
    packages = @($stagedRows)
    exclusions = @('ThirdPersonTemplateAssets except IMC_Default and four referenced input actions', 'MultiplayerTestMap', 'ExternalActors', 'ExternalObjects', 'legacy .sav slots')
    source_tree_mounted = $false
    source_package_saves = 0
    target_content_writes = 0
}
$payload | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "TATTOO_SHOP_CORE57_HARNESS_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
