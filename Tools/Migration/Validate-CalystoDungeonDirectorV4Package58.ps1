[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [Parameter(Mandatory = $true)]
    [string]$CookLog,
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development",
    [ValidateSet("PreCutover", "FinalStrict")]
    [string]$ValidationMode = "FinalStrict",
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8",
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$ValidationMode = if ($ValidationMode -ieq "PreCutover") { "PreCutover" } else { "FinalStrict" }

function Test-TextContains {
    param([string]$Text, [string]$Value)
    return $Text.IndexOf($Value, [StringComparison]::OrdinalIgnoreCase) -ge 0
}

function ConvertTo-PackageName {
    param([string]$ObjectPath)
    $dot = $ObjectPath.IndexOf('.')
    if ($dot -ge 0) { return $ObjectPath.Substring(0, $dot) }
    return $ObjectPath
}

function Get-CppTextMacroPaths {
    param([string]$SourcePath)

    $text = [IO.File]::ReadAllText($SourcePath)
    $macroPattern = 'TEXT\(\s*(?<tokens>(?:"(?:\\.|[^"\\])*"\s*)+)\)'
    $literalPattern = '"(?<part>(?:\\.|[^"\\])*)"'
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($macro in [regex]::Matches(
            $text,
            $macroPattern,
            [Text.RegularExpressions.RegexOptions]::Singleline)) {
        $value = ""
        foreach ($literal in [regex]::Matches($macro.Groups["tokens"].Value, $literalPattern)) {
            $value += [regex]::Unescape($literal.Groups["part"].Value)
        }
        if ($value.StartsWith("/Game/", [StringComparison]::Ordinal)) {
            $paths.Add((ConvertTo-PackageName $value))
        }
    }
    return @($paths | Sort-Object -Unique)
}

function ConvertTo-StagedAssetPath {
    param(
        [string]$PackageName,
        [Collections.Generic.HashSet[string]]$MapPackages
    )
    $extension = if ($MapPackages.Contains($PackageName)) { ".umap" } else { ".uasset" }
    return "NoShellForWinter/Content/" + $PackageName.Substring("/Game/".Length) + $extension
}

function ConvertTo-ContainerAssetPath {
    param(
        [string]$PackageName,
        [Collections.Generic.HashSet[string]]$MapPackages
    )
    $extension = if ($MapPackages.Contains($PackageName)) { ".umap" } else { ".uasset" }
    return "Content/" + $PackageName.Substring("/Game/".Length) + $extension
}

function Find-TokensInBinary {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Tokens
    )

    $remaining = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($token in $Tokens) { [void]$remaining.Add($token) }
    $found = [Collections.Generic.List[string]]::new()
    $maximumTokenLength = @($Tokens | ForEach-Object { $_.Length } | Measure-Object -Maximum).Maximum
    $tailLength = [Math]::Max(0, [int]$maximumTokenLength - 1)
    $asciiTail = ""
    $unicodeTail = ""
    $buffer = [byte[]]::new(4MB)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        while ($remaining.Count -gt 0) {
            $read = $stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            $asciiText = $asciiTail + [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
            # Chunks start on even offsets and use an even buffer size, so UTF-16LE
            # symbol literals can be searched without losing code-unit alignment.
            $unicodeByteCount = $read - ($read % 2)
            $unicodeText = $unicodeTail + [Text.Encoding]::Unicode.GetString($buffer, 0, $unicodeByteCount)
            foreach ($token in @($remaining)) {
                if ((Test-TextContains $asciiText $token) -or (Test-TextContains $unicodeText $token)) {
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

function Find-RegexesInBinary {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][Collections.IDictionary]$Patterns
    )

    $remaining = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $compiled = @{}
    foreach ($entry in $Patterns.GetEnumerator()) {
        $label = [string]$entry.Key
        [void]$remaining.Add($label)
        $compiled[$label] = [Text.RegularExpressions.Regex]::new(
            [string]$entry.Value,
            [Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
                [Text.RegularExpressions.RegexOptions]::CultureInvariant
        )
    }

    $found = [Collections.Generic.List[object]]::new()
    # Keep a deliberately generous overlap and do not inspect the overlap until
    # the next chunk. This is essential for negative lookaheads such as
    # (?!V4): a token ending at a chunk boundary must see the next bytes before
    # it can be classified as legacy.
    $tailLength = 512
    $asciiTail = ""
    $unicodeTail = ""
    $buffer = [byte[]]::new(4MB)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        while ($remaining.Count -gt 0) {
            $read = $stream.Read($buffer, 0, $buffer.Length)
            if ($read -le 0) { break }
            $isFinalChunk = $stream.Position -ge $stream.Length
            $asciiText = $asciiTail + [Text.Encoding]::ASCII.GetString($buffer, 0, $read)
            $unicodeByteCount = $read - ($read % 2)
            $unicodeText = $unicodeTail + [Text.Encoding]::Unicode.GetString($buffer, 0, $unicodeByteCount)

            foreach ($label in @($remaining)) {
                $regex = $compiled[$label]
                $asciiMatch = $regex.Match($asciiText)
                $unicodeMatch = $regex.Match($unicodeText)
                $asciiCutoff = [Math]::Max(0, $asciiText.Length - $tailLength)
                $unicodeCutoff = [Math]::Max(0, $unicodeText.Length - $tailLength)
                $asciiFound = $asciiMatch.Success -and (
                    $isFinalChunk -or $asciiMatch.Index -lt $asciiCutoff
                )
                $unicodeFound = $unicodeMatch.Success -and (
                    $isFinalChunk -or $unicodeMatch.Index -lt $unicodeCutoff
                )
                if ($asciiFound -or $unicodeFound) {
                    $found.Add([pscustomobject]@{
                        label = $label
                        pattern = [string]$Patterns[$label]
                    })
                    [void]$remaining.Remove($label)
                }
            }

            if ($isFinalChunk) {
                $asciiTail = ""
                $unicodeTail = ""
            }
            else {
                $asciiTail = if ($asciiText.Length -gt $tailLength) {
                    $asciiText.Substring($asciiText.Length - $tailLength)
                }
                else { $asciiText }
                $unicodeTail = if ($unicodeText.Length -gt $tailLength) {
                    $unicodeText.Substring($unicodeText.Length - $tailLength)
                }
                else { $unicodeText }
            }
        }
    }
    finally { $stream.Dispose() }
    return @($found)
}

$projectPath = [IO.Path]::GetFullPath($ProjectRoot)
$archivePath = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$cookLogPath = (Resolve-Path -LiteralPath $CookLog).Path
$windowsRoot = Join-Path $archivePath "Windows"
$manifestPath = Join-Path $windowsRoot "Manifest_UFSFiles_Win64.txt"
$packagingContractPath = Join-Path $archivePath "CalystoV4PackagingContract.json"
$policySource = Join-Path $projectPath `
    "Plugins\EFProcedural\Source\EFProceduralRuntime\Private\Calysto\EFCalystoDungeonDirectorPolicyV4.cpp"
$policyHeader = Join-Path $projectPath `
    "Plugins\EFProcedural\Source\EFProceduralRuntime\Public\Calysto\EFCalystoDungeonDirectorPolicyV4.h"
$policyAsset = Join-Path $projectPath `
    "Content\_Game\Data\CalystoDungeon\V4\DA_CalystoDungeonDirectorPolicy.uasset"
$harnessSettingsSource = Join-Path $projectPath `
    "Plugins\EFProcedural\Source\EFProceduralRuntime\Private\Calysto\EFCalystoDungeonHarnessSettings.cpp"
$harnessSettingsHeader = Join-Path $projectPath `
    "Plugins\EFProcedural\Source\EFProceduralRuntime\Public\Calysto\EFCalystoDungeonHarnessSettings.h"
$gameConfig = Join-Path $projectPath "Config\DefaultGame.ini"
$projectDescriptorPath = Join-Path $projectPath "NoShellForWinter.uproject"
$pluginDescriptorPath = Join-Path $projectPath "Plugins\EFProcedural\EFProcedural.uplugin"
$unrealPak = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealPak.exe"
$receiptGuard = Join-Path $projectPath "Tools\Migration\Repair-DazPluginReceipt58.ps1"

foreach ($requiredPath in @(
        $windowsRoot, $manifestPath, $packagingContractPath, $cookLogPath, $policySource, $policyHeader,
        $policyAsset, $harnessSettingsSource, $harnessSettingsHeader, $gameConfig,
        $projectDescriptorPath, $pluginDescriptorPath,
        $unrealPak, $receiptGuard)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required V4 package validation path not found: $requiredPath"
    }
}

if ($archivePath -match '(?i)(?:CALYSTO_PHASE2|PHASE[ _-]*2)' -or
    $cookLogPath -match '(?i)(?:CALYSTO_PHASE2|PHASE[ _-]*2)') {
    throw "V4 validation refuses Phase 2 archive/log evidence. Use a fresh V4 package root and log."
}

$policyHeaderText = [IO.File]::ReadAllText($policyHeader)
$policySourceText = [IO.File]::ReadAllText($policySource)
$harnessSettingsSourceText = [IO.File]::ReadAllText($harnessSettingsSource)
$harnessSettingsHeaderText = [IO.File]::ReadAllText($harnessSettingsHeader)
$gameConfigText = [IO.File]::ReadAllText($gameConfig)
$pluginDescriptor = Get-Content -LiteralPath $pluginDescriptorPath -Raw | ConvertFrom-Json
$projectDescriptor = Get-Content -LiteralPath $projectDescriptorPath -Raw | ConvertFrom-Json
$packagingContract = Get-Content -LiteralPath $packagingContractPath -Raw | ConvertFrom-Json
$identityChecks = [ordered]@{
    policy_header_schema_4 = $policyHeaderText -match '(?m)int32\s+SchemaVersion\s*=\s*4\s*;'
    policy_header_generator_4 = $policyHeaderText -match '(?m)int32\s+GeneratorVersion\s*=\s*4\s*;'
    policy_header_stable_id = Test-TextContains $policyHeaderText 'CalystoDungeonDirectorV4'
    policy_validation_is_fail_closed = (
        $policySourceText -match 'SchemaVersion\s*!=\s*4' -and
        $policySourceText -match 'GeneratorVersion\s*!=\s*4' -and
        (Test-TextContains $policySourceText 'CalystoDungeonDirectorV4')
    )
    primary_asset_scan_is_v4 = (
        (Test-TextContains $gameConfigText 'PrimaryAssetType="EFCalystoDungeonDirectorPolicyV4"') -and
        (Test-TextContains $gameConfigText '/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy')
    )
    v4_policy_is_always_cooked = Test-TextContains $gameConfigText `
        '+DirectoriesToAlwaysCook=(Path="/Game/_Game/Data/CalystoDungeon/V4")'
    v4_clothing_pickup_is_always_cooked = Test-TextContains $gameConfigText `
        '+DirectoriesToAlwaysCook=(Path="/Game/_Game/Items/Clothing")'
    acf_armor_item_is_always_cooked = Test-TextContains $gameConfigText `
        '+DirectoriesToAlwaysCook=(Path="/Game/FullSample/Blueprints/Items/Armor")'
    v4_harness_policy_is_strongly_typed = Test-TextContains $harnessSettingsHeaderText `
        'TSoftObjectPtr<UEFCalystoDungeonDirectorPolicyV4> DirectorPolicy;'
    v4_harness_default_is_active = (
        (Test-TextContains $harnessSettingsSourceText 'TSoftObjectPtr<UEFCalystoDungeonDirectorPolicyV4>') -and
        (Test-TextContains $harnessSettingsSourceText '/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy')
    )
    v3_is_not_configured_as_authority = (
        -not (Test-TextContains $gameConfigText '/Game/_Game/Data/CalystoDungeon/V3') -and
        -not ($gameConfigText -match 'PrimaryAssetType\s*=\s*"EFCalystoDungeonDirectorPolicy"') -and
        -not (Test-TextContains $gameConfigText '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicy"') -and
        -not (Test-TextContains $harnessSettingsSourceText '/Game/_Game/Data/CalystoDungeon/V3') -and
        -not (Test-TextContains $harnessSettingsHeaderText 'TSoftObjectPtr<UEFCalystoDungeonDirectorPolicy> DirectorPolicy;')
    )
    plugin_version_is_4 = ([int]$pluginDescriptor.Version -eq 4 -and [string]$pluginDescriptor.VersionName -ceq '4.0.0')
}

$dazProjectChecks = [ordered]@{}
foreach ($pluginName in @("DazToUnreal", "EFCharacterCreationDazBridge")) {
    $entry = @($projectDescriptor.Plugins | Where-Object { [string]$_.Name -ceq $pluginName })
    $dazProjectChecks["${pluginName}_enabled_in_uproject"] = (
        $entry.Count -eq 1 -and [bool]$entry[0].Enabled
    )
}

$mapPackages = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($mapPackage in @(
        "/Game/FullSample/Integrations/UIIntegrations/Level/MenuMap",
        "/Game/_Game/Hub/HUB",
        "/Game/Procedural/Maps/DungeonGeneration")) {
    [void]$mapPackages.Add($mapPackage)
}

$catalogPackages = @(Get-CppTextMacroPaths $policySource)
$explicitPackages = @(
    "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_DungeonMesh",
    "/Game/Calysto/Dungeon/Data/DataAsset/Spawner/DA_DemoSpawner",
    "/Game/Calysto/Dungeon/Data/DataAsset/Dungeon/DA_RoomTheme",
    "/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon",
    "/Game/Calysto/Dungeon/PCG/PCG_MassiveDungeonMaster",
    "/Game/Calysto/Shared/PCG/PCG_ObjectTransformSimple",
    "/Game/Calysto/Dungeon/PCG/PCG_ObjectTransformSimpleDungeon",
    "/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4",
    "/Game/FullSample/Blueprints/Items/Armor/ACFArmorBP"
) + @($mapPackages)
$requiredPackages = @($catalogPackages + $explicitPackages | Sort-Object -Unique)

$enemyCatalogPackages = @($catalogPackages | Where-Object { $_ -match 'ACF(?:Defender|Gun|Mage|Melee|MM|Ranged)EnemyBP(?:Female|Male)$' })
$companionCatalogPackages = @($catalogPackages | Where-Object { $_ -match 'ACF(?:Base|Melee|Ranged)CompanionBP(?:Female|Male)$' })
$catalogChecks = [ordered]@{
    exact_12_non_dummy_enemies = ($enemyCatalogPackages.Count -eq 12)
    exact_6_gendered_companions = ($companionCatalogPackages.Count -eq 6)
    dummy_catalog_entries_absent = (@($catalogPackages | Where-Object { $_ -match '(?i)Dummy' }).Count -eq 0)
    winters_recall_required = ($requiredPackages -contains '/Game/_Game/Items/Companions/BP_Item_WintersRecall')
    v4_chests_required = (
        $requiredPackages -contains '/Game/_Game/Items/Chests/BP_CalystoLockedChestV4' -and
        $requiredPackages -contains '/Game/_Game/Items/Chests/BP_CalystoLockPickChestV4'
    )
    clothing_pickup_and_acf_armor_required = (
        $requiredPackages -contains '/Game/_Game/Items/Clothing/BP_CalystoArmorPickupV4' -and
        $requiredPackages -contains '/Game/FullSample/Blueprints/Items/Armor/ACFArmorBP'
    )
    forge_and_shrine_required = (
        $requiredPackages -contains '/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomForge' -and
        $requiredPackages -contains '/Game/Calysto/Dungeon/Data/DataAsset/Props/DA_RoomShrine'
    )
}

$manifestPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in [IO.File]::ReadLines($manifestPath)) {
    $relativePath = ($line -split "`t", 2)[0].Trim().Replace("\", "/")
    if (![string]::IsNullOrWhiteSpace($relativePath)) { [void]$manifestPaths.Add($relativePath) }
}

$requiredManifestPaths = @($requiredPackages | ForEach-Object {
    ConvertTo-StagedAssetPath $_ $mapPackages
})
$missingFromManifest = @($requiredManifestPaths | Where-Object { !$manifestPaths.Contains($_) })

$requiredPluginDescriptorPaths = @(
    "Engine/Plugins/DazToUnreal/DazToUnreal.uplugin",
    "Engine/Plugins/Animation/MLDeformer/NeuralMorphModel/NeuralMorphModel.uplugin",
    "NoShellForWinter/Plugins/EFCharacterCreationDazBridge/EFCharacterCreationDazBridge.uplugin"
)
$requiredPluginSupportPaths = @("Engine/Plugins/DazToUnreal/Config/FilterPlugin.ini")
$missingPluginDescriptorsFromManifest = @($requiredPluginDescriptorPaths | Where-Object {
    !$manifestPaths.Contains($_)
})
$missingPluginSupportFromManifest = @($requiredPluginSupportPaths | Where-Object {
    !$manifestPaths.Contains($_)
})

$utocPath = Join-Path $windowsRoot "NoShellForWinter\Content\Paks\NoShellForWinter-Windows.utoc"
$ucasPath = Join-Path $windowsRoot "NoShellForWinter\Content\Paks\NoShellForWinter-Windows.ucas"
$pakPath = Join-Path $windowsRoot "NoShellForWinter\Content\Paks\NoShellForWinter-Windows.pak"
$globalUtocPath = Join-Path $windowsRoot "NoShellForWinter\Content\Paks\global.utoc"
$globalUcasPath = Join-Path $windowsRoot "NoShellForWinter\Content\Paks\global.ucas"
$gameExe = if ($Configuration -eq "Shipping") {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter-Win64-Shipping.exe"
}
else {
    Join-Path $windowsRoot "NoShellForWinter\Binaries\Win64\NoShellForWinter.exe"
}
foreach ($packageFile in @($utocPath, $ucasPath, $pakPath, $globalUtocPath, $globalUcasPath, $gameExe)) {
    if (!(Test-Path -LiteralPath $packageFile -PathType Leaf)) {
        throw "Packaged V4 output is incomplete: $packageFile"
    }
}
$packagingContractChecks = [ordered]@{
    packaging_contract_schema_1 = ([int]$packagingContract.schema_version -eq 1)
    packaging_contract_configuration_matches = ([string]$packagingContract.configuration -ceq $Configuration)
    packaging_contract_uses_project_owned_telemetry = (
        [string]$packagingContract.telemetry_authority -ceq "project-owned-explicit-file" -and
        [int]$packagingContract.telemetry_schema_version -eq 1 -and
        -not [bool]$packagingContract.engine_logging_override
    )
    packaging_contract_binary_hash_matches_archive = (
        [string]$packagingContract.source_evidence.game_binary_sha256 -ceq
            (Get-FileHash -Algorithm SHA256 -LiteralPath $gameExe).Hash
    )
}

$utocListing = (& $unrealPak $utocPath -List 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw "UnrealPak failed to list $utocPath (exit $LASTEXITCODE)." }
$pakListing = (& $unrealPak $pakPath -List 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) { throw "UnrealPak failed to list $pakPath (exit $LASTEXITCODE)." }
$containerListing = $utocListing + [Environment]::NewLine + $pakListing

$requiredContainerPaths = @($requiredPackages | ForEach-Object {
    ConvertTo-ContainerAssetPath $_ $mapPackages
})
$missingFromContainer = @($requiredContainerPaths | Where-Object {
    ![bool](Test-TextContains $containerListing $_)
})
$missingPluginDescriptorsFromContainer = @($requiredPluginDescriptorPaths | Where-Object {
    ![bool](Test-TextContains $containerListing $_)
})
$missingPluginSupportFromContainer = @($requiredPluginSupportPaths | Where-Object {
    ![bool](Test-TextContains $containerListing $_)
})

$alwaysForbiddenTokens = @(
    "/CalystoDungeon/V1/", "/CalystoDungeon/V2/",
    "DT_CalystoFloorProfiles", "DT_CalystoSpawnerWeights", "DT_CalystoThemeWeights",
    "DT_CalystoGenerationOptions", "DT_CalystoSpawnerPresets", "DT_CalystoThemePresets",
    "PolicyV2", "PlanV2", "THEME_V2", "CALYSTO_PHASE2"
)
$v3RetirementTokens = @(
    "/CalystoDungeon/V3/", "PolicyV3", "PlanV3", "THEME_V3"
)
$forbiddenTokens = if ($ValidationMode -ceq "FinalStrict") {
    @($alwaysForbiddenTokens + $v3RetirementTokens)
}
else {
    @($alwaysForbiddenTokens)
}
$forbiddenInManifest = [Collections.Generic.List[object]]::new()
$manifestJoined = @($manifestPaths) -join "`n"
$forbiddenInContainer = [Collections.Generic.List[string]]::new()
foreach ($token in $forbiddenTokens) {
    if (Test-TextContains $manifestJoined $token) {
        $forbiddenInManifest.Add([pscustomobject]@{ token = $token })
    }
    if (Test-TextContains $containerListing $token) { $forbiddenInContainer.Add($token) }
}

$alwaysForbiddenBinaryTokens = @(
    "/CalystoDungeon/V1/", "/CalystoDungeon/V2/",
    "DT_CalystoFloorProfiles", "DT_CalystoSpawnerWeights",
    "DT_CalystoThemeWeights", "DT_CalystoGenerationOptions", "DT_CalystoSpawnerPresets",
    "DT_CalystoThemePresets", "PolicyV2", "PlanV2", "THEME_V2", "CALYSTO_PHASE2"
)
$v3RetirementBinaryTokens = @(
    "/CalystoDungeon/V3/", "CalystoDungeonDirectorV3", "EFCalystoDirectorPolicyV3",
    "PolicyV3", "PlanV3", "THEME_V3"
)
$forbiddenBinaryTokens = if ($ValidationMode -ceq "FinalStrict") {
    @($alwaysForbiddenBinaryTokens + $v3RetirementBinaryTokens)
}
else {
    @($alwaysForbiddenBinaryTokens)
}
$runtimeBinaryDirectory = Split-Path -Parent $gameExe
$runtimeBinaries = @(
    Get-ChildItem -LiteralPath $runtimeBinaryDirectory -File |
        Where-Object { $_.Extension -in @(".exe", ".dll") } |
        Sort-Object FullName
)
if (@($runtimeBinaries | Where-Object { $_.FullName -ceq $gameExe }).Count -ne 1) {
    throw "The packaged Game executable is not present in the runtime binary inventory: $gameExe"
}
$forbiddenInRuntimeBinary = @(
    foreach ($binary in $runtimeBinaries) {
        foreach ($token in @(Find-TokensInBinary $binary.FullName $forbiddenBinaryTokens)) {
            [pscustomobject]@{
                binary = $binary.FullName
                token = $token
            }
        }
    }
)

# Old V3 reflected types do not consistently carry a literal "V3" suffix.
# Match the semantic base name while excluding the V4 successor explicitly.
# These patterns close the gap where the old UClass/UStruct metadata survives
# in the executable or global IoStore metadata after the V3 asset is retired.
$legacyV3ReflectionPatterns = [ordered]@{
    director_policy = 'EFCalystoDungeonDirectorPolicy(?!V4)'
    director_intent = 'EFCalystoDirectorIntent(?!V4)'
    resolved_floor_intent = 'EFCalystoResolvedFloorIntent(?!V4)'
    realized_floor_manifest = 'EFCalystoRealizedFloorManifest(?!V4)'
    run_ecology_state = 'EFCalystoRunEcologyState(?!V4)'
    population_materializer = 'EFCalystoPopulationMaterializer(?!V4)'
    population_materialization_result = 'EFCalystoPopulationMaterializationResult(?!V4)'
    dungeon_math = 'EFCalystoDungeonDirectorMath(?!V4)'
    dungeon_types = 'EFCalystoDungeonTypes(?!V4)'
    dungeon_snapshot = 'EFCalystoDungeonSnapshot(?!V4)'
    floor_outcome = 'EFCalystoFloorOutcome(?!V4)'
    spawn_directive = 'EFCalystoSpawnDirective(?!V4)'
    companion_run_snapshot = 'EFCalystoCompanionRunSnapshot(?!V4)'
}
$legacyV3ReflectionInRuntimeBinary = @(
    foreach ($binary in $runtimeBinaries) {
        foreach ($finding in @(Find-RegexesInBinary $binary.FullName $legacyV3ReflectionPatterns)) {
            [pscustomobject]@{
                binary = $binary.FullName
                label = $finding.label
                pattern = $finding.pattern
            }
        }
    }
)
$runtimeMetadataFiles = @(
    Get-Item -LiteralPath $globalUtocPath
    Get-Item -LiteralPath $globalUcasPath
)
$legacyV3ReflectionInCookedMetadata = @(
    foreach ($metadataFile in $runtimeMetadataFiles) {
        foreach ($finding in @(Find-RegexesInBinary $metadataFile.FullName $legacyV3ReflectionPatterns)) {
            [pscustomobject]@{
                binary = $metadataFile.FullName
                label = $finding.label
                pattern = $finding.pattern
            }
        }
    }
)

# PreCutover intentionally permits only dormant V3 retirement residue. Record it
# explicitly so the receipt cannot hide what FinalStrict must later remove.
$dormantV3InManifest = [Collections.Generic.List[string]]::new()
$dormantV3InContainer = [Collections.Generic.List[string]]::new()
foreach ($token in $v3RetirementTokens) {
    if (Test-TextContains $manifestJoined $token) { $dormantV3InManifest.Add($token) }
    if (Test-TextContains $containerListing $token) { $dormantV3InContainer.Add($token) }
}
$dormantV3InRuntimeBinary = @(
    foreach ($binary in $runtimeBinaries) {
        foreach ($token in @(Find-TokensInBinary $binary.FullName $v3RetirementBinaryTokens)) {
            [pscustomobject]@{
                binary = $binary.FullName
                token = $token
            }
        }
    }
)

$cookText = [IO.File]::ReadAllText($cookLogPath)
$normalizedCookText = $cookText.Replace("\", "/")
$normalizedArchivePath = $archivePath.Replace("\", "/")
$freshCookChecks = [ordered]@{
    uat_build_successful = (
        (Test-TextContains $cookText 'BUILD SUCCESSFUL') -and
        ($cookText -match '(?i)(?:ExitCode=0|exiting with ExitCode=0)')
    )
    log_matches_archive_root = Test-TextContains $normalizedCookText $normalizedArchivePath
    command_requests_cook = $cookText -match '(?i)(?:^|\s)-cook(?:\s|$)'
    command_requests_stage = $cookText -match '(?i)(?:^|\s)-stage(?:\s|$)'
    command_requests_package = $cookText -match '(?i)(?:^|\s)-package(?:\s|$)'
    command_requests_archive = $cookText -match '(?i)(?:^|\s)-archive(?:\s|$)'
    command_requests_pak = $cookText -match '(?i)(?:^|\s)-pak(?:\s|$)'
    command_requests_iostore = $cookText -match '(?i)(?:^|\s)-iostore(?:\s|$)'
    configuration_matches = Test-TextContains $cookText ("-clientconfig=" + $Configuration)
    no_iterative_or_skipcook_flags = -not ($cookText -match '(?i)(?:^|\s)-(?:iterate|iterativecooking|skipcook|cookonthefly)(?:\s|$)')
    no_phase2_evidence = -not ($cookText -match '(?i)(?:CALYSTO_PHASE2|PHASE[ _-]*2)')
}

$blockedLogPatterns = @(
    "Object Transform", "GetAttributeFromPointIndex_0", "Blueprint Runtime Error",
    "ensure condition failed", "Fatal error:", "requested GenerateLocal more than once",
    "duplicate GenerateLocal"
)
$cookLogFindings = @($blockedLogPatterns | Where-Object { Test-TextContains $cookText $_ })

$receiptVerification = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $projectPath -TargetName NoShellForWinter `
    -Configuration $Configuration -VerifyOnly 2>&1 | Out-String
$receiptExitCode = $LASTEXITCODE

$checks = [ordered]@{}
foreach ($entry in $identityChecks.GetEnumerator()) { $checks[$entry.Key] = [bool]$entry.Value }
foreach ($entry in $dazProjectChecks.GetEnumerator()) { $checks[$entry.Key] = [bool]$entry.Value }
foreach ($entry in $catalogChecks.GetEnumerator()) { $checks[$entry.Key] = [bool]$entry.Value }
foreach ($entry in $freshCookChecks.GetEnumerator()) { $checks[$entry.Key] = [bool]$entry.Value }
foreach ($entry in $packagingContractChecks.GetEnumerator()) { $checks[$entry.Key] = [bool]$entry.Value }
$checks["all_required_assets_in_staged_manifest"] = ($missingFromManifest.Count -eq 0)
$checks["all_required_assets_in_iostore_or_pak"] = ($missingFromContainer.Count -eq 0)
$checks["daz_descriptors_in_staged_manifest"] = ($missingPluginDescriptorsFromManifest.Count -eq 0)
$checks["daz_descriptors_in_iostore_or_pak"] = ($missingPluginDescriptorsFromContainer.Count -eq 0)
$checks["daz_support_in_staged_manifest"] = ($missingPluginSupportFromManifest.Count -eq 0)
$checks["daz_support_in_iostore_or_pak"] = ($missingPluginSupportFromContainer.Count -eq 0)
if ($ValidationMode -ceq "FinalStrict") {
    $checks["no_v1_v2_v3_or_phase2_content_in_manifest"] = ($forbiddenInManifest.Count -eq 0)
    $checks["no_v1_v2_v3_or_phase2_content_in_containers"] = ($forbiddenInContainer.Count -eq 0)
    $checks["no_v1_v2_v3_or_phase2_symbols_in_runtime_binary"] = ($forbiddenInRuntimeBinary.Count -eq 0)
    $checks["no_legacy_v3_reflected_types_in_runtime_binaries"] = (
        $legacyV3ReflectionInRuntimeBinary.Count -eq 0
    )
    $checks["no_legacy_v3_reflected_types_in_cooked_metadata"] = (
        $legacyV3ReflectionInCookedMetadata.Count -eq 0
    )
}
else {
    $checks["no_v1_v2_or_phase2_content_in_manifest"] = ($forbiddenInManifest.Count -eq 0)
    $checks["no_v1_v2_or_phase2_content_in_containers"] = ($forbiddenInContainer.Count -eq 0)
    $checks["no_v1_v2_or_phase2_symbols_in_runtime_binary"] = ($forbiddenInRuntimeBinary.Count -eq 0)
    $checks["precutover_v3_is_dormant_not_authority"] = (
        [bool]$identityChecks.v4_harness_policy_is_strongly_typed -and
        [bool]$identityChecks.v4_harness_default_is_active -and
        [bool]$identityChecks.v3_is_not_configured_as_authority
    )
}
$checks["cook_log_blocklist_empty"] = ($cookLogFindings.Count -eq 0)
$checks["daz_receipt_verify_pass"] = ($receiptExitCode -eq 0)

$failedChecks = @($checks.GetEnumerator() | Where-Object { -not [bool]$_.Value } | ForEach-Object { $_.Key })
$status = if ($failedChecks.Count -eq 0) { "PASS" } else { "FAIL" }
$hashes = [ordered]@{}
foreach ($file in @($cookLogPath, $manifestPath, $packagingContractPath, $utocPath, $ucasPath, $pakPath, $globalUtocPath, $globalUcasPath, $gameExe, $policyAsset)) {
    $hashes[(Split-Path -Leaf $file)] = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash
}
foreach ($binary in $runtimeBinaries) {
    $hashes["runtime_binary::$($binary.Name)"] =
        (Get-FileHash -Algorithm SHA256 -LiteralPath $binary.FullName).Hash
}

$report = [ordered]@{
    schema_version = 4
    artifact_schema_version = 2
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString("o")
    status = $status
    semantic = if ($ValidationMode -ceq "FinalStrict") {
        "fresh_v4_cook_package_final_strict_validation"
    }
    else {
        "fresh_v4_cook_package_precutover_validation"
    }
    validation_mode = $ValidationMode
    allows_dormant_v3 = ($ValidationMode -ceq "PreCutover")
    validation_contract = [ordered]@{
        v4_cooked_and_active_required = $true
        v1_v2_or_phase2_allowed = $false
        dormant_v3_allowed = ($ValidationMode -ceq "PreCutover")
        v3_retirement_required = ($ValidationMode -ceq "FinalStrict")
    }
    configuration = $Configuration
    archive_root = $archivePath
    cook_log = $cookLogPath
    policy_asset = "/Game/_Game/Data/CalystoDungeon/V4/DA_CalystoDungeonDirectorPolicy"
    required_package_count = $requiredPackages.Count
    required_packages = $requiredPackages
    catalog_package_count = $catalogPackages.Count
    enemy_catalog_packages = $enemyCatalogPackages
    companion_catalog_packages = $companionCatalogPackages
    missing_from_manifest = $missingFromManifest
    missing_from_container = $missingFromContainer
    required_plugin_descriptors = $requiredPluginDescriptorPaths
    missing_plugin_descriptors_from_manifest = $missingPluginDescriptorsFromManifest
    missing_plugin_descriptors_from_container = $missingPluginDescriptorsFromContainer
    required_plugin_support = $requiredPluginSupportPaths
    missing_plugin_support_from_manifest = $missingPluginSupportFromManifest
    missing_plugin_support_from_container = $missingPluginSupportFromContainer
    forbidden_tokens = $forbiddenTokens
    forbidden_in_manifest = $forbiddenInManifest
    forbidden_in_container = $forbiddenInContainer
    forbidden_binary_tokens = $forbiddenBinaryTokens
    legacy_v3_reflection_patterns = $legacyV3ReflectionPatterns
    runtime_binaries_scanned = @($runtimeBinaries | ForEach-Object { $_.FullName })
    forbidden_in_runtime_binary = $forbiddenInRuntimeBinary
    runtime_metadata_scanned = @($runtimeMetadataFiles | ForEach-Object { $_.FullName })
    legacy_v3_reflection_in_runtime_binary = $legacyV3ReflectionInRuntimeBinary
    legacy_v3_reflection_in_cooked_metadata = $legacyV3ReflectionInCookedMetadata
    dormant_v3_observation = [ordered]@{
        manifest = @($dormantV3InManifest)
        container = @($dormantV3InContainer)
        runtime_binary = $dormantV3InRuntimeBinary
        reflected_types_in_runtime_binary = $legacyV3ReflectionInRuntimeBinary
        reflected_types_in_cooked_metadata = $legacyV3ReflectionInCookedMetadata
    }
    cook_log_findings = $cookLogFindings
    receipt_exit_code = $receiptExitCode
    receipt_verification = $receiptVerification.Trim()
    hashes = $hashes
    checks = $checks
    failed_checks = $failedChecks
}
$json = ConvertTo-Json -InputObject $report -Depth 12
if (![string]::IsNullOrWhiteSpace($OutputPath)) {
    $outputFullPath = [IO.Path]::GetFullPath($OutputPath)
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputFullPath))
    if (Test-Path -LiteralPath $outputFullPath) {
        throw "Refusing to overwrite V4 package evidence: $outputFullPath"
    }
    [IO.File]::WriteAllText($outputFullPath, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

Write-Output $json
if ($status -ne "PASS") {
    throw "Dungeon Director V4 $Configuration package validation failed: $($failedChecks -join ', ')"
}
