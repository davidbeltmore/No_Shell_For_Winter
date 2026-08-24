[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $OutputPath = Join-Path $root `
        "Saved\Migration\CalystoDungeonDirectorV3\FinalCutoverAudit_$stamp.json"
}

$failures = [Collections.Generic.List[string]]::new()
function Add-CutoverFailure([string]$Message) {
    $failures.Add($Message)
}

$dataRoot = Join-Path $root 'Content\_Game\Data\CalystoDungeon'
$policyRelative = 'Content\_Game\Data\CalystoDungeon\V3\DA_CalystoDungeonDirectorPolicy.uasset'
$policyPath = Join-Path $root $policyRelative
$liveDataFiles = @(
    Get-ChildItem -LiteralPath $dataRoot -Recurse -File -Force |
        ForEach-Object { $_.FullName.Substring($root.Length + 1) }
)
if ($liveDataFiles.Count -ne 1 -or $liveDataFiles[0] -ne $policyRelative) {
    Add-CutoverFailure(
        "CalystoDungeon Content must contain only $policyRelative; found: $($liveDataFiles -join ', ')"
    )
}

$retirementManifestPath = Join-Path $root `
    'Saved\Migration\CalystoDungeonDirectorV3\RetiredPolicyAssets\manifest.json'
$retirementManifest = Get-Content -Raw -LiteralPath $retirementManifestPath |
    ConvertFrom-Json
$retiredFiles = @()
foreach ($entry in @($retirementManifest.files)) {
    $backupExists = Test-Path -LiteralPath $entry.backup -PathType Leaf
    $backupLength = if ($backupExists) {
        [int64](Get-Item -LiteralPath $entry.backup).Length
    } else { $null }
    $backupHash = if ($backupExists) {
        (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.backup).Hash
    } else { $null }
    $backupMatches = $backupExists `
        -and $backupLength -eq [int64]$entry.length `
        -and $backupHash -eq [string]$entry.sha256
    $sourceAbsent = -not (Test-Path -LiteralPath $entry.source)
    if (!$backupMatches) {
        Add-CutoverFailure("Retired backup mismatch: $($entry.backup)")
    }
    if (!$sourceAbsent) {
        Add-CutoverFailure("Retired policy asset remains live: $($entry.source)")
    }
    $retiredFiles += [ordered]@{
        source = [string]$entry.source
        backup = [string]$entry.backup
        source_absent = $sourceAbsent
        backup_matches = $backupMatches
        length = $backupLength
        sha256 = $backupHash
    }
}
if ($retiredFiles.Count -ne 6) {
    Add-CutoverFailure("Expected six retired policy backups; found $($retiredFiles.Count).")
}

$activeRoots = @(
    'Plugins'
    'Source'
    'Config'
    'Tools'
    '.agents'
    'NoShellForWinter.uproject'
) | ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path $_ }
$prohibitedTokens = @(
    ('/V' + '2')
    ('PolicyV' + '2')
    ('PlanV' + '2')
    ('THEME_' + 'V2')
    ('CALYSTO_' + 'PHASE2')
)
$legacyHits = @()
foreach ($token in $prohibitedTokens) {
    $matches = @(
        & rg -n --fixed-strings --hidden `
            --glob '!**/Intermediate/**' `
            --glob '!**/Binaries/**' `
            --glob '!**/__pycache__/**' `
            $token @activeRoots 2>$null
    )
    foreach ($match in $matches) {
        $legacyHits += "[$token] $match"
    }
}
if ($legacyHits.Count -ne 0) {
    Add-CutoverFailure("Active legacy scan found $($legacyHits.Count) matches.")
}

$pluginDescriptor = Get-Content -Raw -LiteralPath `
    (Join-Path $root 'Plugins\EFProcedural\EFProcedural.uplugin') |
    ConvertFrom-Json
if ([int]$pluginDescriptor.Version -ne 3 -or [string]$pluginDescriptor.VersionName -ne '3.0.0') {
    Add-CutoverFailure('EFProcedural plugin version is not the frozen 3 / 3.0.0 contract.')
}

$subsystemHeaderPath = Join-Path $root `
    'Plugins\EFProcedural\Source\EFProceduralRuntime\Public\Calysto\EFCalystoDungeonSubsystem.h'
$subsystemHeader = [IO.File]::ReadAllText($subsystemHeaderPath)
$requiredApis = @(
    'RequestStartNewRun'
    'RequestStartNewRunWithSeed'
    'RequestAdvanceFloor'
    'RequestRerollCurrentFloor'
    'RequestReplayCurrentFloor'
    'RequestTravelToFloor'
    'GetSnapshot'
    'SetNextFloorDirectorIntent'
    'ClearNextFloorDirectorIntent'
    'SubmitFloorOutcome'
    'GetResolvedFloorIntent'
    'GetRealizedFloorManifest'
)
$missingApis = @($requiredApis | Where-Object {
    $subsystemHeader.IndexOf($_, [StringComparison]::Ordinal) -lt 0
})
if ($missingApis.Count -ne 0) {
    Add-CutoverFailure("Required public APIs are missing: $($missingApis -join ', ')")
}
$removedApis = @(
    'RequestRegenerateFloor'
    'SetRunSeed'
    'SetForcedLayoutPreset'
    'SetForcedSpawnerPreset'
    'SetForcedThemePreset'
    'ClearForcedPresets'
)
$remainingRemovedApis = @($removedApis | Where-Object {
    $subsystemHeader.IndexOf($_, [StringComparison]::Ordinal) -ge 0
})
if ($remainingRemovedApis.Count -ne 0) {
    Add-CutoverFailure("Removed APIs remain declared: $($remainingRemovedApis -join ', ')")
}

$projectDescriptor = Get-Content -Raw -LiteralPath `
    (Join-Path $root 'NoShellForWinter.uproject') | ConvertFrom-Json
$dazPluginNames = @('DazToUnreal', 'EFCharacterCreationDazBridge')
$descriptorDaz = @(
    foreach ($name in $dazPluginNames) {
        $plugin = $projectDescriptor.Plugins | Where-Object Name -eq $name
        [ordered]@{
            name = $name
            enabled = $null -ne $plugin -and $plugin.Enabled -eq $true
        }
    }
)
foreach ($plugin in $descriptorDaz) {
    if (!$plugin.enabled) {
        Add-CutoverFailure("Project descriptor does not enable $($plugin.name).")
    }
}

$receiptPaths = [ordered]@{
    Editor = 'Binaries\Win64\NoShellForWinterEditor.target'
    Development = 'Binaries\Win64\NoShellForWinter.target'
    Shipping = 'Binaries\Win64\NoShellForWinter-Win64-Shipping.target'
}
$receiptDaz = [ordered]@{}
foreach ($receiptProperty in $receiptPaths.GetEnumerator()) {
    $receiptPath = Join-Path $root $receiptProperty.Value
    $receipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json
    $states = @(
        foreach ($name in $dazPluginNames) {
            $plugin = $receipt.Plugins | Where-Object Name -eq $name
            [ordered]@{
                name = $name
                enabled = $null -ne $plugin -and $plugin.Enabled -eq $true
            }
        }
    )
    $receiptDaz[$receiptProperty.Key] = $states
    foreach ($state in $states) {
        if (!$state.enabled) {
            Add-CutoverFailure("$($receiptProperty.Key) receipt does not enable $($state.name).")
        }
    }
}

$policyHash = if (Test-Path -LiteralPath $policyPath -PathType Leaf) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $policyPath).Hash
} else { $null }
$result = if ($failures.Count -eq 0) { 'PASS' } else { 'FAIL' }
$report = [ordered]@{
    schema_version = 3
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = $result
    git_head = (& git -C $root rev-parse HEAD).Trim()
    policy = [ordered]@{
        path = '/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy'
        sha256 = $policyHash
        sole_live_data_file = $liveDataFiles.Count -eq 1 -and $liveDataFiles[0] -eq $policyRelative
    }
    retired_policy_assets = $retiredFiles
    active_legacy_scan = [ordered]@{
        tokens = $prohibitedTokens
        hit_count = $legacyHits.Count
        hits = $legacyHits
    }
    plugin_version = [ordered]@{
        version = [int]$pluginDescriptor.Version
        version_name = [string]$pluginDescriptor.VersionName
    }
    public_api = [ordered]@{
        required = $requiredApis
        missing = $missingApis
        removed = $removedApis
        unexpectedly_present = $remainingRemovedApis
    }
    daz_descriptor = $descriptorDaz
    daz_receipts = $receiptDaz
    failures = @($failures)
}

$outputFullPath = [IO.Path]::GetFullPath($OutputPath)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputFullPath))
[IO.File]::WriteAllText(
    $outputFullPath,
    ($report | ConvertTo-Json -Depth 10),
    [Text.UTF8Encoding]::new($false)
)
$report | ConvertTo-Json -Depth 10
if ($result -ne 'PASS') {
    throw 'Dungeon Director V3 final cutover audit failed.'
}
