[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$packageScript = Join-Path $ProjectRoot 'Tools\Migration\Package-NoShellForWinter58.ps1'
$wrapperScript = Join-Path $ProjectRoot `
    'Tools\Migration\Invoke-NoShellForWinterPackageValidation58.ps1'
$validatorScript = Join-Path $ProjectRoot `
    'Tools\Migration\Validate-CalystoDungeonDirectorV6Package58.ps1'
$closureHelper = Join-Path $ProjectRoot `
    'Tools\Migration\Validate-CalystoDungeonDirectorV6CookClosure.py'
$internalPCGCreator = Join-Path $ProjectRoot `
    'Tools\Migration\Create-CalystoDungeonDirectorV6InternalPCGClosure.py'
$policySource = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralRuntime\Private\Calysto\EFCalystoDungeonDirectorPolicyV6.cpp'
$compatibilitySource = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\Calysto\EFCalystoPCGCookedCompatibility.cpp'
$graphSource = Join-Path $ProjectRoot `
    'Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\Calysto\EFCalystoPCGRuntimeGraphV6.cpp'
$files = @(
    $packageScript, $wrapperScript, $validatorScript, $closureHelper,
    $internalPCGCreator, $policySource, $compatibilitySource, $graphSource)
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required static-validation input is missing: $file"
    }
}

foreach ($script in @($packageScript, $wrapperScript, $validatorScript)) {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile(
        $script, [ref]$tokens, [ref]$errors)
    if ($errors.Count -ne 0) {
        throw "PowerShell AST validation failed for $script`: $($errors.Message -join '; ')"
    }
}

$packageText = [IO.File]::ReadAllText($packageScript)
$wrapperText = [IO.File]::ReadAllText($wrapperScript)
$validatorText = [IO.File]::ReadAllText($validatorScript)
$closureHelperText = [IO.File]::ReadAllText($closureHelper)
$internalPCGCreatorText = [IO.File]::ReadAllText($internalPCGCreator)
$policyText = [IO.File]::ReadAllText($policySource)
$compatibilityText = [IO.File]::ReadAllText($compatibilitySource)
$graphText = [IO.File]::ReadAllText($graphSource)
$combinedActiveText = $packageText + [Environment]::NewLine + $wrapperText

$requiredActiveTokens = @(
    '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset',
    '/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy',
    'CalystoV6PackagingContract.json',
    'Validate-CalystoDungeonDirectorV6Package58.ps1',
    'ValidateCookClosureV6.json',
    'PENDING_MISSING_DEFINITIVE_V6_RUNNER'
)
foreach ($token in $requiredActiveTokens) {
    if ($combinedActiveText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "Active package scripts are missing the V6 token: $token"
    }
}
$retiredContractSuffix = 'V' + [string](2 + 3)
$forbiddenDelegationTokens = @(
    "Validate-CalystoDungeonDirector${retiredContractSuffix}Package58.ps1",
    "Run-CalystoDungeonDirector${retiredContractSuffix}PackagedSmoke58.ps1",
    "Run-CalystoDungeonDirector${retiredContractSuffix}PackagedDeterminism58.ps1",
    "Calysto${retiredContractSuffix}PackagingContract.json",
    "EFCalystoDungeonDirectorPolicy${retiredContractSuffix}Asset"
)
foreach ($token in $forbiddenDelegationTokens) {
    if ($combinedActiveText.IndexOf($token, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Active package scripts still delegate to a retired Calysto contract: $token"
    }
}

$bloodObjectMatches = @([regex]::Matches(
    $policyText,
    '"(?<path>/Game/RealisticBlood/[^".]+)\.[^".]+"') | ForEach-Object {
        $_.Groups['path'].Value
    } | Sort-Object -Unique)
$expectedBloodPackages = @(
    '/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_04',
    '/Game/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04'
)
if (@(Compare-Object -CaseSensitive -ReferenceObject $expectedBloodPackages `
        -DifferenceObject $bloodObjectMatches).Count -ne 0 -or
    $bloodObjectMatches.Count -ne $expectedBloodPackages.Count) {
    throw "V6 policy blood closure drift: $($bloodObjectMatches -join ', ')"
}

$decalInstanceMatches = @([regex]::Matches(
    $policyText,
    '"(?<path>/EFProcedural/Calysto/Internal/Materials/Decals/MI_[^".]+)\.[^".]+"') |
    ForEach-Object { $_.Groups['path'].Value } | Sort-Object -Unique)
if ($decalInstanceMatches.Count -ne 3) {
    throw "V6 policy must reference exactly three shared decal MIs: $($decalInstanceMatches -join ', ')"
}

$requiredValidatorTokens = @(
    'EXACTLY_T_SPLAT_04_AND_T_SPLAT_N_04',
    'decal_master_direct_blood_closure_exact',
    'realistic_blood_demo_niagara_blueprint_abp_manny_quinn_absent_from_manifest',
    'realistic_blood_demo_niagara_blueprint_abp_manny_quinn_absent_from_containers',
    'Other systems may legitimately cook other blood textures',
    'cook_closure_receipt_pass'
)
foreach ($token in $requiredValidatorTokens) {
    if ($validatorText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "V6 package validator is missing the closure safeguard: $token"
    }
}
$requiredClosureHelperTokens = @(
    'get_cook_bundle_asset_paths',
    'realistic_blood_dependency_closure',
    'READ_ONLY_COOK_CLOSURE_VALIDATION',
    '"save_api_calls": 0'
)
foreach ($token in $requiredClosureHelperTokens) {
    if ($closureHelperText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "V6 read-only cook-closure helper is missing: $token"
    }
}
foreach ($forbiddenMutation in @(
        'EditorAssetLibrary.save_asset', 'EditorAssetLibrary.save_loaded_asset',
        'EditorAssetLibrary.delete_asset', 'EditorAssetLibrary.duplicate_asset',
        'AssetToolsHelpers.get_asset_tools().create_asset')) {
    if ($closureHelperText.IndexOf(
            $forbiddenMutation, [StringComparison]::Ordinal) -ge 0) {
        throw "V6 read-only cook-closure helper contains a mutation API: $forbiddenMutation"
    }
}

$requiredInternalPCGCreatorTokens = @(
    'CREATE_ONCE_FROM_FROZEN_VENDOR_GRAPHS',
    'VALIDATE_EXISTING_READ_ONLY',
    'prepare_new_internal_cooked_closure',
    'validate_internal_cooked_closure',
    'PCG_SetDungeonMeshCookedSafe',
    'PCG_AddRampsCookedSafe',
    'EditorAssetLibrary.duplicate_asset',
    'EditorAssetLibrary.save_loaded_asset'
)
foreach ($token in $requiredInternalPCGCreatorTokens) {
    if ($internalPCGCreatorText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "V6 internal PCG creator is missing its guarded contract: $token"
    }
}
$requiredTwoCloneTokens = @(
    'Result.ClonedGraphCount = 1;',
    'Result.ValidatedInternalGraphCount = 2;',
    'PCG_SetDungeonMeshCookedSafe.PCG_SetDungeonMeshCookedSafe',
    'PCG_AddRampsCookedSafe.PCG_AddRampsCookedSafe'
)
foreach ($token in $requiredTwoCloneTokens) {
    if ($compatibilityText.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
        throw "Cooked compatibility is missing the resident internal closure contract: $token"
    }
}
if ($graphText.IndexOf(
        'Compatibility.RuntimeGraph->HasAnyFlags(RF_Transient)',
        [StringComparison]::Ordinal) -lt 0) {
    throw 'V6 compositor no longer reuses the one cooked transient Master.'
}

$deniedPattern = '(?i)(?:^|/)Content/RealisticBlood/(?:Demo|_Blueprints|_LevelSequence|_Maps)(?:/|$)|' +
    '(?:^|/)Content/RealisticBlood/.*/Niagara(?:/|$)|' +
    '(?:^|/)Content/RealisticBlood/.*(?:/|^)(?:BP_|ABP_)[^/]*\.(?:uasset|umap)|' +
    '(?:^|/)Content/RealisticBlood/.*(?:Manny|Quinn)'
$allowedSamples = @(
    'NoShellForWinter/Content/RealisticBlood/_Commons/Textures/Decals/T_Splat_04.uasset',
    'NoShellForWinter/Content/RealisticBlood/_Commons/Textures/Decals/T_Splat_N_04.uasset',
    'NoShellForWinter/Content/RealisticBlood/_Commons/Textures/Splatters/Splatter06/T_BloodSplatter_06.uasset'
)
$deniedSamples = @(
    'NoShellForWinter/Content/RealisticBlood/Demo/Characters/Mannequins/Meshes/SKM_Manny.uasset',
    'NoShellForWinter/Content/RealisticBlood/Splatter/Niagara/NS_Blood.uasset',
    'NoShellForWinter/Content/RealisticBlood/_Blueprints/BP_Blood.uasset',
    'NoShellForWinter/Content/RealisticBlood/Demo/Animations/ABP_Quinn.uasset'
)
if (@($allowedSamples | Where-Object { $_ -match $deniedPattern }).Count -ne 0) {
    throw 'RealisticBlood blocklist incorrectly rejects an allowed texture sample.'
}
if (@($deniedSamples | Where-Object { $_ -notmatch $deniedPattern }).Count -ne 0) {
    throw 'RealisticBlood blocklist permits a denied vendor feature sample.'
}

Write-Output (@(
    'Calysto Dungeon Director V6 package-script static validation: PASS'
    "policy_blood_package_count=$($bloodObjectMatches.Count)"
    "shared_decal_instance_count=$($decalInstanceMatches.Count)"
    'intimacy_splatter06_allowlist_regression=PASS'
    'active_v5_delegation=ABSENT'
) -join ' ')
