[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$SourceRegistryPath = 'D:\Projects UE5\NoShellForWinter\Saved\Migration\Phase2\SourceAssetRegistry57.json',
    [string]$TargetRegistryPath = 'D:\Projects UE5\NoShellForWinter\Saved\Migration\Phase2\TargetAssetRegistry58.json',
    [string]$OutputCsv = 'D:\Projects UE5\NoShellForWinter\Docs\Migration\04_Content_Migration_Manifest.csv',
    [string]$SummaryJson = 'D:\Projects UE5\NoShellForWinter\Docs\Migration\Evidence\Phase2_Content_Manifest_Summary.json'
)

$ErrorActionPreference = 'Stop'

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputCsv)
$resolvedSummary = [System.IO.Path]::GetFullPath($SummaryJson)
if (-not $resolvedOutput.StartsWith($target + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Manifest output escapes target workspace: $resolvedOutput"
}
if (-not $resolvedSummary.StartsWith($target + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Summary output escapes target workspace: $resolvedSummary"
}

function ConvertTo-PackageName {
    param([string]$RelativePath)
    $extension = [System.IO.Path]::GetExtension($RelativePath)
    if ([string]::IsNullOrEmpty($extension)) {
        throw "Content package has no extension: $RelativePath"
    }
    $withoutExtension = $RelativePath.Substring(0, $RelativePath.Length - $extension.Length)
    return '/Game/' + $withoutExtension.Replace('\', '/')
}

function Get-PackageFiles {
    param([string]$ContentRoot)
    $result = @{}
    $files = @(Get-ChildItem -LiteralPath $ContentRoot -Recurse -File -Force | Where-Object {
        $_.Extension -ieq '.uasset' -or $_.Extension -ieq '.umap'
    })
    $index = 0
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($ContentRoot.Length).TrimStart('\')
        $package = ConvertTo-PackageName -RelativePath $relative
        if ($result.ContainsKey($package)) {
            throw "Duplicate package file detected: $package ($($result[$package].RelativePath), $relative)"
        }
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
        $result[$package] = [pscustomobject]@{
            PackageName = $package
            RelativePath = $relative
            Extension = $file.Extension.ToLowerInvariant()
            Length = [int64]$file.Length
            SHA256 = $hash
        }
        $index++
        if (($index % 500) -eq 0) {
            Write-Host ("Hashed {0}/{1} files under {2}" -f $index, $files.Count, $ContentRoot)
        }
    }
    return $result
}

function New-RegistryIndex {
    param([string]$Path)
    $index = @{}
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $index
    }
    $payload = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    foreach ($asset in @($payload.assets)) {
        $package = [string]$asset.package_name
        if (-not $package.StartsWith('/Game/', [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        if (-not $index.ContainsKey($package)) {
            $index[$package] = [pscustomobject]@{
                Classes = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
                Dependencies = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
                Referencers = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
                Tags = [System.Collections.Generic.List[object]]::new()
            }
        }
        $entry = $index[$package]
        if ([string]$asset.class_path) {
            [void]$entry.Classes.Add([string]$asset.class_path)
        }
        foreach ($dependency in @($asset.dependencies)) {
            if ([string]$dependency) { [void]$entry.Dependencies.Add([string]$dependency) }
        }
        foreach ($referencer in @($asset.referencers)) {
            if ([string]$referencer) { [void]$entry.Referencers.Add([string]$referencer) }
        }
        if ($asset.tags) {
            $entry.Tags.Add($asset.tags)
        }
    }
    return $index
}

function Join-SortedSet {
    param($Set)
    if ($null -eq $Set) { return '' }
    return (@($Set) | Sort-Object) -join ';'
}

function Get-SystemName {
    param([string]$PackageName)
    $p = $PackageName.ToLowerInvariant()
    if ($p -match 'storyselection|backstory|profession|characterbackground|trait') { return 'StorySelection' }
    if ($p -match 'charactercreation|charactercreator|preset') { return 'CharacterCreation' }
    if ($p -match 'chronicle|activityfeed') { return 'Chronicle' }
    if ($p -match 'sinfulascension|sinful_ascension|sxp|willpower|altar') { return 'SinfulAscension' }
    if ($p -match 'survival|innerstate|inner_state|needs|status') { return 'SurvivalNeedsStatus' }
    if ($p -match 'food|consumable|drink|pickup') { return 'Consumables' }
    if ($p -match 'defeat|struggle|knockout|pain') { return 'DefeatStruggle' }
    if ($p -match 'procedural|calysto|dungeon|pcglevel|pcg_|/pcg|doortolevel|/_game/hub|/hub/') { return 'ProceduralLevelFlow' }
    if ($p -match 'emote|locomotion|crawl|walkoverride|projectaction') { return 'EmotesLocomotionActions' }
    if ($p -match 'intimacy') { return 'Intimacy' }
    if ($p -match 'lockpick') { return 'Lockpicking' }
    if ($p -match 'dirtypawn|dirty_pawn') { return 'DirtyPawn' }
    if ($p -match 'tattoo|skinneddecal') { return 'Tattoo' }
    if ($p -match 'blink|eyeclose|eye_close') { return 'Blink' }
    if ($p -match 'clothingmorph|morph|daztounreal') { return 'DazMorphClothing' }
    if ($p -match '/input/|inputaction|inputmapping') { return 'Input' }
    if ($p -match 'enemy|combat|targeting|melee|ranged|mage|dummy') { return 'CombatEnemiesTargeting' }
    if ($p -match 'savegame|save_game|save/') { return 'Persistence' }
    if ($p -match '/ui/|_wbp$|_wb$|widget|hud') { return 'UI' }
    if ($p -match '/fullsample/') { return 'FullSample' }
    return 'Other'
}

function Get-Policy {
    param(
        [string]$PackageName,
        [bool]$HasSource,
        [bool]$HasTarget,
        [bool]$HashesMatch
    )

    $comparison = [System.StringComparison]::OrdinalIgnoreCase
    if ($PackageName.Equals('/Game/FullSample/Integrations/ATSIntegrations/Dialogue/SampleDialogueButton_WBP', $comparison) -or
        $PackageName.Equals('/Game/FullSample/Integrations/ATSIntegrations/Dialogue/ACF_Dialogue_WB', $comparison)) {
        return @('RETIRED_LEGACY_DIALOGUE', 'TARGET_CURRENT_ACFU_STACK', 'RETIRE_DO_NOT_MIGRATE', 'Replaced by ACFU 4.3.5 dialogue widgets; no target consumer.')
    }
    if ($PackageName.Equals('/Game/FullSample/Player', $comparison)) {
        return @('TARGET_AUTHORITATIVE_PLAYER_COLLISION', 'TARGET_PLAYER', 'KEEP_TARGET_RECOMPOSE_SOURCE_CONTRACT', 'Never overwrite Player; reintroduce source behavior through components/interfaces.')
    }
    if ($PackageName.StartsWith('/Game/DazToUnreal/', $comparison)) {
        if ($HasTarget) {
            return @('TARGET_AUTHORITATIVE_DAZ', 'TARGET_DAZ', 'KEEP_TARGET_DAZ_AUDIT_DEPENDENCIES', 'Never overwrite target Daz assets or current meshes.')
        }
        return @('SOURCE_ONLY_LEGACY_DAZ', 'TARGET_DAZ', 'DO_NOT_AUTO_MIGRATE_DAZ_REMAP_REFERENCES', 'Map dependencies to current target Multiple/Female/Male/Frederick assets.')
    }
    if ($HasTarget -and -not $HasSource) {
        return @('TARGET_ONLY', 'TARGET', 'KEEP_TARGET', 'Target-only UE 5.8 content.')
    }
    if ($HasSource -and $HasTarget -and $HashesMatch) {
        return @('IDENTICAL_OVERLAP', 'TARGET', 'KEEP_TARGET_IDENTICAL', 'Byte-identical package; no migration action.')
    }
    if ($HasSource -and $HasTarget) {
        if ($PackageName.StartsWith('/Game/FullSample/', $comparison)) {
            return @('DIFFERENT_FULLSAMPLE_COLLISION', 'THREE_WAY', 'COMPARE_FULLSAMPLE_IN_EDITOR', 'Target FullSample is authoritative; port only proven custom behavior/data.')
        }
        if ($PackageName.StartsWith('/Game/Characters/', $comparison)) {
            return @('DIFFERENT_CHARACTER_COLLISION', 'TARGET_CHARACTER', 'COMPARE_CHARACTER_REFERENCES_KEEP_TARGET', 'Preserve current target character assets unless an individual source dependency is proven.')
        }
        return @('DIFFERENT_OVERLAP', 'THREE_WAY', 'COMPARE_THREE_WAY_IN_EDITOR', 'Compare source 5.7, target 5.8, and desired contract before changing.')
    }
    if ($HasSource) {
        if ($PackageName.StartsWith('/Game/FullSample/', $comparison)) {
            return @('SOURCE_ONLY_FULLSAMPLE', 'SOURCE_BEHAVIOR', 'MIGRATE_SOURCE_ONLY_FULLSAMPLE_VIA_EDITOR', 'Source-only FullSample package; migrate with dependencies after ownership review.')
        }
        $customPrefixes = @(
            '/Game/UI/', '/Game/_Game/', '/Game/Procedural/', '/Game/Calysto/',
            '/Game/ExportedAnimations/', '/Game/KawaiiAnimations/', '/Game/QuangPhan/',
            '/Game/ShareTextures/', '/Game/DirtyPawnSystem/'
        )
        foreach ($prefix in $customPrefixes) {
            if ($PackageName.StartsWith($prefix, $comparison)) {
                return @('SOURCE_ONLY_PROJECT_CONTENT', 'SOURCE_BEHAVIOR', 'MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE', 'Project content candidate; never bulk-copy.')
            }
        }
        return @('SOURCE_ONLY_DEPENDENCY_CANDIDATE', 'SOURCE_OR_THIRD_PARTY', 'REVIEW_SOURCE_ONLY_DEPENDENCY', 'Migrate only if reached from an approved system anchor.')
    }
    throw "Unreachable package policy state: $PackageName"
}

Write-Host 'Loading Asset Registry exports...'
$sourceRegistry = New-RegistryIndex -Path $SourceRegistryPath
$targetRegistry = New-RegistryIndex -Path $TargetRegistryPath
Write-Host ("Registry packages: source={0}, target={1}" -f $sourceRegistry.Count, $targetRegistry.Count)

Write-Host 'Hashing source content packages...'
$sourceFiles = Get-PackageFiles -ContentRoot $sourceContent
Write-Host 'Hashing target content packages...'
$targetFiles = Get-PackageFiles -ContentRoot $targetContent

$packageNames = @($sourceFiles.Keys + $targetFiles.Keys | Sort-Object -Unique)
$rows = [System.Collections.Generic.List[object]]::new()

foreach ($package in $packageNames) {
    $hasSource = $sourceFiles.ContainsKey($package)
    $hasTarget = $targetFiles.ContainsKey($package)
    $sourceFile = if ($hasSource) { $sourceFiles[$package] } else { $null }
    $targetFile = if ($hasTarget) { $targetFiles[$package] } else { $null }
    $hashesMatch = $hasSource -and $hasTarget -and $sourceFile.SHA256 -eq $targetFile.SHA256
    $sourceReg = if ($sourceRegistry.ContainsKey($package)) { $sourceRegistry[$package] } else { $null }
    $targetReg = if ($targetRegistry.ContainsKey($package)) { $targetRegistry[$package] } else { $null }
    $policy = Get-Policy -PackageName $package -HasSource $hasSource -HasTarget $hasTarget -HashesMatch $hashesMatch
    $sourceDependencies = Join-SortedSet $sourceReg.Dependencies
    $targetDependencies = Join-SortedSet $targetReg.Dependencies
    $dependencyUnion = (@(
        if ($sourceReg) { @($sourceReg.Dependencies) }
        if ($targetReg) { @($targetReg.Dependencies) }
    ) | Sort-Object -Unique) -join ';'
    $sourceClasses = Join-SortedSet $sourceReg.Classes
    $targetClasses = Join-SortedSet $targetReg.Classes
    $assetClass = (@($sourceClasses, $targetClasses) | Where-Object { $_ } | Sort-Object -Unique) -join ';'

    $rows.Add([pscustomobject][ordered]@{
        PackageName = $package
        SourcePath = if ($hasSource) { $package } else { '' }
        TargetPath = if ($hasTarget) { $package } else { '' }
        SourceFile = if ($hasSource) { 'Content\' + $sourceFile.RelativePath } else { '' }
        TargetFile = if ($hasTarget) { 'Content\' + $targetFile.RelativePath } else { '' }
        AssetClass = $assetClass
        SourceAssetClass = $sourceClasses
        TargetAssetClass = $targetClasses
        Presence = if ($hasSource -and $hasTarget) { 'BOTH' } elseif ($hasSource) { 'SOURCE_ONLY' } else { 'TARGET_ONLY' }
        SourceLength = if ($hasSource) { $sourceFile.Length } else { '' }
        TargetLength = if ($hasTarget) { $targetFile.Length } else { '' }
        SourceSHA256 = if ($hasSource) { $sourceFile.SHA256 } else { '' }
        TargetSHA256 = if ($hasTarget) { $targetFile.SHA256 } else { '' }
        SourceRegistryPresent = [bool]$sourceReg
        TargetRegistryPresent = [bool]$targetReg
        SourceDependencyCount = if ($sourceReg) { $sourceReg.Dependencies.Count } else { 0 }
        TargetDependencyCount = if ($targetReg) { $targetReg.Dependencies.Count } else { 0 }
        SourceDependencies = $sourceDependencies
        TargetDependencies = $targetDependencies
        Dependencies = $dependencyUnion
        SourceReferencers = Join-SortedSet $sourceReg.Referencers
        TargetReferencers = Join-SortedSet $targetReg.Referencers
        System = Get-SystemName -PackageName $package
        Classification = $policy[0]
        Authority = $policy[1]
        Action = $policy[2]
        Result = 'PENDING'
        TestEvidence = 'Phase2 inventory + Asset Registry export'
        Commit = ''
        Notes = $policy[3]
    })
}

New-Item -ItemType Directory -Path (Split-Path -Parent $resolvedOutput) -Force | Out-Null
$rows | Export-Csv -LiteralPath $resolvedOutput -NoTypeInformation -Encoding UTF8

function Count-ByProperty {
    param([string]$Property)
    $result = [ordered]@{}
    foreach ($group in @($rows | Group-Object -Property $Property | Sort-Object Name)) {
        $result[[string]$group.Name] = $group.Count
    }
    return $result
}

$summary = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    source_root = $source
    target_root = $target
    source_registry = [ordered]@{
        path = $SourceRegistryPath
        sha256 = if (Test-Path -LiteralPath $SourceRegistryPath) { (Get-FileHash -Algorithm SHA256 -LiteralPath $SourceRegistryPath).Hash } else { $null }
        indexed_packages = $sourceRegistry.Count
    }
    target_registry = [ordered]@{
        path = $TargetRegistryPath
        sha256 = if (Test-Path -LiteralPath $TargetRegistryPath) { (Get-FileHash -Algorithm SHA256 -LiteralPath $TargetRegistryPath).Hash } else { $null }
        indexed_packages = $targetRegistry.Count
    }
    source_package_count = $sourceFiles.Count
    target_package_count = $targetFiles.Count
    union_package_count = $rows.Count
    counts_by_presence = Count-ByProperty -Property 'Presence'
    counts_by_classification = Count-ByProperty -Property 'Classification'
    counts_by_action = Count-ByProperty -Property 'Action'
    counts_by_system = Count-ByProperty -Property 'System'
    source_packages_missing_registry = @($rows | Where-Object { $_.SourcePath -and -not $_.SourceRegistryPresent }).Count
    target_packages_missing_registry = @($rows | Where-Object { $_.TargetPath -and -not $_.TargetRegistryPresent }).Count
    manifest_path = $resolvedOutput
    manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedOutput).Hash
    status = 'COMPLETE_PENDING_ACTION_EXECUTION'
}

New-Item -ItemType Directory -Path (Split-Path -Parent $resolvedSummary) -Force | Out-Null
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedSummary -Encoding UTF8
Write-Host ("Manifest rows={0}, source={1}, target={2}, output={3}" -f $rows.Count, $sourceFiles.Count, $targetFiles.Count, $resolvedOutput)
