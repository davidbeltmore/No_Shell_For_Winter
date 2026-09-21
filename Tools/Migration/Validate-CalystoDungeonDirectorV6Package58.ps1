[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [Parameter(Mandatory = $true)]
    [string]$CookLog,
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration = 'Development',
    [ValidateSet('PreCutover', 'FinalStrict')]
    [string]$ValidationMode = 'FinalStrict',
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$OutputPath = ''
)

# This validator intentionally proves Calysto's RealisticBlood closure, not the
# complete project's RealisticBlood inventory. Other project systems may cook
# their own permitted textures (for example Intimacy's Splatter06) without
# becoming dependencies of the Calysto V6 Director.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$expectedPolicyPackage = '/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy'
$expectedPolicyObject = "$expectedPolicyPackage.DA_CalystoDungeonDirectorPolicy"
$expectedPolicyClass = '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset'
$expectedHashSchemaVersion = 3
$expectedBundleNames = @('CalystoFloorV6', 'CalystoStyleV6', 'CalystoThemeV6', 'CalystoDecalsV6')
$expectedBloodPackages = @(
    '/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04',
    '/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04'
)
$decalMaterialPackages = @(
    '/EFProcedural/Calysto/Internal/Materials/Decals/M_CalystoBloodDecal',
    '/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Ceiling',
    '/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor',
    '/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Wall'
)
$internalPCGPackages = @(
    '/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe',
    '/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe'
)
$runtimeBlueprintPackages = @(
    '/Game/_Game/Items/Chests/BP_CalystoLockedChest',
    '/Game/_Game/Items/Chests/BP_CalystoLockPickChest',
    '/Game/_Game/Items/Clothing/BP_CalystoArmorPickup'
)
$retiredVersions = @(3..5)
$legacyPolicyPackages = @($retiredVersions | ForEach-Object {
    "/Game/_Game/Data/CalystoDungeon/V$($_)/DA_CalystoDungeonDirectorPolicy"
}) + @(
    "/Game/_Game/Data/CalystoDungeon/V$($retiredVersions[-1])/DT_CalystoDungeonDirectorPolicy"
)
$requiredCookMaps = @(
    '/Game/FullSample/Integrations/UIIntegrations/Level/MenuMap',
    '/Game/_Game/Hub/HUB',
    '/Game/Procedural/Maps/DungeonGeneration'
)
$fixedCookSupportPackages = @(
    $expectedPolicyPackage,
    '/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh',
    '/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial',
    '/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner',
    '/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme',
    '/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon',
    '/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint',
    '/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster',
    '/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape',
    '/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh',
    '/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps',
    '/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple',
    '/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon',
    '/AscentCombatFramework/Blueprints/AI/Controllers/ACFMeleeAIControllerBP',
    '/AscentCombatFramework/Blueprints/AI/Controllers/ACFRangedAIControllerBP'
)

function Test-TextContains {
    param([AllowEmptyString()][string]$Text, [Parameter(Mandatory = $true)][string]$Value)
    return $Text.IndexOf($Value, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

function Test-Sha256 {
    param([object]$Value)
    return [string]$Value -cmatch '^[A-Fa-f0-9]{64}$'
}

function Get-Utf8StringSha256 {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '')
    }
    finally { $algorithm.Dispose() }
}

function ConvertTo-PackageName {
    param([Parameter(Mandatory = $true)][string]$ObjectPath)
    $dot = $ObjectPath.IndexOf('.')
    if ($dot -ge 0) { return $ObjectPath.Substring(0, $dot) }
    return $ObjectPath
}

function Get-CppTextMacroAssetPaths {
    param([Parameter(Mandatory = $true)][string]$SourcePath)
    $text = [IO.File]::ReadAllText($SourcePath)
    $macroPattern = 'TEXT\(\s*(?<tokens>(?:"(?:\\.|[^"\\])*"\s*)+)\)'
    $literalPattern = '"(?<part>(?:\\.|[^"\\])*)"'
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($macro in [regex]::Matches(
            $text, $macroPattern, [Text.RegularExpressions.RegexOptions]::Singleline)) {
        $value = ''
        foreach ($literal in [regex]::Matches($macro.Groups['tokens'].Value, $literalPattern)) {
            $value += [regex]::Unescape($literal.Groups['part'].Value)
        }
        if ($value.StartsWith('/Game/', [StringComparison]::Ordinal) -or
            $value.StartsWith('/EFProcedural/', [StringComparison]::Ordinal)) {
            $paths.Add($value)
        }
    }
    return @($paths | Sort-Object -Unique)
}

function ConvertTo-LocalPackageFile {
    param(
        [Parameter(Mandatory = $true)][string]$PackageName,
        [Parameter(Mandatory = $true)][string]$ResolvedProjectRoot
    )
    if ($PackageName.StartsWith('/Game/', [StringComparison]::Ordinal)) {
        return Join-Path $ResolvedProjectRoot ('Content\' +
            $PackageName.Substring('/Game/'.Length).Replace('/', '\') + '.uasset')
    }
    if ($PackageName.StartsWith('/EFProcedural/', [StringComparison]::Ordinal)) {
        return Join-Path $ResolvedProjectRoot ('Plugins\EFProcedural\Content\' +
            $PackageName.Substring('/EFProcedural/'.Length).Replace('/', '\') + '.uasset')
    }
    return ''
}

function Get-ManifestAssetPath {
    param([Parameter(Mandatory = $true)][string]$PackageName, [bool]$IsMap = $false)
    $extension = if ($IsMap) { '.umap' } else { '.uasset' }
    if ($PackageName.StartsWith('/Game/', [StringComparison]::Ordinal)) {
        return 'NoShellForWinter/Content/' + $PackageName.Substring('/Game/'.Length) + $extension
    }
    if ($PackageName.StartsWith('/EFProcedural/', [StringComparison]::Ordinal)) {
        return 'NoShellForWinter/Plugins/EFProcedural/Content/' +
            $PackageName.Substring('/EFProcedural/'.Length) + $extension
    }
    $mountRelative = $PackageName.TrimStart('/')
    $mountSeparator = $mountRelative.IndexOf('/')
    if ($mountSeparator -ge 0) {
        $mountRelative = $mountRelative.Substring($mountSeparator + 1)
    }
    return 'Content/' + $mountRelative + $extension
}

function Get-ContainerAssetPath {
    param([Parameter(Mandatory = $true)][string]$PackageName, [bool]$IsMap = $false)
    $extension = if ($IsMap) { '.umap' } else { '.uasset' }
    if ($PackageName.StartsWith('/Game/', [StringComparison]::Ordinal)) {
        return 'Content/' + $PackageName.Substring('/Game/'.Length) + $extension
    }
    if ($PackageName.StartsWith('/EFProcedural/', [StringComparison]::Ordinal)) {
        return 'Plugins/EFProcedural/Content/' +
            $PackageName.Substring('/EFProcedural/'.Length) + $extension
    }
    $mountRelative = $PackageName.TrimStart('/')
    $mountSeparator = $mountRelative.IndexOf('/')
    if ($mountSeparator -ge 0) {
        $mountRelative = $mountRelative.Substring($mountSeparator + 1)
    }
    return 'Content/' + $mountRelative + $extension
}

function Test-ManifestPackagePresent {
    param(
        [Parameter(Mandatory = $true)][Collections.Generic.HashSet[string]]$Manifest,
        [Parameter(Mandatory = $true)][string]$PackageName,
        [bool]$IsMap = $false
    )
    $expected = Get-ManifestAssetPath -PackageName $PackageName -IsMap $IsMap
    if ($Manifest.Contains($expected)) { return $true }
    if ($PackageName.StartsWith('/Game/', [StringComparison]::Ordinal) -or
        $PackageName.StartsWith('/EFProcedural/', [StringComparison]::Ordinal)) { return $false }
    $suffix = $expected
    return @($Manifest | Where-Object {
        ([string]$_).EndsWith($suffix, [StringComparison]::OrdinalIgnoreCase)
    }).Count -gt 0
}

function Test-ContainerPackagePresent {
    param(
        [Parameter(Mandatory = $true)][string]$Listing,
        [Parameter(Mandatory = $true)][string]$PackageName,
        [bool]$IsMap = $false
    )
    return Test-TextContains $Listing (Get-ContainerAssetPath -PackageName $PackageName -IsMap $IsMap)
}

function Find-TokensInBinary {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Tokens
    )
    $remaining = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($token in $Tokens) { [void]$remaining.Add($token) }
    $found = [Collections.Generic.List[string]]::new()
    $maximumTokenLength = @($Tokens | ForEach-Object { $_.Length } |
        Measure-Object -Maximum).Maximum
    $tailLength = [Math]::Max(0, [int]$maximumTokenLength - 1)
    $asciiTail = ''
    $unicodeTail = ''
    $buffer = [byte[]]::new(4MB)
    $stream = [IO.File]::Open(
        $Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        while ($remaining.Count -gt 0) {
            $read = $stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            $asciiText = $asciiTail + [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
            $unicodeByteCount = $read - ($read % 2)
            $unicodeText = $unicodeTail +
                [Text.Encoding]::Unicode.GetString($buffer, 0, $unicodeByteCount)
            foreach ($token in @($remaining)) {
                if ((Test-TextContains $asciiText $token) -or
                    (Test-TextContains $unicodeText $token)) {
                    $found.Add($token)
                    [void]$remaining.Remove($token)
                }
            }
            $asciiTail = if ($asciiText.Length -gt $tailLength) {
                $asciiText.Substring($asciiText.Length - $tailLength)
            } else { $asciiText }
            $unicodeTail = if ($unicodeText.Length -gt $tailLength) {
                $unicodeText.Substring($unicodeText.Length - $tailLength)
            } else { $unicodeText }
        }
    }
    finally { $stream.Dispose() }
    return @($found)
}

$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$cookLogPath = (Resolve-Path -LiteralPath $CookLog).Path
$windowsRoot = Join-Path $archivePath 'Windows'
$manifestPath = Join-Path $windowsRoot 'Manifest_UFSFiles_Win64.txt'
$packagingContractPath = Join-Path $archivePath 'CalystoV6PackagingContract.json'
$policyHeader = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralRuntime\Public\Calysto\EFCalystoDungeonDirectorPolicyV6.h'
$policySource = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralRuntime\Private\Calysto\EFCalystoDungeonDirectorPolicyV6.cpp'
$pcgSource = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\EFProceduralPCGSubsystem.cpp'
$graphSource = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\Calysto\EFCalystoPCGRuntimeGraphV6.cpp'
$decalPoolSource = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\Calysto\EFCalystoDecalPoolV6.cpp'
$gameConfigPath = Join-Path $ProjectRoot 'Config\DefaultGame.ini'
$gameTargetPath = Join-Path $ProjectRoot 'Source\NoShellForWinter.Target.cs'
$projectDescriptorPath = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$receiptGuardPath = Join-Path $ProjectRoot 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$policyReceiptPath = Join-Path $ProjectRoot `
    'Saved\Migration\CalystoDungeonDirectorV6\CreateDirectorPolicyV6.json'
$decalReceiptPath = Join-Path $ProjectRoot `
    'Saved\Migration\CalystoDungeonDirectorV6\CreateDecalMaterialsV6.json'
$runtimeAssetsReceiptPath = Join-Path $ProjectRoot `
    'Saved\Migration\CalystoDungeonDirectorV6\CreateRuntimeAssetsV6.json'
$internalPCGReceiptPath = Join-Path $ProjectRoot `
    'Saved\Migration\CalystoDungeonDirectorV6\CreateInternalPCGClosureV6.json'
$cookClosureReceiptPath = Join-Path $ProjectRoot `
    'Saved\Migration\CalystoDungeonDirectorV6\ValidateCookClosureV6.json'
$cookClosureHelperPath = Join-Path $ProjectRoot `
    'Tools\Migration\Validate-CalystoDungeonDirectorV6CookClosure.py'
$unrealPakPath = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealPak.exe'
$policyAssetPath = ConvertTo-LocalPackageFile $expectedPolicyPackage $ProjectRoot
$gameBinaryName = if ($Configuration -eq 'Shipping') {
    'NoShellForWinter-Win64-Shipping.exe'
} else { 'NoShellForWinter.exe' }
$gameReceiptName = if ($Configuration -eq 'Shipping') {
    'NoShellForWinter-Win64-Shipping.target'
} else { 'NoShellForWinter.target' }
$localGameBinaryPath = Join-Path $ProjectRoot "Binaries\Win64\$gameBinaryName"
$localGameReceiptPath = Join-Path $ProjectRoot "Binaries\Win64\$gameReceiptName"
$gameExePath = Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\$gameBinaryName"
$utocPath = Join-Path $windowsRoot 'NoShellForWinter\Content\Paks\NoShellForWinter-Windows.utoc'
$ucasPath = Join-Path $windowsRoot 'NoShellForWinter\Content\Paks\NoShellForWinter-Windows.ucas'
$pakPath = Join-Path $windowsRoot 'NoShellForWinter\Content\Paks\NoShellForWinter-Windows.pak'
$globalUtocPath = Join-Path $windowsRoot 'NoShellForWinter\Content\Paks\global.utoc'
$globalUcasPath = Join-Path $windowsRoot 'NoShellForWinter\Content\Paks\global.ucas'

$requiredFiles = @(
    $manifestPath, $packagingContractPath, $policyHeader, $policySource, $pcgSource,
    $graphSource, $decalPoolSource, $gameConfigPath, $gameTargetPath, $projectDescriptorPath,
    $receiptGuardPath, $policyReceiptPath, $decalReceiptPath, $runtimeAssetsReceiptPath,
    $internalPCGReceiptPath,
    $cookClosureReceiptPath,
    $cookClosureHelperPath,
    $unrealPakPath, $policyAssetPath, $localGameBinaryPath, $localGameReceiptPath,
    $gameExePath, $utocPath, $ucasPath, $pakPath, $globalUtocPath, $globalUcasPath
)
foreach ($package in $decalMaterialPackages + $internalPCGPackages +
        $runtimeBlueprintPackages + $expectedBloodPackages) {
    $localFile = ConvertTo-LocalPackageFile $package $ProjectRoot
    if (-not [string]::IsNullOrWhiteSpace($localFile)) { $requiredFiles += $localFile }
}
foreach ($requiredFile in @($requiredFiles | Sort-Object -Unique)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf) -or
        (Get-Item -LiteralPath $requiredFile).Length -le 0) {
        throw "Required non-empty Calysto V6 package-validation input is missing: $requiredFile"
    }
}

$packagingContract = Get-Content -Raw -LiteralPath $packagingContractPath | ConvertFrom-Json
$policyReceipt = Get-Content -Raw -LiteralPath $policyReceiptPath | ConvertFrom-Json
$decalReceipt = Get-Content -Raw -LiteralPath $decalReceiptPath | ConvertFrom-Json
$runtimeAssetsReceipt = Get-Content -Raw -LiteralPath $runtimeAssetsReceiptPath | ConvertFrom-Json
$internalPCGReceipt = Get-Content -Raw -LiteralPath $internalPCGReceiptPath | ConvertFrom-Json
$cookClosureReceipt = Get-Content -Raw -LiteralPath $cookClosureReceiptPath | ConvertFrom-Json
$policyHeaderText = [IO.File]::ReadAllText($policyHeader)
$policySourceText = [IO.File]::ReadAllText($policySource)
$pcgSourceText = [IO.File]::ReadAllText($pcgSource)
$graphSourceText = [IO.File]::ReadAllText($graphSource)
$decalPoolSourceText = [IO.File]::ReadAllText($decalPoolSource)
$gameConfigText = [IO.File]::ReadAllText($gameConfigPath)
$cookText = [IO.File]::ReadAllText($cookLogPath)

$nativeDefaultBundleAssetPaths = @(Get-CppTextMacroAssetPaths $policySource)
$nativeDefaultBundlePackages = @($nativeDefaultBundleAssetPaths | ForEach-Object {
    ConvertTo-PackageName $_
} | Sort-Object -Unique)
$nativeDefaultBloodPackages = @($nativeDefaultBundlePackages | Where-Object {
    $_.StartsWith('/Game/RealisticBlood/', [StringComparison]::OrdinalIgnoreCase)
})
$policyBundleAssetPaths = @($cookClosureReceipt.policy.bundle_asset_paths)
$policyBundlePackages = @($policyBundleAssetPaths | ForEach-Object {
    ConvertTo-PackageName $_
} | Sort-Object -Unique)
$policyBundleCanonical = $policyBundleAssetPaths -join "`n"
$policyBloodPackages = @($policyBundlePackages | Where-Object {
    $_.StartsWith('/Game/RealisticBlood/', [StringComparison]::OrdinalIgnoreCase)
})
$policyDecalMaterials = @($policyBundlePackages | Where-Object {
    $_.StartsWith(
        '/EFProcedural/Calysto/Internal/Materials/Decals/',
        [StringComparison]::OrdinalIgnoreCase)
})
$policyInternalPCGPackages = @($policyBundlePackages | Where-Object {
    $_.StartsWith(
        '/EFProcedural/Calysto/Internal/PCG/',
        [StringComparison]::OrdinalIgnoreCase)
})
$requiredCookSupportPackages = @(
    $fixedCookSupportPackages + $policyBundlePackages + $decalMaterialPackages +
        $internalPCGPackages |
        Sort-Object -Unique
)
$requiredPackages = @($requiredCookMaps + $requiredCookSupportPackages | Sort-Object -Unique)

$manifest = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in [IO.File]::ReadLines($manifestPath)) {
    $relativePath = ($line -split "`t", 2)[0].Trim().Replace('\', '/')
    if (-not [string]::IsNullOrWhiteSpace($relativePath)) { [void]$manifest.Add($relativePath) }
}
$manifestJoined = @($manifest) -join "`n"
$utocListing = (& $unrealPakPath $utocPath -List 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw "UnrealPak failed to list $utocPath (exit $LASTEXITCODE)." }
$pakListing = (& $unrealPakPath $pakPath -List 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw "UnrealPak failed to list $pakPath (exit $LASTEXITCODE)." }
$containerListing = $utocListing + [Environment]::NewLine + $pakListing

$mapSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($map in $requiredCookMaps) { [void]$mapSet.Add($map) }
$missingFromManifest = @($requiredPackages | Where-Object {
    -not (Test-ManifestPackagePresent -Manifest $manifest -PackageName $_ `
        -IsMap ($mapSet.Contains($_)))
})
$missingFromContainers = @($requiredPackages | Where-Object {
    -not (Test-ContainerPackagePresent -Listing $containerListing -PackageName $_ `
        -IsMap ($mapSet.Contains($_)))
})

$v6ManifestAssets = @($manifest | Where-Object {
    $_ -match '(?i)^NoShellForWinter/Content/_Game/Data/CalystoDungeon/V6/.+\.(?:uasset|umap)$'
})
$expectedV6ManifestAsset = Get-ManifestAssetPath $expectedPolicyPackage
$legacyInManifest = @($legacyPolicyPackages | Where-Object {
    Test-ManifestPackagePresent -Manifest $manifest -PackageName $_
})
$legacyInContainers = @($legacyPolicyPackages | Where-Object {
    Test-ContainerPackagePresent -Listing $containerListing -PackageName $_
})

$realisticBloodManifestAssets = @($manifest | Where-Object {
    $_ -match '(?i)(?:^|/)Content/RealisticBlood/.+\.(?:uasset|umap)$'
} | Sort-Object)
$deniedRealisticBloodPattern = '(?i)(?:^|/)Content/RealisticBlood/(?:Demo|_Blueprints|_LevelSequence|_Maps)(?:/|$)|' +
    '(?:^|/)Content/RealisticBlood/.*/Niagara(?:/|$)|' +
    '(?:^|/)Content/RealisticBlood/.*(?:/|^)(?:BP_|ABP_)[^/]*\.(?:uasset|umap)|' +
    '(?:^|/)Content/RealisticBlood/.*(?:Manny|Quinn)'
$deniedRealisticBloodManifestAssets = @($realisticBloodManifestAssets | Where-Object {
    [string]$_ -match $deniedRealisticBloodPattern
})
$deniedRealisticBloodContainerLines = @($containerListing -split "`r?`n" | Where-Object {
    [string]$_ -match $deniedRealisticBloodPattern
})

$baseDecalReceiptProperty = $decalReceipt.assets.PSObject.Properties[
    '/EFProcedural/Calysto/Internal/Materials/Decals/M_CalystoBloodDecal'
]
$baseDecalReceipt = if ($null -ne $baseDecalReceiptProperty) {
    $baseDecalReceiptProperty.Value
} else { $null }
$baseReceiptBloodDependencies = if ($null -ne $baseDecalReceipt) {
    @($baseDecalReceipt.realistic_blood_dependencies | Sort-Object -Unique)
} else { @() }
$unexpectedDirectBloodDependencies = [Collections.Generic.List[string]]::new()
foreach ($property in @($decalReceipt.assets.PSObject.Properties)) {
    foreach ($dependency in @($property.Value.realistic_blood_dependencies)) {
        if ($expectedBloodPackages -cnotcontains [string]$dependency) {
            $unexpectedDirectBloodDependencies.Add([string]$dependency)
        }
    }
}
$decalAssetHashMismatches = [Collections.Generic.List[string]]::new()
foreach ($property in @($decalReceipt.assets.PSObject.Properties)) {
    $packageName = [string]$property.Name
    $localFile = ConvertTo-LocalPackageFile $packageName $ProjectRoot
    if ([string]::IsNullOrWhiteSpace($localFile) -or
        -not (Test-Path -LiteralPath $localFile -PathType Leaf) -or
        [string]$property.Value.package_sha256 -cne
            (Get-FileHash -Algorithm SHA256 -LiteralPath $localFile).Hash) {
        $decalAssetHashMismatches.Add($packageName)
    }
}
$runtimeBlueprintHashMismatches = [Collections.Generic.List[string]]::new()
foreach ($blueprint in @($runtimeAssetsReceipt.blueprints)) {
    $packageName = [string]$blueprint.package
    $localFile = ConvertTo-LocalPackageFile $packageName $ProjectRoot
    if ([string]::IsNullOrWhiteSpace($localFile) -or
        -not (Test-Path -LiteralPath $localFile -PathType Leaf) -or
        [string]$blueprint.package_sha256 -cne
            (Get-FileHash -Algorithm SHA256 -LiteralPath $localFile).Hash) {
        $runtimeBlueprintHashMismatches.Add($packageName)
    }
}
$internalPCGHashMismatches = [Collections.Generic.List[string]]::new()
foreach ($property in @($internalPCGReceipt.assets.PSObject.Properties)) {
    $packageName = [string]$property.Name
    $localFile = ConvertTo-LocalPackageFile $packageName $ProjectRoot
    if ([string]::IsNullOrWhiteSpace($localFile) -or
        -not (Test-Path -LiteralPath $localFile -PathType Leaf) -or
        [string]$property.Value.package_sha256 -cne
            (Get-FileHash -Algorithm SHA256 -LiteralPath $localFile).Hash) {
        $internalPCGHashMismatches.Add($packageName)
    }
}
$bloodSourceHashMismatches = [Collections.Generic.List[string]]::new()
foreach ($sourceProperty in @($decalReceipt.sources.PSObject.Properties)) {
    $objectPath = [string]$sourceProperty.Value.object
    $packageName = ConvertTo-PackageName $objectPath
    $localFile = ConvertTo-LocalPackageFile $packageName $ProjectRoot
    if ($expectedBloodPackages -cnotcontains $packageName -or
        [string]::IsNullOrWhiteSpace($localFile) -or
        -not (Test-Path -LiteralPath $localFile -PathType Leaf) -or
        [string]$sourceProperty.Value.sha256 -cne
            (Get-FileHash -Algorithm SHA256 -LiteralPath $localFile).Hash) {
        $bloodSourceHashMismatches.Add($packageName)
    }
}
$cookClosureGuardHashMismatches = [Collections.Generic.List[string]]::new()
foreach ($property in @($cookClosureReceipt.guarded_asset_hashes.PSObject.Properties)) {
    $packageName = [string]$property.Name
    $localFile = ConvertTo-LocalPackageFile $packageName $ProjectRoot
    if ([string]::IsNullOrWhiteSpace($localFile) -or
        -not (Test-Path -LiteralPath $localFile -PathType Leaf) -or
        [string]$property.Value -cne
            (Get-FileHash -Algorithm SHA256 -LiteralPath $localFile).Hash) {
        $cookClosureGuardHashMismatches.Add($packageName)
    }
}

$authoringChecks = [ordered]@{
    policy_receipt_pass = ([string]$policyReceipt.status -ceq 'PASS')
    policy_object_exact = ([string]$policyReceipt.policy.object -ceq $expectedPolicyObject)
    policy_class_exact = ([string]$policyReceipt.policy.class -ceq $expectedPolicyClass)
    policy_four_hashes_valid = (
        (Test-Sha256 $policyReceipt.policy.hashes.gameplay) -and
        (Test-Sha256 $policyReceipt.policy.hashes.authoring) -and
        (Test-Sha256 $policyReceipt.policy.hashes.materials) -and
        (Test-Sha256 $policyReceipt.policy.hashes.decals)
    )
    policy_bundle_names_exact = (
        @(Compare-Object -CaseSensitive -ReferenceObject $expectedBundleNames `
            -DifferenceObject @($policyReceipt.policy.bundle_names)).Count -eq 0 -and
        @($policyReceipt.policy.bundle_names).Count -eq $expectedBundleNames.Count
    )
    cook_closure_receipt_pass = ([string]$cookClosureReceipt.status -ceq 'PASS')
    cook_closure_policy_identity_exact = (
        [string]$cookClosureReceipt.policy.object -ceq $expectedPolicyObject -and
        [string]$cookClosureReceipt.policy.class -ceq $expectedPolicyClass
    )
    authored_policy_bundle_matches_cook_closure_receipt = (
        [int]$policyReceipt.policy.bundle_asset_count -eq $policyBundleAssetPaths.Count -and
        [string]$policyReceipt.policy.bundle_asset_paths_sha256 -ceq
            (Get-Utf8StringSha256 $policyBundleCanonical) -and
        [int]$cookClosureReceipt.policy.bundle_asset_count -eq $policyBundleAssetPaths.Count -and
        [string]$cookClosureReceipt.policy.bundle_asset_paths_sha256 -ceq
            (Get-Utf8StringSha256 $policyBundleCanonical)
    )
    authored_policy_hashes_match_cook_closure_receipt = (
        [string]$policyReceipt.policy.hashes.gameplay -ceq
            [string]$cookClosureReceipt.policy.hashes.gameplay -and
        [string]$policyReceipt.policy.hashes.authoring -ceq
            [string]$cookClosureReceipt.policy.hashes.authoring -and
        [string]$policyReceipt.policy.hashes.materials -ceq
            [string]$cookClosureReceipt.policy.hashes.materials -and
        [string]$policyReceipt.policy.hashes.decals -ceq
            [string]$cookClosureReceipt.policy.hashes.decals
    )
    cook_closure_guarded_asset_hashes_are_fresh = (
        $cookClosureGuardHashMismatches.Count -eq 0)
    cook_closure_internal_pcg_contract_pass = (
        [string]$cookClosureReceipt.internal_pcg_closure.status -ceq 'PASS' -and
        @(Compare-Object -CaseSensitive -ReferenceObject $internalPCGPackages `
            -DifferenceObject @($cookClosureReceipt.internal_pcg_closure.packages)).Count -eq 0
    )
    policy_package_matches_receipt = (
        [string]$policyReceipt.policy.package_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $policyAssetPath).Hash
    )
    decal_receipt_pass = ([string]$decalReceipt.status -ceq 'PASS')
    decal_source_contract_exact = (
        [string]$decalReceipt.source_contract -ceq 'EXACTLY_T_SPLAT_04_AND_T_SPLAT_N_04'
    )
    decal_receipt_contains_exact_asset_set = (
        @(Compare-Object -CaseSensitive -ReferenceObject $decalMaterialPackages `
            -DifferenceObject @($decalReceipt.assets.PSObject.Properties.Name)).Count -eq 0 -and
        @($decalReceipt.assets.PSObject.Properties).Count -eq $decalMaterialPackages.Count
    )
    decal_master_direct_blood_closure_exact = (
        $null -ne $baseDecalReceipt -and
        @(Compare-Object -CaseSensitive -ReferenceObject $expectedBloodPackages `
            -DifferenceObject $baseReceiptBloodDependencies).Count -eq 0 -and
        $baseReceiptBloodDependencies.Count -eq $expectedBloodPackages.Count
    )
    decal_assets_have_no_unexpected_direct_blood_dependency = (
        $unexpectedDirectBloodDependencies.Count -eq 0
    )
    decal_asset_hashes_match_authoring_receipt = ($decalAssetHashMismatches.Count -eq 0)
    blood_texture_hashes_match_authoring_receipt = (
        $bloodSourceHashMismatches.Count -eq 0 -and
        @($decalReceipt.sources.PSObject.Properties).Count -eq $expectedBloodPackages.Count
    )
    runtime_blueprint_receipt_pass = ([string]$runtimeAssetsReceipt.status -ceq 'PASS')
    runtime_blueprints_exact = (
        @(Compare-Object -CaseSensitive -ReferenceObject $runtimeBlueprintPackages `
            -DifferenceObject @($runtimeAssetsReceipt.blueprints.package)).Count -eq 0 -and
        @($runtimeAssetsReceipt.blueprints).Count -eq $runtimeBlueprintPackages.Count
    )
    runtime_blueprint_hashes_match_authoring_receipt = (
        $runtimeBlueprintHashMismatches.Count -eq 0
    )
    internal_pcg_receipt_pass = ([string]$internalPCGReceipt.status -ceq 'PASS')
    internal_pcg_assets_exact = (
        @(Compare-Object -CaseSensitive -ReferenceObject $internalPCGPackages `
            -DifferenceObject @($internalPCGReceipt.assets.PSObject.Properties.Name)).Count -eq 0 -and
        @($internalPCGReceipt.assets.PSObject.Properties).Count -eq $internalPCGPackages.Count
    )
    internal_pcg_hashes_match_authoring_receipt = ($internalPCGHashMismatches.Count -eq 0)
}

$identityChecks = [ordered]@{
    v6_header_class_exact = Test-TextContains $policyHeaderText `
        'class EFPROCEDURALRUNTIME_API UEFCalystoDungeonDirectorPolicyV6Asset final'
    v6_hash_schema_exact = $policyHeaderText -cmatch (
        'static\s+constexpr\s+int32\s+HashSchemaVersion\s*=\s*' +
        [regex]::Escape([string]$expectedHashSchemaVersion) + '\s*;')
    v6_bundle_metadata_exact = Test-TextContains $policyHeaderText `
        'AssetBundles = "CalystoFloorV6"'
    config_authority_exact = Test-TextContains $gameConfigText `
        'DirectorPolicy=/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy'
    config_primary_asset_class_exact = (
        (Test-TextContains $gameConfigText 'PrimaryAssetType="EFCalystoDungeonDirectorPolicyV6Asset"') -and
        (Test-TextContains $gameConfigText `
            'AssetBaseClass="/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"')
    )
    config_always_cooks_internal_pcg_closure = Test-TextContains $gameConfigText `
        '+DirectoriesToAlwaysCook=(Path="/EFProcedural/Calysto/Internal/PCG")'
    config_has_no_legacy_calysto_authority_or_primary_asset_scan = -not (
        $gameConfigText -match '(?i)DirectorPolicy=/Game/_Game/Data/CalystoDungeon/V[345]/' -or
        $gameConfigText -match '(?i)PrimaryAssetType="EFCalystoDungeonDirectorPolicyV[345]' -or
        $gameConfigText -match '(?i)AssetBaseClass="/Script/EFProceduralRuntime\.EFCalystoDungeonDirectorPolicyV[345]'
    )
    policy_declares_exact_blood_closure = (
        @(Compare-Object -CaseSensitive -ReferenceObject $expectedBloodPackages `
            -DifferenceObject $policyBloodPackages).Count -eq 0 -and
        $policyBloodPackages.Count -eq $expectedBloodPackages.Count
    )
    policy_declares_exact_internal_pcg_closure = (
        @(Compare-Object -CaseSensitive -ReferenceObject $internalPCGPackages `
            -DifferenceObject $policyInternalPCGPackages).Count -eq 0 -and
        $policyInternalPCGPackages.Count -eq $internalPCGPackages.Count
    )
    native_v6_defaults_declare_exact_blood_closure = (
        @(Compare-Object -CaseSensitive -ReferenceObject $expectedBloodPackages `
            -DifferenceObject $nativeDefaultBloodPackages).Count -eq 0 -and
        $nativeDefaultBloodPackages.Count -eq $expectedBloodPackages.Count
    )
    policy_uses_only_shared_decal_instances = (
        @(Compare-Object -CaseSensitive `
            -ReferenceObject @($decalMaterialPackages | Where-Object { $_ -match '/MI_' }) `
            -DifferenceObject $policyDecalMaterials).Count -eq 0 -and
        $policyDecalMaterials.Count -eq 3
    )
    pcg_uses_v6_room_manifest = Test-TextContains $pcgSourceText 'BuildAndFreezeRoomManifestV6'
    pcg_uses_v6_decal_realization = Test-TextContains $pcgSourceText 'RealizeDecalsV6'
    graph_uses_v6_room_theme_node = Test-TextContains $graphSourceText `
        'UPCGCalystoAssignRoomThemeSettings'
    decal_pool_is_v6_owned = Test-TextContains $decalPoolSourceText `
        'UEFCalystoDecalPoolComponentV6::AcquireDecal'
}

$normalizedCookText = $cookText.Replace('\', '/')
$normalizedArchivePath = $archivePath.Replace('\', '/')
$exactCookArgument = '-map=' + (($requiredCookMaps + $requiredCookSupportPackages) -join '+')
$freshCookChecks = [ordered]@{
    uat_build_successful = (
        (Test-TextContains $cookText 'BUILD SUCCESSFUL') -and
        $cookText -match '(?i)(?:ExitCode=0|exiting with ExitCode=0)'
    )
    log_matches_archive_root = Test-TextContains $normalizedCookText $normalizedArchivePath
    command_requests_cook = $cookText -match '(?i)(?:^|\s)-cook(?:\s|$)'
    command_requests_stage = $cookText -match '(?i)(?:^|\s)-stage(?:\s|$)'
    command_requests_package = $cookText -match '(?i)(?:^|\s)-package(?:\s|$)'
    command_requests_archive = $cookText -match '(?i)(?:^|\s)-archive(?:\s|$)'
    command_requests_pak = $cookText -match '(?i)(?:^|\s)-pak(?:\s|$)'
    command_requests_iostore = $cookText -match '(?i)(?:^|\s)-iostore(?:\s|$)'
    configuration_matches = Test-TextContains $cookText ("-clientconfig=$Configuration")
    exact_map_and_support_closure_requested = Test-TextContains $cookText $exactCookArgument
    no_iterative_or_skipcook_flags = -not (
        $cookText -match '(?i)(?:^|\s)-(?:iterate|iterativecooking|skipcook|cookonthefly)(?:\s|$)')
}
$blockedLogPatterns = @(
    'Blueprint Runtime Error', 'ensure condition failed', 'Fatal error:',
    'requested GenerateLocal more than once', 'duplicate GenerateLocal'
)
$cookLogFindings = @($blockedLogPatterns | Where-Object { Test-TextContains $cookText $_ })

$contractChecks = [ordered]@{
    contract_schema_1 = ([int]$packagingContract.schema_version -eq 1)
    contract_target_exact = ([string]$packagingContract.target -ceq 'NoShellForWinter')
    contract_configuration_matches = ([string]$packagingContract.configuration -ceq $Configuration)
    contract_authority_schema_6 = ([int]$packagingContract.authority_schema_version -eq 6)
    contract_hash_schema_exact = (
        [int]$packagingContract.canonical_hash_schema_version -eq $expectedHashSchemaVersion)
    contract_policy_identity_exact = (
        [string]$packagingContract.policy_object -ceq $expectedPolicyObject -and
        [string]$packagingContract.policy_class -ceq $expectedPolicyClass)
    contract_policy_hashes_exact = (
        [string]$packagingContract.policy_hashes.gameplay -ceq
            [string]$policyReceipt.policy.hashes.gameplay -and
        [string]$packagingContract.policy_hashes.authoring -ceq
            [string]$policyReceipt.policy.hashes.authoring -and
        [string]$packagingContract.policy_hashes.materials -ceq
            [string]$policyReceipt.policy.hashes.materials -and
        [string]$packagingContract.policy_hashes.decals -ceq
            [string]$policyReceipt.policy.hashes.decals)
    contract_maps_exact = (
        @(Compare-Object -CaseSensitive -ReferenceObject $requiredCookMaps `
            -DifferenceObject @($packagingContract.cook_maps)).Count -eq 0 -and
        @($packagingContract.cook_maps).Count -eq $requiredCookMaps.Count)
    contract_support_closure_exact = (
        @(Compare-Object -CaseSensitive -ReferenceObject $requiredCookSupportPackages `
            -DifferenceObject @($packagingContract.cook_support_packages)).Count -eq 0 -and
        @($packagingContract.cook_support_packages).Count -eq $requiredCookSupportPackages.Count)
    contract_blood_closure_exact = (
        @(Compare-Object -CaseSensitive -ReferenceObject $expectedBloodPackages `
            -DifferenceObject @($packagingContract.calysto_realistic_blood_packages)).Count -eq 0 -and
        @($packagingContract.calysto_realistic_blood_packages).Count -eq $expectedBloodPackages.Count)
    contract_decal_material_closure_exact = (
        @(Compare-Object -CaseSensitive -ReferenceObject $decalMaterialPackages `
            -DifferenceObject @($packagingContract.calysto_decal_material_packages)).Count -eq 0 -and
        @($packagingContract.calysto_decal_material_packages).Count -eq $decalMaterialPackages.Count)
    contract_license_entitlement_gate_is_explicitly_pending = (
        [string]$packagingContract.realistic_blood_license_entitlement -ceq 'PENDING')
    contract_package_log_hash_exact = (
        [string]$packagingContract.package_log -ceq $cookLogPath -and
        (Test-Sha256 $packagingContract.package_log_sha256) -and
        [string]$packagingContract.package_log_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $cookLogPath).Hash)
    contract_policy_receipt_hash_exact = (
        [string]$packagingContract.source_evidence.policy_authoring_receipt_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $policyReceiptPath).Hash)
    contract_decal_receipt_hash_exact = (
        [string]$packagingContract.source_evidence.decal_authoring_receipt_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $decalReceiptPath).Hash)
    contract_runtime_assets_receipt_hash_exact = (
        [string]$packagingContract.source_evidence.runtime_assets_receipt_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeAssetsReceiptPath).Hash)
    contract_cook_closure_receipt_hash_exact = (
        [string]$packagingContract.source_evidence.cook_closure_receipt_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $cookClosureReceiptPath).Hash)
    contract_cook_closure_helper_hash_exact = (
        [string]$packagingContract.source_evidence.cook_closure_helper_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $cookClosureHelperPath).Hash)
    contract_policy_asset_hash_exact = (
        [string]$packagingContract.source_evidence.policy_v6_asset_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $policyAssetPath).Hash)
    contract_policy_source_hashes_exact = (
        [string]$packagingContract.source_evidence.policy_v6_header_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $policyHeader).Hash -and
        [string]$packagingContract.source_evidence.policy_v6_source_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $policySource).Hash)
    contract_runtime_source_hashes_exact = (
        [string]$packagingContract.source_evidence.pcg_subsystem_source_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $pcgSource).Hash -and
        [string]$packagingContract.source_evidence.graph_builder_v6_source_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $graphSource).Hash -and
        [string]$packagingContract.source_evidence.decal_pool_v6_source_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $decalPoolSource).Hash)
    contract_config_and_target_hashes_exact = (
        [string]$packagingContract.source_evidence.default_game_ini_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $gameConfigPath).Hash -and
        [string]$packagingContract.source_evidence.target_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $gameTargetPath).Hash)
}

$runtimeBinaries = @(Get-ChildItem -LiteralPath (Split-Path -Parent $gameExePath) -File |
    Where-Object { $_.Extension -in @('.exe', '.dll') } | Sort-Object FullName)
$requiredRuntimeTokens = @(
    'EFCalystoDungeonDirectorPolicyV6Asset', $expectedPolicyObject,
    'CalystoFloorV6', 'FEFCalystoPCGRuntimeGraphBuilderV6',
    'UPCGCalystoAssignRoomThemeSettings', 'AEFCalystoDecalPoolOwnerV6'
)
$legacyRuntimeTokens = @(
    "EFCalystoDungeonDirectorPolicyV$($retiredVersions[0])",
    "EFCalystoDungeonDirectorPolicyV$($retiredVersions[1])",
    "EFCalystoDungeonDirectorPolicyV$($retiredVersions[2])Asset"
) + $legacyPolicyPackages
$foundRequiredRuntimeTokens = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
$foundLegacyRuntimeTokens = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
foreach ($binary in $runtimeBinaries) {
    foreach ($token in @(Find-TokensInBinary $binary.FullName $requiredRuntimeTokens)) {
        [void]$foundRequiredRuntimeTokens.Add($token)
    }
    if ($ValidationMode -ceq 'FinalStrict') {
        foreach ($token in @(Find-TokensInBinary $binary.FullName $legacyRuntimeTokens)) {
            [void]$foundLegacyRuntimeTokens.Add($token)
        }
    }
}
$missingRuntimeTokens = @($requiredRuntimeTokens | Where-Object {
    -not $foundRequiredRuntimeTokens.Contains($_)
})

$checks = [ordered]@{}
foreach ($group in @($authoringChecks, $identityChecks, $freshCookChecks, $contractChecks)) {
    foreach ($entry in $group.GetEnumerator()) { $checks[$entry.Key] = [bool]$entry.Value }
}
$checks['all_required_packages_in_staged_manifest'] = ($missingFromManifest.Count -eq 0)
$checks['all_required_packages_in_iostore_or_pak'] = ($missingFromContainers.Count -eq 0)
$checks['exactly_one_v6_policy_in_staged_manifest'] = (
    $v6ManifestAssets.Count -eq 1 -and
    [string]$v6ManifestAssets[0] -ceq $expectedV6ManifestAsset)
$checks['legacy_policy_assets_absent_from_staged_manifest'] = ($legacyInManifest.Count -eq 0)
$checks['legacy_policy_assets_absent_from_iostore_or_pak'] = ($legacyInContainers.Count -eq 0)
$checks['calysto_blood_textures_present_in_staged_manifest'] = (
    @($expectedBloodPackages | Where-Object {
        -not (Test-ManifestPackagePresent -Manifest $manifest -PackageName $_)
    }).Count -eq 0)
$checks['calysto_blood_textures_present_in_iostore_or_pak'] = (
    @($expectedBloodPackages | Where-Object {
        -not (Test-ContainerPackagePresent -Listing $containerListing -PackageName $_)
    }).Count -eq 0)
$checks['realistic_blood_demo_niagara_blueprint_abp_manny_quinn_absent_from_manifest'] = (
    $deniedRealisticBloodManifestAssets.Count -eq 0)
$checks['realistic_blood_demo_niagara_blueprint_abp_manny_quinn_absent_from_containers'] = (
    $deniedRealisticBloodContainerLines.Count -eq 0)
$checks['cook_log_blocklist_empty'] = ($cookLogFindings.Count -eq 0)
$checks['v6_runtime_tokens_present'] = ($missingRuntimeTokens.Count -eq 0)

$legacyLocalFiles = @($legacyPolicyPackages | ForEach-Object {
    ConvertTo-LocalPackageFile $_ $ProjectRoot
} | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
if ($ValidationMode -ceq 'FinalStrict') {
    $checks['final_strict_legacy_policy_assets_absent_locally'] = ($legacyLocalFiles.Count -eq 0)
    $checks['final_strict_legacy_calysto_tokens_absent_from_runtime_binaries'] = (
        $foundLegacyRuntimeTokens.Count -eq 0)
    $checks['final_strict_legacy_calysto_paths_absent_from_config'] = -not (
        $gameConfigText -match '(?i)/Game/_Game/Data/CalystoDungeon/V[345](?:/|"|\))')
}

$projectDescriptor = Get-Content -Raw -LiteralPath $projectDescriptorPath | ConvertFrom-Json
foreach ($pluginName in @('DazToUnreal', 'EFCharacterCreationDazBridge')) {
    $entry = @($projectDescriptor.Plugins | Where-Object { [string]$_.Name -ceq $pluginName })
    $checks["${pluginName}_enabled_in_uproject"] = (
        $entry.Count -eq 1 -and [bool]$entry[0].Enabled)
}
$receiptVerification = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File $receiptGuardPath -ProjectRoot $ProjectRoot -TargetName NoShellForWinter `
    -Configuration $Configuration -VerifyOnly 2>&1 | Out-String
$receiptExitCode = $LASTEXITCODE
$checks['daz_receipt_verify_pass'] = ($receiptExitCode -eq 0)

$freshnessInputs = @(
    $policyHeader, $policySource, $pcgSource, $graphSource, $decalPoolSource,
    $gameConfigPath, $gameTargetPath, $projectDescriptorPath,
    $policyReceiptPath, $decalReceiptPath,
    $runtimeAssetsReceiptPath, $cookClosureReceiptPath, $policyAssetPath,
    $cookClosureHelperPath
)
foreach ($compiledRoot in @(
        'Plugins\EFProcedural\Source\EFProceduralRuntime',
        'Plugins\EFProcedural\Source\EFProceduralPCGRuntime',
        'Plugins\EFProcedural\Source\EFProceduralACFURuntime',
        'Plugins\EFProjectSystems\Source\EFProjectSystemsGameplay')) {
    $resolvedCompiledRoot = Join-Path $ProjectRoot $compiledRoot
    $freshnessInputs += @(Get-ChildItem -LiteralPath $resolvedCompiledRoot -Recurse -File |
        Where-Object { $_.Extension -in @('.h', '.cpp', '.inl', '.cs') } |
        ForEach-Object { $_.FullName })
}
foreach ($package in $policyBundlePackages + $decalMaterialPackages) {
    $localFile = ConvertTo-LocalPackageFile $package $ProjectRoot
    if (-not [string]::IsNullOrWhiteSpace($localFile)) { $freshnessInputs += $localFile }
}
$freshnessInputs = @($freshnessInputs | Sort-Object -Unique)
$latestInput = @($freshnessInputs | ForEach-Object { Get-Item -LiteralPath $_ } |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)[0]
$localBinaryItem = Get-Item -LiteralPath $localGameBinaryPath
$localReceiptItem = Get-Item -LiteralPath $localGameReceiptPath
$archiveBinaryItem = Get-Item -LiteralPath $gameExePath
$cookLogItem = Get-Item -LiteralPath $cookLogPath
$manifestItem = Get-Item -LiteralPath $manifestPath
$checks['local_game_binary_is_fresh'] = ($localBinaryItem.LastWriteTimeUtc -ge $latestInput.LastWriteTimeUtc)
$checks['local_game_receipt_is_fresh'] = ($localReceiptItem.LastWriteTimeUtc -ge $latestInput.LastWriteTimeUtc)
$checks['cook_log_is_fresh'] = ($cookLogItem.LastWriteTimeUtc -ge $latestInput.LastWriteTimeUtc)
$checks['manifest_is_fresh'] = ($manifestItem.LastWriteTimeUtc -ge $latestInput.LastWriteTimeUtc)
$localBinaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $localGameBinaryPath).Hash
$archiveBinaryHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameExePath).Hash
$checks['archive_binary_matches_local_binary'] = ($localBinaryHash -ceq $archiveBinaryHash)
$checks['packaging_contract_binary_hash_matches_local_and_archive'] = (
    [string]$packagingContract.source_evidence.game_binary_sha256 -ceq $localBinaryHash -and
    $localBinaryHash -ceq $archiveBinaryHash)
$checks['packaging_contract_game_receipt_hash_matches_local'] = (
    [string]$packagingContract.source_evidence.game_receipt_sha256 -ceq
        (Get-FileHash -Algorithm SHA256 -LiteralPath $localGameReceiptPath).Hash)
$checks['archive_binary_not_older_than_local_binary'] = (
    $archiveBinaryItem.LastWriteTimeUtc -ge $localBinaryItem.LastWriteTimeUtc)
$checks['no_hot_reload_livecoding_or_patch_binaries'] = @($runtimeBinaries | Where-Object {
    $_.Name -match '(?i)(?:LiveCoding|HotReload|\.patch_|-\d{4,})'
}).Count -eq 0

$failedChecks = @($checks.GetEnumerator() | Where-Object {
    -not [bool]$_.Value
} | ForEach-Object { $_.Key })
$status = if ($failedChecks.Count -eq 0) { 'PASS' } else { 'FAIL' }

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $OutputPath = Join-Path $ProjectRoot `
        "Saved\Migration\CalystoDungeonDirectorV6\PackageValidation_${Configuration}_$stamp.json"
}
$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $outputFullPath) {
    throw "Refusing to overwrite Calysto V6 package evidence: $outputFullPath"
}
[void][IO.Directory]::CreateDirectory((Split-Path -Parent $outputFullPath))

$hashes = [ordered]@{}
foreach ($file in @(
        $cookLogPath, $manifestPath, $packagingContractPath, $policyAssetPath,
        $policyReceiptPath, $decalReceiptPath, $runtimeAssetsReceiptPath,
        $cookClosureReceiptPath,
        $utocPath, $ucasPath, $pakPath, $globalUtocPath, $globalUcasPath,
        $gameExePath, $localGameBinaryPath, $localGameReceiptPath)) {
    $hashes[(Split-Path -Leaf $file)] =
        (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash
}

$report = [ordered]@{
    schema_version = 6
    artifact_schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = $status
    semantic = 'fresh_calysto_v6_cook_package_validation'
    validation_mode = $ValidationMode
    configuration = $Configuration
    archive_root = $archivePath
    cook_log = $cookLogPath
    policy = [ordered]@{
        object = $expectedPolicyObject
        class = $expectedPolicyClass
        hash_schema_version = $expectedHashSchemaVersion
        hashes = $policyReceipt.policy.hashes
        bundle_names = $expectedBundleNames
        bundle_asset_paths = $policyBundleAssetPaths
        bundle_packages = $policyBundlePackages
        native_default_bundle_asset_paths = $nativeDefaultBundleAssetPaths
        cook_closure_receipt = $cookClosureReceiptPath
    }
    realistic_blood = [ordered]@{
        entitlement_distribution_gate = 'PENDING'
        calysto_declared_dependency_closure = $policyBloodPackages
        expected_calysto_dependency_closure = $expectedBloodPackages
        project_owned_decal_materials = $decalMaterialPackages
        staged_project_realistic_blood_assets = $realisticBloodManifestAssets
        note = 'Other systems may legitimately cook other blood textures; only denied vendor feature content is globally blocked.'
        denied_manifest_assets = $deniedRealisticBloodManifestAssets
        denied_container_lines = $deniedRealisticBloodContainerLines
        unexpected_direct_dependencies = $unexpectedDirectBloodDependencies
        decal_asset_hash_mismatches = $decalAssetHashMismatches
        blood_source_hash_mismatches = $bloodSourceHashMismatches
    }
    runtime_blueprint_hash_mismatches = $runtimeBlueprintHashMismatches
    cook_closure_guard_hash_mismatches = $cookClosureGuardHashMismatches
    required_cook_maps = $requiredCookMaps
    required_cook_support_packages = $requiredCookSupportPackages
    missing_from_manifest = $missingFromManifest
    missing_from_containers = $missingFromContainers
    legacy_in_manifest = $legacyInManifest
    legacy_in_containers = $legacyInContainers
    legacy_local_files = $legacyLocalFiles
    missing_runtime_tokens = $missingRuntimeTokens
    found_legacy_runtime_tokens = @($foundLegacyRuntimeTokens)
    cook_log_findings = $cookLogFindings
    checks = $checks
    failed_checks = $failedChecks
    daz_receipt_output = $receiptVerification.Trim()
    hashes = $hashes
}
[IO.File]::WriteAllText(
    $outputFullPath,
    ($report | ConvertTo-Json -Depth 10) + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false)
)
Write-Output "Calysto Dungeon Director V6 package report: $outputFullPath"
if ($status -ne 'PASS') {
    throw "Calysto Dungeon Director V6 $Configuration package validation failed: $($failedChecks -join ', ')"
}
Write-Output 'Calysto Dungeon Director V6 package validation: PASS'
