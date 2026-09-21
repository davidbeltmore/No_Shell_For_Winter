param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8",
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development",
    [string]$ArchiveRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function ConvertTo-CalystoPackageName {
    param([Parameter(Mandatory = $true)][string]$ObjectPath)
    $dot = $ObjectPath.IndexOf('.')
    if ($dot -ge 0) { return $ObjectPath.Substring(0, $dot) }
    return $ObjectPath
}

function Get-CalystoCppTextMacroAssetPaths {
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

function ConvertTo-CalystoLocalPackageFile {
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

$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"
$runUAT = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"
$clothingCatalogCompiler = Join-Path $ProjectRoot `
    "Tools\ClothingMorphV4\Compile-EFClothingMorphV4Catalog58.ps1"
$clothingCatalogCompilerPython = Join-Path $ProjectRoot `
    "Tools\ClothingMorphV4\Compile-EFClothingMorphV4Catalog58.py"
$gameTarget = Join-Path $ProjectRoot "Source\NoShellForWinter.Target.cs"
$gameConfig = Join-Path $ProjectRoot "Config\DefaultGame.ini"
$pcgSubsystemHeader = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Public\EFProceduralPCGSubsystem.h"
$pcgSubsystemSource = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\EFProceduralPCGSubsystem.cpp"
$policyV6AssetHeader = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralRuntime\Public\Calysto\EFCalystoDungeonDirectorPolicyV6.h"
$policyV6AssetSource = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralRuntime\Private\Calysto\EFCalystoDungeonDirectorPolicyV6.cpp"
$graphBuilderV6Source = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\Calysto\EFCalystoPCGRuntimeGraphV6.cpp"
$decalPoolV6Source = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\Calysto\EFCalystoDecalPoolV6.cpp"
$policyV6Asset = Join-Path $ProjectRoot `
    "Content\_Game\Data\CalystoDungeon\V6\DA_CalystoDungeonDirectorPolicy.uasset"
$policyV6AuthoringReceipt = Join-Path $ProjectRoot `
    "Saved\Migration\CalystoDungeonDirectorV6\CreateDirectorPolicyV6.json"
$decalV6AuthoringReceipt = Join-Path $ProjectRoot `
    "Saved\Migration\CalystoDungeonDirectorV6\CreateDecalMaterialsV6.json"
$runtimeAssetsV6AuthoringReceipt = Join-Path $ProjectRoot `
    "Saved\Migration\CalystoDungeonDirectorV6\CreateRuntimeAssetsV6.json"
$internalPCGV6AuthoringReceipt = Join-Path $ProjectRoot `
    "Saved\Migration\CalystoDungeonDirectorV6\CreateInternalPCGClosureV6.json"
$cookClosureV6Receipt = Join-Path $ProjectRoot `
    "Saved\Migration\CalystoDungeonDirectorV6\ValidateCookClosureV6.json"
$cookClosureV6Helper = Join-Path $ProjectRoot `
    "Tools\Migration\Validate-CalystoDungeonDirectorV6CookClosure.py"
$hashSchemaVersion = 3
$authoritySchemaVersion = 6
$expectedPolicyObject = "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy"
$expectedPolicyClass = "/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"
$expectedBloodPackages = @(
    "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04",
    "/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04"
)
$decalMaterialPackages = @(
    "/EFProcedural/Calysto/Internal/Materials/Decals/M_CalystoBloodDecal",
    "/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Ceiling",
    "/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor",
    "/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Wall"
)
$internalPCGPackages = @(
    "/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe",
    "/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe"
)
$calystoV6CompiledRoots = @(
    "Plugins\EFProcedural\Source\EFProceduralRuntime",
    "Plugins\EFProcedural\Source\EFProceduralPCGRuntime",
    "Plugins\EFProcedural\Source\EFProceduralACFURuntime",
    "Plugins\EFProjectSystems\Source\EFProjectSystemsGameplay"
) | ForEach-Object { Join-Path $ProjectRoot $_ }
$calystoV6CompiledInputs = @($calystoV6CompiledRoots | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Recurse -File | Where-Object {
        $_.Extension -in @('.h', '.cpp', '.inl', '.cs')
    } | ForEach-Object { $_.FullName }
} | Sort-Object -Unique)
$nativeDefaultBundleAssetPaths = @(Get-CalystoCppTextMacroAssetPaths $policyV6AssetSource)
if (-not (Test-Path -LiteralPath $cookClosureV6Receipt -PathType Leaf)) {
    throw "Run the read-only Calysto V6 cook-closure validator before packaging: $cookClosureV6Receipt"
}
$cookClosureReceipt = Get-Content -Raw -LiteralPath $cookClosureV6Receipt | ConvertFrom-Json
if ([string]$cookClosureReceipt.status -cne 'PASS') {
    throw "Calysto V6 cook-closure receipt is not PASS: $cookClosureV6Receipt"
}
$policyBundleAssetPaths = @($cookClosureReceipt.policy.bundle_asset_paths)
$policyBundlePackages = @($policyBundleAssetPaths | ForEach-Object {
    ConvertTo-CalystoPackageName $_
} | Sort-Object -Unique)
$calystoV6LocalAssetInputs = @(
    $policyBundlePackages + $decalMaterialPackages | ForEach-Object {
        ConvertTo-CalystoLocalPackageFile $_ $ProjectRoot
    } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique
)
$requiredCookMaps = @(
    "/Game/FullSample/Integrations/UIIntegrations/Level/MenuMap",
    "/Game/_Game/Hub/HUB",
    "/Game/Procedural/Maps/DungeonGeneration"
)
$requiredCookSupportPackages = @(
    "/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMaterial",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonShape",
    "/Game/Calysto/Dungeon/PCG/Function/PCG_SetDungeonMesh",
    "/Game/Calysto/Dungeon/PCG/Function/PCG_AddRamps",
    "/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple",
    "/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon",
    "/AscentCombatFramework/Blueprints/AI/Controllers/ACFMeleeAIControllerBP",
    "/AscentCombatFramework/Blueprints/AI/Controllers/ACFRangedAIControllerBP"
) + $policyBundlePackages + $decalMaterialPackages | Sort-Object -Unique
# UE 5.8 sends every -map token through AddFlexPathToCook, so a single exact
# package list cooks the runtime maps, V6 policy bundle, and exact project-owned
# decal/PCG closures without pulling unrelated blood-pack features.
$requiredCookPackageArgument = "-map=" + (($requiredCookMaps + $requiredCookSupportPackages) -join "+")

foreach ($requiredPath in @(
        $projectPath,
        $runUAT,
        $receiptGuard,
        $clothingCatalogCompiler,
        $clothingCatalogCompilerPython,
        $gameTarget,
        $gameConfig,
        $pcgSubsystemHeader,
        $pcgSubsystemSource,
        $policyV6AssetHeader,
        $policyV6AssetSource,
        $graphBuilderV6Source,
        $decalPoolV6Source,
        $policyV6Asset,
        $policyV6AuthoringReceipt,
        $decalV6AuthoringReceipt,
        $runtimeAssetsV6AuthoringReceipt,
        $internalPCGV6AuthoringReceipt,
        $cookClosureV6Receipt,
        $cookClosureV6Helper) + $calystoV6CompiledInputs +
        $calystoV6LocalAssetInputs) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required packaging path not found: $requiredPath"
    }
}

$internalPCGReceipt = Get-Content -Raw -LiteralPath $internalPCGV6AuthoringReceipt |
    ConvertFrom-Json
if ([string]$internalPCGReceipt.status -cne 'PASS' -or
    @($internalPCGReceipt.vendor_assets_saved).Count -ne 0 -or
    @($internalPCGReceipt.assets.PSObject.Properties).Count -ne 2 -or
    @(Compare-Object -CaseSensitive -ReferenceObject $internalPCGPackages `
        -DifferenceObject @($internalPCGReceipt.assets.PSObject.Properties.Name)).Count -ne 0) {
    throw "Calysto V6 internal PCG authoring receipt is not exact: $internalPCGV6AuthoringReceipt"
}

$clothingToolsRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $ProjectRoot "Tools\ClothingMorphV4"))
$clothingToolsPrefix = $clothingToolsRoot.TrimEnd('\') + '\'
foreach ($compilerPath in @($clothingCatalogCompiler, $clothingCatalogCompilerPython)) {
    $compilerFullPath = [System.IO.Path]::GetFullPath($compilerPath)
    if (-not $compilerFullPath.StartsWith(
            $clothingToolsPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "EF Clothing catalog compiler escapes the project-owned tools directory: $compilerFullPath"
    }
    if (-not (Test-Path -LiteralPath $compilerFullPath -PathType Leaf)) {
        throw "Required EF Clothing catalog compiler file is missing: $compilerFullPath"
    }
}

if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -ne 0) {
    throw "Close Unreal Editor before a fresh cook/package."
}

$projectDescriptor = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
foreach ($pluginName in @("DazToUnreal", "EFCharacterCreationDazBridge")) {
    $plugin = $projectDescriptor.Plugins | Where-Object { $_.Name -eq $pluginName }
    if ($null -eq $plugin -or $plugin.Enabled -ne $true) {
        throw "$pluginName must remain explicitly enabled during cook/package."
    }
}

$gameTargetSource = Get-Content -LiteralPath $gameTarget -Raw
if ($gameTargetSource -cmatch 'bUseLoggingInShipping\s*=\s*true\s*;') {
    throw (@(
        "bUseLoggingInShipping=true is incompatible with this installed Engine build."
        "Shipping evidence must use explicit project-owned Calysto V6 artifacts."
    ) -join " ")
}

$pcgSubsystemCppSource = Get-Content -LiteralPath $pcgSubsystemSource -Raw
$policyV6HeaderSource = Get-Content -LiteralPath $policyV6AssetHeader -Raw
$policyV6SourceText = Get-Content -LiteralPath $policyV6AssetSource -Raw
$graphBuilderV6SourceText = Get-Content -LiteralPath $graphBuilderV6Source -Raw
$decalPoolV6SourceText = Get-Content -LiteralPath $decalPoolV6Source -Raw
$hashSchemaPattern = 'static\s+constexpr\s+int32\s+HashSchemaVersion\s*=\s*' +
    [regex]::Escape([string]$hashSchemaVersion) + '\s*;'
if ($policyV6HeaderSource -cnotmatch $hashSchemaPattern) {
    throw "Calysto V6 policy does not declare canonical hash schema $hashSchemaVersion."
}
$efProceduralConfigText = [IO.File]::ReadAllText($gameConfig)
$efProceduralSectionPattern = '(?m)^\[/Script/EFProceduralRuntime\.EFProceduralSettings\]\s*$'
if ([regex]::Matches($efProceduralConfigText, $efProceduralSectionPattern).Count -ne 1) {
    throw "DefaultGame.ini must contain exactly one EFProceduralSettings section."
}
$requiredEFProceduralConfigTokens = @(
    '!ManagedMapNames=__ClearArray__',
    '+ManagedMapNames=DungeonGeneration',
    'DungeonActorClass=/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon.BP_MassiveDungeon_C',
    'StartPointActorClass=/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint.BP_StartPoint_C',
    'MeleeAIControllerClass=/AscentCombatFramework/Blueprints/AI/Controllers/ACFMeleeAIControllerBP.ACFMeleeAIControllerBP_C',
    'RangedAIControllerClass=/AscentCombatFramework/Blueprints/AI/Controllers/ACFRangedAIControllerBP.ACFRangedAIControllerBP_C'
)
foreach ($token in $requiredEFProceduralConfigTokens) {
    if ($efProceduralConfigText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "DefaultGame.ini is missing the exact Calysto runtime config token: $token"
    }
}
$requiredV6DataAssetConfigTokens = @(
    "DirectorPolicy=$expectedPolicyObject",
    'PrimaryAssetType="EFCalystoDungeonDirectorPolicyV6Asset"',
    'AssetBaseClass="/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset"',
    'Directories=((Path="/Game/_Game/Data/CalystoDungeon/V6"))'
)
foreach ($token in $requiredV6DataAssetConfigTokens) {
    if ($efProceduralConfigText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "DefaultGame.ini is missing the exact active V6 Data Asset config token: $token"
    }
}
$configuredCalystoDirectorLines = @($efProceduralConfigText -split "`r?`n" | Where-Object {
    $_ -match '^DirectorPolicy=/Game/_Game/Data/CalystoDungeon/'
})
if ($configuredCalystoDirectorLines.Count -ne 1 -or
    [string]$configuredCalystoDirectorLines[0] -cne "DirectorPolicy=$expectedPolicyObject") {
    throw "DefaultGame.ini must configure exactly one Calysto Director authority: $expectedPolicyObject"
}
$requiredPreservedProjectConfigTokens = @(
    '[/Script/EFCharacterCreationRuntime.EFCharacterCreationSettings]',
    'RootWidgetClass=/EFCharacterCreation/UI/WBP_EFCharacterCreationRoot.WBP_EFCharacterCreationRoot_C',
    '[/Script/EFClothingMorphRuntime.EFClothingMorphV2Settings]',
    'DirectorPolicy=/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector.DA_EFClothingMorphDirector',
    '+DirectoriesToAlwaysCook=(Path="/EFClothingMorph/_Internal/Compiled/V4")',
    '+MapsToCook=(FilePath="/Game/_Game/Hub/HUB")',
    'PrimaryAssetType="GameFeatureData"'
)
foreach ($token in $requiredPreservedProjectConfigTokens) {
    if ($efProceduralConfigText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "DefaultGame.ini lost a preserved cross-system config token: $token"
    }
}
foreach ($sourceContract in @(
        [pscustomobject]@{
            Text = $policyV6HeaderSource
            Tokens = @(
                'UEFCalystoDungeonDirectorPolicyV6Asset',
                'AssetBundles = "CalystoFloorV6"',
                'FEFCalystoStyleProfileV6', 'FEFCalystoRoomThemeProfileV6'
            )
            Description = "V6 Primary Data Asset contract"
        },
        [pscustomobject]@{
            Text = $policyV6SourceText
            Tokens = @(
                'PolicyId = TEXT("CalystoDungeonDirectorV6")',
                '/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04.T_Splat_04',
                '/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.T_Splat_N_04',
                '/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Floor',
                '/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Wall',
                '/EFProcedural/Calysto/Internal/Materials/Decals/MI_CalystoBloodDecal_Ceiling'
            )
            Description = "V6 policy and exact blood closure"
        },
        [pscustomobject]@{
            Text = $pcgSubsystemCppSource
            Tokens = @(
                'BuildAndFreezeRoomManifestV6', 'RealizeDecalsV6',
                'RecordReadinessMilestone', 'VisualsReady', 'DoorEnabled'
            )
            Description = "V6 PCG readiness and visual realization"
        },
        [pscustomobject]@{
            Text = $graphBuilderV6SourceText
            Tokens = @('FEFCalystoPCGRuntimeGraphBuilderV6', 'UPCGCalystoAssignRoomThemeSettings')
            Description = "V6 transient PCG graph"
        },
        [pscustomobject]@{
            Text = $decalPoolV6SourceText
            Tokens = @('UEFCalystoDecalPoolComponentV6::AcquireDecal', 'SetIsReplicatedByDefault(false)')
            Description = "V6 pooled decals"
        })) {
    foreach ($token in $sourceContract.Tokens) {
        if ($sourceContract.Text.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
            throw "Calysto V6 $($sourceContract.Description) is missing required contract token: $token"
        }
    }
}

$policyReceipt = Get-Content -Raw -LiteralPath $policyV6AuthoringReceipt | ConvertFrom-Json
$decalReceipt = Get-Content -Raw -LiteralPath $decalV6AuthoringReceipt | ConvertFrom-Json
$runtimeAssetsReceipt = Get-Content -Raw -LiteralPath $runtimeAssetsV6AuthoringReceipt | ConvertFrom-Json
if ([string]$policyReceipt.status -cne 'PASS' -or
    [string]$policyReceipt.policy.object -cne $expectedPolicyObject -or
    [string]$policyReceipt.policy.class -cne $expectedPolicyClass) {
    throw "Calysto V6 policy authoring receipt is not an exact PASS: $policyV6AuthoringReceipt"
}
foreach ($hashName in @('gameplay', 'authoring', 'materials', 'decals')) {
    if ([string]$policyReceipt.policy.hashes.$hashName -cnotmatch '^[A-Fa-f0-9]{64}$') {
        throw "Calysto V6 policy receipt has no valid $hashName SHA-256."
    }
}
if ([string]$policyReceipt.policy.package_sha256 -cne
    (Get-FileHash -Algorithm SHA256 -LiteralPath $policyV6Asset).Hash) {
    throw 'Calysto V6 policy asset no longer matches its authoring receipt.'
}
if ([string]$decalReceipt.status -cne 'PASS' -or
    [string]$decalReceipt.source_contract -cne 'EXACTLY_T_SPLAT_04_AND_T_SPLAT_N_04') {
    throw "Calysto V6 decal authoring receipt does not prove the exact blood closure: $decalV6AuthoringReceipt"
}
if ([string]$runtimeAssetsReceipt.status -cne 'PASS') {
    throw "Calysto V6 runtime Blueprint authoring receipt is not PASS: $runtimeAssetsV6AuthoringReceipt"
}
$closureHashNames = @('gameplay', 'authoring', 'materials', 'decals')
foreach ($hashName in $closureHashNames) {
    if ([string]$cookClosureReceipt.policy.hashes.$hashName -cne
        [string]$policyReceipt.policy.hashes.$hashName) {
        throw "Calysto V6 cook closure has a stale $hashName hash. Rerun its read-only validator."
    }
}
if ([string]$cookClosureReceipt.policy.package_sha256 -cne
        (Get-FileHash -Algorithm SHA256 -LiteralPath $policyV6Asset).Hash -or
    [string]$cookClosureReceipt.policy.bundle_asset_paths_sha256 -cne
        [string]$policyReceipt.policy.bundle_asset_paths_sha256 -or
    [int]$cookClosureReceipt.policy.bundle_asset_count -ne
        [int]$policyReceipt.policy.bundle_asset_count) {
    throw 'Calysto V6 cook-closure receipt no longer matches the authored policy receipt/asset.'
}
foreach ($assetHashProperty in @($cookClosureReceipt.guarded_asset_hashes.PSObject.Properties)) {
    $guardedLocalFile = ConvertTo-CalystoLocalPackageFile `
        ([string]$assetHashProperty.Name) $ProjectRoot
    if ([string]::IsNullOrWhiteSpace($guardedLocalFile) -or
        -not (Test-Path -LiteralPath $guardedLocalFile -PathType Leaf) -or
        [string]$assetHashProperty.Value -cne
            (Get-FileHash -Algorithm SHA256 -LiteralPath $guardedLocalFile).Hash) {
        throw "Calysto V6 guarded asset changed after cook-closure capture: $($assetHashProperty.Name)"
    }
}
$policyBloodPackages = @($policyBundlePackages | Where-Object {
    $_.StartsWith('/Game/RealisticBlood/', [StringComparison]::OrdinalIgnoreCase)
})
if (@(Compare-Object -CaseSensitive -ReferenceObject $expectedBloodPackages `
        -DifferenceObject $policyBloodPackages).Count -ne 0 -or
    $policyBloodPackages.Count -ne $expectedBloodPackages.Count) {
    throw "Calysto V6 policy RealisticBlood closure must be exactly T_Splat_04 + T_Splat_N_04."
}
$nativeDefaultBloodPackages = @($nativeDefaultBundleAssetPaths | ForEach-Object {
    ConvertTo-CalystoPackageName $_
} | Where-Object {
    $_.StartsWith('/Game/RealisticBlood/', [StringComparison]::OrdinalIgnoreCase)
} | Sort-Object -Unique)
if (@(Compare-Object -CaseSensitive -ReferenceObject $expectedBloodPackages `
        -DifferenceObject $nativeDefaultBloodPackages).Count -ne 0 -or
    $nativeDefaultBloodPackages.Count -ne $expectedBloodPackages.Count) {
    throw 'Calysto V6 native defaults no longer preserve the exact blood texture closure.'
}
Write-Host (@(
    "Calysto V6 packaging preflight: PASS"
    "authority=V6-primary-data-asset"
    "hash_schema=$hashSchemaVersion"
    "blood_closure=T_Splat_04+T_Splat_N_04"
    "runtime_package_tests=PENDING-until-definitive-V6-runners"
) -join " ")

$gameBinaryName = if ($Configuration -eq "Shipping") {
    "NoShellForWinter-Win64-Shipping.exe"
} else {
    "NoShellForWinter.exe"
}
$gameReceiptName = if ($Configuration -eq "Shipping") {
    "NoShellForWinter-Win64-Shipping.target"
} else {
    "NoShellForWinter.target"
}
$gameBinary = Join-Path $ProjectRoot "Binaries\Win64\$gameBinaryName"
$gameReceipt = Join-Path $ProjectRoot "Binaries\Win64\$gameReceiptName"
foreach ($buildOutput in @($gameBinary, $gameReceipt)) {
    if (!(Test-Path -LiteralPath $buildOutput -PathType Leaf)) {
        throw "Fresh $Configuration Game build output is missing: $buildOutput"
    }
    if ((Get-Item -LiteralPath $buildOutput).Length -le 0) {
        throw "Fresh $Configuration Game build output is empty: $buildOutput"
    }
}
$latestRequiredBuildInputUtc = @(
    $gameTarget,
    $pcgSubsystemHeader,
    $pcgSubsystemSource,
    $policyV6AssetHeader,
    $policyV6AssetSource,
    $graphBuilderV6Source,
    $decalPoolV6Source
) + $calystoV6CompiledInputs | ForEach-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc } |
    Sort-Object -Descending |
    Select-Object -First 1
foreach ($buildOutput in @($gameBinary, $gameReceipt)) {
    if ((Get-Item -LiteralPath $buildOutput).LastWriteTimeUtc -lt $latestRequiredBuildInputUtc) {
        throw (@(
            "$Configuration Game output predates the Calysto V6 runtime implementation: $buildOutput."
            "Run Build-NoShellForWinterGame58.ps1 for $Configuration before packaging."
        ) -join " ")
    }
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $ProjectRoot `
    -TargetName NoShellForWinter `
    -Configuration $Configuration `
    -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt verification failed before $Configuration cook/package."
}

Write-Host "Strictly certifying all enabled EF Clothing Morph V4 clothes before $Configuration cook/package..."
$clothingCertificationStamp = Get-Date -Format "yyyyMMdd_HHmmss"
$clothingCertificationReceipt = Join-Path $ProjectRoot `
    "Saved\ClothingMorphV4QA\compiler_receipt_$clothingCertificationStamp.json"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $clothingCatalogCompiler `
    -ProjectRoot $ProjectRoot `
    -EngineRoot $EngineRoot `
    -Stamp $clothingCertificationStamp
$clothingCompilerExitCode = $LASTEXITCODE
if ($clothingCompilerExitCode -ne 0) {
    throw (@(
        "EF Clothing Morph V4 catalog certification failed with exit code $clothingCompilerExitCode."
        "BuildCookRun was not started; fix the catalog/compiler error before packaging."
        "Compiler: $clothingCatalogCompiler"
    ) -join " ")
}
if (-not (Test-Path -LiteralPath $clothingCertificationReceipt -PathType Leaf)) {
    throw "EF Clothing Morph V4 certification receipt is missing: $clothingCertificationReceipt"
}
$clothingCertification = Get-Content -Raw -LiteralPath $clothingCertificationReceipt |
    ConvertFrom-Json
if ($clothingCertification.status -ne 'UE58_EF_CLOTHING_MORPH_V4_CATALOG_COMPILE_PASS' -or
    -not [bool]$clothingCertification.catalog_equality_gate -or
    -not [bool]$clothingCertification.strict_validation.fresh) {
    throw "EF Clothing Morph V4 certification receipt failed its strict gate: $clothingCertificationReceipt"
}
Write-Host "EF Clothing Morph V4 pre-package catalog certification: PASS"

if ([string]::IsNullOrWhiteSpace($ArchiveRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $ArchiveRoot = Join-Path $ProjectRoot `
        "Saved\Artifacts\PendingValidation\${Configuration}_$stamp"
}
$archiveFullPath = [System.IO.Path]::GetFullPath($ArchiveRoot)
if (Test-Path -LiteralPath $archiveFullPath) {
    throw "Fresh package archive already exists; choose a new path: $archiveFullPath"
}
$packageLogPath = "$archiveFullPath.Package.log"
if (Test-Path -LiteralPath $packageLogPath) {
    throw "Fresh package log already exists; choose a new archive path: $packageLogPath"
}

$originalGameConfigBytes = [IO.File]::ReadAllBytes($gameConfig)
$originalGameConfigHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameConfig).Hash
$originalGameConfigWriteTimeUtc = (Get-Item -LiteralPath $gameConfig).LastWriteTimeUtc
$gameConfigText = [IO.File]::ReadAllText($gameConfig)
$gameFeatureDataRule = '+PrimaryAssetTypesToScan=(PrimaryAssetType="GameFeatureData",AssetBaseClass="/Script/GameFeatures.GameFeatureData",bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=,SpecificAssets=,Rules=(Priority=-1,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))'
$gameFeatureDataRuleInjected = $gameConfigText.IndexOf(
    'PrimaryAssetType="GameFeatureData"',
    [StringComparison]::Ordinal) -lt 0
if ($gameFeatureDataRuleInjected) {
    $assetManagerSection = '[/Script/Engine.AssetManagerSettings]'
    $sectionIndex = $gameConfigText.IndexOf(
        $assetManagerSection,
        [StringComparison]::Ordinal)
    if ($sectionIndex -lt 0) {
        throw 'DefaultGame.ini has no AssetManagerSettings section for the packaging-only GameFeatureData rule.'
    }
    $insertIndex = $sectionIndex + $assetManagerSection.Length
    $gameConfigText = $gameConfigText.Insert(
        $insertIndex,
        [Environment]::NewLine + $gameFeatureDataRule)
    [IO.File]::WriteAllText($gameConfig, $gameConfigText, [Text.UTF8Encoding]::new($false))
    Write-Host 'Packaging-only GameFeatureData scan rule injected; DefaultGame.ini will be restored byte-exact.'
}

$uatExitCode = 1
$packageErrorActionPreference = $ErrorActionPreference
try {
    # Windows PowerShell surfaces native stderr records as ErrorRecord objects.
    # UAT legitimately writes diagnostics to stderr, so keep the pipeline alive
    # and enforce the concrete native exit code immediately afterward.
    $ErrorActionPreference = 'Continue'
    & $runUAT BuildCookRun `
        "-project=$projectPath" `
        -noP4 `
        -platform=Win64 `
        "-clientconfig=$Configuration" `
        -target=NoShellForWinter `
        -nocompileeditor `
        -skipbuild `
        $requiredCookPackageArgument `
        -cook `
        -stage `
        -pak `
        -iostore `
        -package `
        -archive `
        "-archivedirectory=$archiveFullPath" `
        -utf8output `
        2>&1 | Tee-Object -FilePath $packageLogPath
    $uatExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $packageErrorActionPreference
    if ($gameFeatureDataRuleInjected) {
        [IO.File]::WriteAllBytes($gameConfig, $originalGameConfigBytes)
        [IO.File]::SetLastWriteTimeUtc($gameConfig, $originalGameConfigWriteTimeUtc)
    }
}
$restoredGameConfigHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameConfig).Hash
if ($restoredGameConfigHash -cne $originalGameConfigHash) {
    throw 'DefaultGame.ini was not restored byte-exact after cook/package.'
}
if (-not (Test-Path -LiteralPath $packageLogPath -PathType Leaf) -or
    (Get-Item -LiteralPath $packageLogPath).Length -le 0) {
    throw "NoShellForWinter $Configuration package log was not persisted: $packageLogPath"
}
if ($uatExitCode -ne 0) {
    throw "NoShellForWinter $Configuration cook/package failed with exit code $uatExitCode."
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $ProjectRoot `
    -TargetName NoShellForWinter `
    -Configuration $Configuration `
    -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt verification failed after $Configuration cook/package."
}

$contractReceiptPath = Join-Path $archiveFullPath "CalystoV6PackagingContract.json"
$contractReceipt = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString("o")
    target = "NoShellForWinter"
    configuration = $Configuration
    authority_schema_version = $authoritySchemaVersion
    canonical_hash_schema_version = $hashSchemaVersion
    policy_object = $expectedPolicyObject
    policy_class = $expectedPolicyClass
    policy_hashes = $policyReceipt.policy.hashes
    policy_bundle_names = $policyReceipt.policy.bundle_names
    policy_bundle_asset_paths = $policyBundleAssetPaths
    policy_bundle_asset_paths_sha256 = $policyReceipt.policy.bundle_asset_paths_sha256
    cook_closure_receipt = $cookClosureV6Receipt
    internal_pcg_authoring_receipt = $internalPCGV6AuthoringReceipt
    cook_maps = $requiredCookMaps
    cook_support_packages = $requiredCookSupportPackages
    calysto_realistic_blood_packages = $expectedBloodPackages
    calysto_decal_material_packages = $decalMaterialPackages
    realistic_blood_license_entitlement = "PENDING"
    game_feature_data_rule_injected_for_package = $gameFeatureDataRuleInjected
    package_log = $packageLogPath
    package_log_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packageLogPath).Hash
    packaged_smoke_gate = "PENDING_UNTIL_DEFINITIVE_V6_RUNNER"
    packaged_determinism_gate = "PENDING_UNTIL_DEFINITIVE_V6_RUNNER"
    engine_logging_override = $false
    daz_receipt_verified_before_and_after = $true
    ef_clothing_morph_v4_catalog_certified = $true
    ef_clothing_morph_v4_enabled_clothes = [int]$clothingCertification.enabled_row_count
    ef_clothing_morph_v4_valid_bindings = [int]$clothingCertification.valid_binding_count
    ef_clothing_morph_v4_certification_receipt = $clothingCertificationReceipt
    source_evidence = [ordered]@{
        target_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameTarget).Hash
        default_game_ini_sha256 = $originalGameConfigHash
        policy_v6_asset_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $policyV6Asset).Hash
        policy_v6_header_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $policyV6AssetHeader).Hash
        policy_v6_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $policyV6AssetSource).Hash
        policy_authoring_receipt_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $policyV6AuthoringReceipt).Hash
        decal_authoring_receipt_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $decalV6AuthoringReceipt).Hash
        runtime_assets_receipt_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeAssetsV6AuthoringReceipt).Hash
        cook_closure_receipt_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $cookClosureV6Receipt).Hash
        cook_closure_helper_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $cookClosureV6Helper).Hash
        pcg_subsystem_header_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $pcgSubsystemHeader).Hash
        pcg_subsystem_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $pcgSubsystemSource).Hash
        graph_builder_v6_source_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $graphBuilderV6Source).Hash
        decal_pool_v6_source_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $decalPoolV6Source).Hash
        game_binary_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameBinary).Hash
        game_receipt_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameReceipt).Hash
        clothing_compiler_wrapper_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $clothingCatalogCompiler).Hash
        clothing_compiler_python_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $clothingCatalogCompilerPython).Hash
        clothing_certification_receipt_sha256 = `
            (Get-FileHash -Algorithm SHA256 -LiteralPath $clothingCertificationReceipt).Hash
    }
}
$contractJson = $contractReceipt | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText(
    $contractReceiptPath,
    $contractJson + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

Write-Host "NoShellForWinter $Configuration fresh cook/package: PASS"
Write-Host "Calysto V6 packaging contract: $contractReceiptPath"
Write-Host "Immutable package log: $packageLogPath"
Write-Output $archiveFullPath
