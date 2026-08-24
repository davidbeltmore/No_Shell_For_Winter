param(
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development",
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8",
    [string]$CookLog,
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$windowsRoot = Join-Path $archivePath "Windows"
$manifestPath = Join-Path $windowsRoot "Manifest_UFSFiles_Win64.txt"
$policySource = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralRuntime\Private\Calysto\EFCalystoDungeonDirectorPolicy.cpp"
$unrealPak = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealPak.exe"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"

foreach ($requiredPath in @($windowsRoot, $manifestPath, $policySource, $unrealPak, $receiptGuard)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required package validation path not found: $requiredPath"
    }
}

$catalogPackages = [Regex]::Matches(
    [System.IO.File]::ReadAllText($policySource),
    'TEXT\("(?<P>/Game/[^"]+)"\)'
) | ForEach-Object {
    $_.Groups["P"].Value -replace '\.[^./]+$', ''
} | Sort-Object -Unique

$requiredPackages = @(
    "/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy"
) + $catalogPackages + @(
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh"
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner"
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme"
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon"
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster"
    "/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple"
    "/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon"
    "/Game/Procedural/Maps/DungeonGeneration"
) | Sort-Object -Unique

if ($requiredPackages.Count -ne 34) {
    throw "V3 package contract expected 34 packages but resolved $($requiredPackages.Count)."
}

$manifestPaths = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($line in [System.IO.File]::ReadLines($manifestPath)) {
    $relativePath = ($line -split "`t", 2)[0].Replace("\", "/")
    [void]$manifestPaths.Add($relativePath)
}

function ConvertTo-StagedAssetPath {
    param([string]$PackageName)

    $extension = if ($PackageName -eq "/Game/Procedural/Maps/DungeonGeneration") {
        ".umap"
    }
    else {
        ".uasset"
    }
    return "NoShellForWinter/Content/" + $PackageName.Substring("/Game/".Length) + $extension
}

$requiredManifestPaths = @($requiredPackages | ForEach-Object { ConvertTo-StagedAssetPath $_ })
$missingFromManifest = @($requiredManifestPaths | Where-Object { !$manifestPaths.Contains($_) })
$requiredPluginDescriptorPaths = @(
    "Engine/Plugins/DazToUnreal/DazToUnreal.uplugin"
    "Engine/Plugins/Animation/MLDeformer/NeuralMorphModel/NeuralMorphModel.uplugin"
    "NoShellForWinter/Plugins/EFCharacterCreationDazBridge/EFCharacterCreationDazBridge.uplugin"
)
$requiredPluginSupportPaths = @(
    "Engine/Plugins/DazToUnreal/Config/FilterPlugin.ini"
)
$missingPluginDescriptorsFromManifest = @($requiredPluginDescriptorPaths | Where-Object {
    !$manifestPaths.Contains($_)
})
$missingPluginSupportFromManifest = @($requiredPluginSupportPaths | Where-Object {
    !$manifestPaths.Contains($_)
})

$utocPath = Join-Path $windowsRoot `
    "NoShellForWinter\Content\Paks\NoShellForWinter-Windows.utoc"
$ucasPath = Join-Path $windowsRoot `
    "NoShellForWinter\Content\Paks\NoShellForWinter-Windows.ucas"
$pakPath = Join-Path $windowsRoot `
    "NoShellForWinter\Content\Paks\NoShellForWinter-Windows.pak"
$gameExe = if ($Configuration -eq "Shipping") {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter-Win64-Shipping.exe"
}
else {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter.exe"
}

foreach ($packageFile in @($utocPath, $ucasPath, $pakPath, $gameExe)) {
    if (!(Test-Path -LiteralPath $packageFile)) {
        throw "Packaged output is incomplete: $packageFile"
    }
}

$utocListing = (& $unrealPak $utocPath -List 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "UnrealPak failed to list $utocPath with exit code $LASTEXITCODE."
}
$pakListing = (& $unrealPak $pakPath -List 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "UnrealPak failed to list $pakPath with exit code $LASTEXITCODE."
}
$containerListing = $utocListing + [Environment]::NewLine + $pakListing

$requiredContainerPaths = @($requiredPackages | ForEach-Object {
    $extension = if ($_ -eq "/Game/Procedural/Maps/DungeonGeneration") { ".umap" } else { ".uasset" }
    "Content/" + $_.Substring("/Game/".Length) + $extension
})
$missingFromContainer = @($requiredContainerPaths | Where-Object {
    $containerListing.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -lt 0
})
$missingPluginDescriptorsFromContainer = @($requiredPluginDescriptorPaths | Where-Object {
    $containerListing.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -lt 0
})
$missingPluginSupportFromContainer = @($requiredPluginSupportPaths | Where-Object {
    $containerListing.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -lt 0
})

$legacyNames = @(
    "DT_CalystoFloorProfiles"
    "DT_CalystoSpawnerWeights"
    "DT_CalystoThemeWeights"
    "DT_CalystoGenerationOptions"
    "DT_CalystoSpawnerPresets"
    "DT_CalystoThemePresets"
)
$legacyInManifest = @($legacyNames | Where-Object {
    $legacyName = $_
    @($manifestPaths | Where-Object { $_.IndexOf($legacyName, [StringComparison]::OrdinalIgnoreCase) -ge 0 }).Count -gt 0
})
$legacyInContainer = @($legacyNames | Where-Object {
    $containerListing.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -ge 0
})

$receiptVerification = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $ProjectRoot `
    -TargetName NoShellForWinter `
    -Configuration $Configuration `
    -VerifyOnly 2>&1 | Out-String
$receiptExitCode = $LASTEXITCODE

$blockedLogPatterns = @(
    "Object Transform"
    "GetAttributeFromPointIndex_0"
    "Blueprint Runtime Error"
    "ensure condition failed"
    "Fatal error:"
    "duplicate GenerateLocal"
)
$logFindings = @()
$cookSucceeded = $true
if (![string]::IsNullOrWhiteSpace($CookLog)) {
    $resolvedCookLog = (Resolve-Path -LiteralPath $CookLog).Path
    $cookText = [System.IO.File]::ReadAllText($resolvedCookLog)
    $cookSucceeded = $cookText.IndexOf("BUILD SUCCESSFUL", [StringComparison]::OrdinalIgnoreCase) -ge 0 `
        -and $cookText.IndexOf("ExitCode=0", [StringComparison]::OrdinalIgnoreCase) -ge 0
    foreach ($pattern in $blockedLogPatterns) {
        if ($cookText.IndexOf($pattern, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $logFindings += $pattern
        }
    }
}

$hashes = [ordered]@{}
foreach ($file in @($manifestPath, $utocPath, $ucasPath, $pakPath, $gameExe)) {
    $hashes[(Split-Path -Leaf $file)] = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash
}

$status = if (
    $missingFromManifest.Count -eq 0 `
    -and $missingFromContainer.Count -eq 0 `
    -and $missingPluginDescriptorsFromManifest.Count -eq 0 `
    -and $missingPluginDescriptorsFromContainer.Count -eq 0 `
    -and $missingPluginSupportFromManifest.Count -eq 0 `
    -and $missingPluginSupportFromContainer.Count -eq 0 `
    -and $legacyInManifest.Count -eq 0 `
    -and $legacyInContainer.Count -eq 0 `
    -and $receiptExitCode -eq 0 `
    -and $cookSucceeded `
    -and $logFindings.Count -eq 0
) { "PASS" } else { "FAIL" }

$report = [ordered]@{
    schema_version = 3
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = $status
    configuration = $Configuration
    archive_root = $archivePath
    required_package_count = $requiredPackages.Count
    required_packages = $requiredPackages
    missing_from_manifest = $missingFromManifest
    missing_from_container = $missingFromContainer
    required_plugin_descriptors = $requiredPluginDescriptorPaths
    missing_plugin_descriptors_from_manifest = $missingPluginDescriptorsFromManifest
    missing_plugin_descriptors_from_container = $missingPluginDescriptorsFromContainer
    required_plugin_support = $requiredPluginSupportPaths
    missing_plugin_support_from_manifest = $missingPluginSupportFromManifest
    missing_plugin_support_from_container = $missingPluginSupportFromContainer
    legacy_in_manifest = $legacyInManifest
    legacy_in_container = $legacyInContainer
    receipt_exit_code = $receiptExitCode
    receipt_verification = $receiptVerification.Trim()
    cook_succeeded = $cookSucceeded
    blocked_log_findings = $logFindings
    hashes = $hashes
}
$json = $report | ConvertTo-Json -Depth 8

if (![string]::IsNullOrWhiteSpace($OutputPath)) {
    $outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
    [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($outputFullPath))
    [System.IO.File]::WriteAllText($outputFullPath, $json, [System.Text.UTF8Encoding]::new($false))
}

Write-Output $json
if ($status -ne "PASS") {
    throw "Dungeon Director V3 $Configuration package validation failed."
}
