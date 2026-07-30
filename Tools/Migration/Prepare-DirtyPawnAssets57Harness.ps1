[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "DIRTYPAWN57_HARNESS_GATE_FAIL: $Message"
    }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith(
        $fullRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-TextSha256 {
    param([string]$Text)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Text)
        return (-join ($algorithm.ComputeHash($bytes) | ForEach-Object { $_.ToString('X2') }))
    }
    finally {
        $algorithm.Dispose()
    }
}

function Assert-NoReparsePoints {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $items = @((Get-Item -LiteralPath $Path -Force)) + @(
        Get-ChildItem -LiteralPath $Path -Recurse -Force
    )
    foreach ($item in $items) {
        $isReparse = (
            [int]$item.Attributes -band [int][System.IO.FileAttributes]::ReparsePoint
        ) -ne 0
        Assert-True (-not $isReparse) "Reparse point, junction, or symlink is forbidden: $($item.FullName)"
    }
}

$expectedPackageCount = 15
$expectedSourceBytes = [int64]70130437
$expectedSourceFingerprint = '8F58CFD389DA0D63D2633D372C5DE251496EC53F53EEC15FE51AB160C46AEC36'
$assets = @(
    [pscustomobject]@{ Package = '/Game/_Game/Textures/leakage_sfhkcazc_4k/T_Leakage_sfhkcazc_2K_Mask'; RelativeFile = '_Game\Textures\leakage_sfhkcazc_4k\T_Leakage_sfhkcazc_2K_Mask.uasset'; Class = 'Texture2D'; Length = [int64]3103406; Sha256 = '524C8FA141E6822957DB8FB078162D23A1EBE51174B7352A0437C8AE1211FF6A' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Demo/Character/Characters/Mannequins/Textures/Manny/T_Manny_01_D'; RelativeFile = 'DirtyPawnSystem\Demo\Character\Characters\Mannequins\Textures\Manny\T_Manny_01_D.uasset'; Class = 'Texture2D'; Length = [int64]5671240; Sha256 = '47CF420ED20BFE200907775D89FB1A08F6D8FAF19A90C4DD9C46F2FB722DC23A' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Demo/StarterContent/Textures/T_Perlin_Noise_M'; RelativeFile = 'DirtyPawnSystem\Demo\StarterContent\Textures\T_Perlin_Noise_M.uasset'; Class = 'Texture2D'; Length = [int64]6804350; Sha256 = '9709239B17825D08BC603D4F9321D209AD980D4A1FB91242C5971BAFDA6ED421' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper'; RelativeFile = 'DirtyPawnSystem\Materials\DAZ\M_DirtyPawn_DAZSkinWrapper.uasset'; Class = 'Material'; Length = [int64]255891; Sha256 = '392D4DDFE8E8DD00278364E2F71385960720257614BB30679135C073B417D0F9' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/Blood/MF_Blood_Mask'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\Blood\MF_Blood_Mask.uasset'; Class = 'MaterialFunction'; Length = [int64]22408; Sha256 = '57BDD2550E8A1586A9999CAB8307CDB4BA52A926C9B0B9EDF2FCD66045BB852E' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SandSnow_Mask'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\MF_SandSnow_Mask.uasset'; Class = 'MaterialFunction'; Length = [int64]23681; Sha256 = '56FBDDDD6762F8CD54DAF49BE8B8B4EA098BD08626C79926FEDB35AA4CDFEA7D' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/MF_SkinnedHeightMask'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\MF_SkinnedHeightMask.uasset'; Class = 'MaterialFunction'; Length = [int64]12443; Sha256 = '02D08E46CC2DE5E92BD9559EDF802DA42FD42460E313F7B754CE0F129DD969F6' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/Mud/MF_MudHeight_BaseColor'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\Mud\MF_MudHeight_BaseColor.uasset'; Class = 'MaterialFunction'; Length = [int64]30871; Sha256 = '191537151EAD8A64A90F18C7BF19BD5711DDBAF7C5CEE9A2B122706F1CFA6124' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_BaseColor'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\Sand\MF_Sand_BaseColor.uasset'; Class = 'MaterialFunction'; Length = [int64]20559; Sha256 = '1370B08DEB27C2103029D615080E3A130AC0B50F3B62FAC78164A17B64F294B7' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Normal'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\Sand\MF_Sand_Normal.uasset'; Class = 'MaterialFunction'; Length = [int64]21777; Sha256 = '47CDF279EC8493331CACD9FFCB05754A0FD3F303E4786063E450BB1AE0839665' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/Sand/MF_Sand_Roughness'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\Sand\MF_Sand_Roughness.uasset'; Class = 'MaterialFunction'; Length = [int64]28930; Sha256 = '59D6D1E3B53A7AC68318BB618398C988C1A06FC3AFD72CC691535ECDA20AC4CB' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Materials/Functions/DPS/Smear/MF_Smear_Mask'; RelativeFile = 'DirtyPawnSystem\Materials\Functions\DPS\Smear\MF_Smear_Mask.uasset'; Class = 'MaterialFunction'; Length = [int64]24293; Sha256 = 'D75FDB123DEADA3C94BE1EC96D41370450784E7F6D103A84AE515D043BC8EDDA' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Textures/T_BloodScratch_Alpha'; RelativeFile = 'DirtyPawnSystem\Textures\T_BloodScratch_Alpha.uasset'; Class = 'Texture2D'; Length = [int64]3151543; Sha256 = 'B47114C5A32143F9D904E37D64CBB1DB3EEF0BC4F3C2E8F72CA9EEB0C3DB58F2' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Textures/T_Noise_Normal'; RelativeFile = 'DirtyPawnSystem\Textures\T_Noise_Normal.uasset'; Class = 'Texture2D'; Length = [int64]44168853; Sha256 = '4FCDADCE5A057901DC58A2D01C4F1222788A57776CEB9C300237C96A5962E252' },
    [pscustomobject]@{ Package = '/Game/DirtyPawnSystem/Textures/T_Perlin_Noise_M'; RelativeFile = 'DirtyPawnSystem\Textures\T_Perlin_Noise_M.uasset'; Class = 'Texture2D'; Length = [int64]6790192; Sha256 = 'A723B32150877CFFE0C0CCD63012645A6F164A7EA3060F997DC02F9B04FE233A' }
)
$allowedExternalDependencies = @(
    '/Engine/EngineMaterials/DefaultDiffuse',
    '/Engine/EngineMaterials/T_Default_Normal',
    '/Engine/EngineResources/DefaultTexture',
    '/Engine/Functions/Engine_MaterialFunctions01/Gradient/LinearGradient',
    '/Engine/Functions/Engine_MaterialFunctions01/ImageAdjustment/CheapContrast',
    '/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture',
    '/Script/InterchangeEngine'
)

Assert-True ($assets.Count -eq $expectedPackageCount) 'Internal allowlist count differs.'
Assert-True (@($assets.Package | Sort-Object -Unique).Count -eq $expectedPackageCount) 'Internal allowlist contains duplicate package names.'
Assert-True (@($assets.RelativeFile | Sort-Object -Unique).Count -eq $expectedPackageCount) 'Internal allowlist contains duplicate files.'
Assert-True ((($assets | Measure-Object -Property Length -Sum).Sum) -eq $expectedSourceBytes) 'Internal source-byte total differs.'
$fingerprintText = @(
    $assets |
        Sort-Object Package |
        ForEach-Object { '{0}|{1}|{2}' -f $_.Package, $_.Length, $_.Sha256 }
) -join "`n"
Assert-True ((Get-TextSha256 -Text $fingerprintText) -eq $expectedSourceFingerprint) 'Internal source fingerprint differs.'

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4\DirtyPawnAssets'
$harnessRoot = Join-Path $phaseRoot 'DirtyPawnAssets57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'DirtyPawnAssets57Harness.uproject'
$receiptPath = Join-Path $phaseRoot 'DirtyPawnAssets57HarnessReceipt.json'
$manifestPath = Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv'
$registryEvidencePath = Join-Path $target 'Saved\Migration\Phase2\SourceAssetRegistry57.json'
$safetyScript = Join-Path $target 'Tools\Migration\Test-DirtyPawnAssetsMigrationGates.ps1'
$preStageSafetyEvidence = Join-Path $phaseRoot 'Gates\PRE_STAGE_SafetyGate.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root (Join-Path $target 'Saved\Migration')) 'Harness escapes target Saved/Migration.'
Assert-True (-not (Test-IsUnderRoot -Path $harnessRoot -Root $source)) 'Harness unexpectedly lives under the read-only source.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'
Assert-True (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf) 'Target project descriptor is absent.'
Assert-True (Test-Path -LiteralPath $safetyScript -PathType Leaf) 'Batch safety-gate script is absent.'

& $safetyScript -Stage PRE_STAGE -SourceRoot $source -TargetRoot $target
Assert-True (Test-Path -LiteralPath $preStageSafetyEvidence -PathType Leaf) 'PRE_STAGE safety evidence was not produced.'
$preStageSafety = Get-Content -Raw -LiteralPath $preStageSafetyEvidence | ConvertFrom-Json
Assert-True ([string]$preStageSafety.status -eq 'DIRTYPAWN_ASSETS_SOURCE_PROTECTED_SAFETY_PASS') 'PRE_STAGE source/protected evidence is not PASS.'

Assert-True (Test-Path -LiteralPath $manifestPath -PathType Leaf) 'Migration manifest is absent.'
$manifest = @(Import-Csv -LiteralPath $manifestPath)
$manifestRows = @($manifest | Where-Object { $assets.Package -contains $_.PackageName })
Assert-True ($manifestRows.Count -eq $expectedPackageCount) 'Manifest does not contain exactly the allowlisted packages.'
foreach ($asset in $assets) {
    $rows = @($manifestRows | Where-Object PackageName -eq $asset.Package)
    Assert-True ($rows.Count -eq 1) "Manifest row count differs for $($asset.Package)."
    $row = $rows[0]
    Assert-True ([string]$row.Presence -eq 'SOURCE_ONLY') "Manifest presence is not SOURCE_ONLY for $($asset.Package)."
    Assert-True ([int64]$row.SourceLength -eq $asset.Length) "Manifest length differs for $($asset.Package)."
    Assert-True ([string]$row.SourceSHA256 -eq $asset.Sha256) "Manifest hash differs for $($asset.Package)."
    Assert-True ([string]$row.Action -eq 'MIGRATE_VIA_UNREAL_ASSETTOOLS_AFTER_DEPENDENCY_GATE') "Manifest action does not authorize AssetTools for $($asset.Package)."
}

Assert-True (Test-Path -LiteralPath $registryEvidencePath -PathType Leaf) 'UE 5.7 source Asset Registry evidence is absent.'
$registryEvidence = Get-Content -Raw -LiteralPath $registryEvidencePath | ConvertFrom-Json
$registryRows = @($registryEvidence.assets | Where-Object { $assets.Package -contains $_.package_name })
Assert-True ($registryRows.Count -eq $expectedPackageCount) 'Source Asset Registry evidence does not contain the exact allowlist.'
$packageNames = @($assets.Package)
$allDependencies = @()
foreach ($asset in $assets) {
    $rows = @($registryRows | Where-Object package_name -eq $asset.Package)
    Assert-True ($rows.Count -eq 1) "Source Asset Registry row count differs for $($asset.Package)."
    $registryRow = $rows[0]
    Assert-True ([string]$registryRow.object_path -eq ($asset.Package + '.' + ($asset.Package -split '/')[-1])) "Source object path differs for $($asset.Package)."
    Assert-True ([string]$registryRow.class_path -match ('asset_name: "' + [regex]::Escape($asset.Class) + '"')) "Source class differs for $($asset.Package)."
    $dependencies = @($registryRow.dependencies | Sort-Object -Unique)
    $unexpectedGame = @($dependencies | Where-Object { $_ -like '/Game/*' -and $packageNames -notcontains $_ })
    $unexpectedExternal = @($dependencies | Where-Object { $_ -notlike '/Game/*' -and $allowedExternalDependencies -notcontains $_ })
    Assert-True ($unexpectedGame.Count -eq 0) "Unexpected /Game dependency for $($asset.Package): $($unexpectedGame -join ', ')"
    Assert-True ($unexpectedExternal.Count -eq 0) "Unexpected external dependency for $($asset.Package): $($unexpectedExternal -join ', ')"
    $allDependencies += $dependencies
}
$actualExternalDependencies = @($allDependencies | Where-Object { $_ -notlike '/Game/*' } | Sort-Object -Unique)
$externalDelta = @(Compare-Object -ReferenceObject ($allowedExternalDependencies | Sort-Object) -DifferenceObject $actualExternalDependencies -SyncWindow 0)
Assert-True ($externalDelta.Count -eq 0) 'Frozen external dependency union differs.'
$wrapperPackage = '/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper'
$wrapperRow = @($registryRows | Where-Object package_name -eq $wrapperPackage)[0]
$wrapperGameDependencies = @($wrapperRow.dependencies | Where-Object { $_ -like '/Game/*' } | Sort-Object -Unique)
$expectedWrapperGameDependencies = @($packageNames | Where-Object { $_ -ne $wrapperPackage } | Sort-Object)
$wrapperDelta = @(Compare-Object -ReferenceObject $expectedWrapperGameDependencies -DifferenceObject $wrapperGameDependencies -SyncWindow 0)
Assert-True ($wrapperDelta.Count -eq 0) 'DAZ wrapper no longer references exactly the other fourteen allowlisted packages.'

$sourceRows = @()
foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    $targetFile = Join-Path $targetContent $asset.RelativeFile
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source asset is absent: $($asset.Package)"
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq $asset.Length) "Source length differs: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $sourceFile) -eq $asset.Sha256) "Source hash differs: $($asset.Package)"
    Assert-True (-not (Test-Path -LiteralPath $targetFile)) "Target collision exists: $($asset.Package)"
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($sourceFile, $extension)))) "Unexpected source sidecar exists for $($asset.Package): $extension"
        Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($targetFile, $extension)))) "Unexpected target sidecar exists for $($asset.Package): $extension"
    }
    $sourceRows += [ordered]@{
        package = $asset.Package
        class = $asset.Class
        source = $sourceFile
        target = $targetFile
        length = $asset.Length
        sha256 = $asset.Sha256
    }
}

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $phaseRoot) 'Refusing to rotate a harness outside the batch staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'DirtyPawnAssets57Harness') 'Unexpected harness directory name.'
    Assert-NoReparsePoints -Path $resolvedHarness
    $quarantineRoot = Join-Path $phaseRoot 'Quarantine'
    New-Item -ItemType Directory -Path $quarantineRoot -Force | Out-Null
    $quarantinePath = Join-Path $quarantineRoot ('DirtyPawnAssets57Harness_' + [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff'))
    Assert-True (-not (Test-Path -LiteralPath $quarantinePath)) 'Harness quarantine collision exists.'
    Move-Item -LiteralPath $resolvedHarness -Destination $quarantinePath
}

$stagedRows = @()
foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    $stagedFile = Join-Path $harnessContent $asset.RelativeFile
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile
    Assert-True ((Get-Item -LiteralPath $stagedFile).Length -eq $asset.Length) "Staged length differs: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $stagedFile) -eq $asset.Sha256) "Staged hash differs: $($asset.Package)"
    $sourceRow = @($sourceRows | Where-Object package -eq $asset.Package)[0]
    $stagedRows += [ordered]@{
        package = $asset.Package
        class = $asset.Class
        source = $sourceRow.source
        staged = $stagedFile
        target = $sourceRow.target
        length = $asset.Length
        sha256 = $asset.Sha256
    }
}
Assert-NoReparsePoints -Path $harnessRoot

[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated UE 5.7 read-only load and AssetTools harness for exactly fifteen DirtyPawn material contracts.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true }
    )
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    $targetFile = Join-Path $targetContent $asset.RelativeFile
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq $asset.Length) "Source length changed while staging: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $sourceFile) -eq $asset.Sha256) "Source hash changed while staging: $($asset.Package)"
    Assert-True (-not (Test-Path -LiteralPath $targetFile)) "Target asset appeared while staging: $($asset.Package)"
}

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_DIRTYPAWN_ASSETS57_HARNESS_PASS'
    package_count = $expectedPackageCount
    source_bytes = $expectedSourceBytes
    source_fingerprint = $expectedSourceFingerprint
    fingerprint_algorithm = 'SHA256 of LF-joined sorted package|length|sha256 rows; no trailing LF'
    class_counts = [ordered]@{
        Texture2D = 6
        MaterialFunction = 8
        Material = 1
    }
    source_root = $source
    target_root = $target
    harness_root = $harnessRoot
    harness_content = $harnessContent
    harness_project = $harnessProject
    staged_assets = $stagedRows
    allowed_external_dependencies = $allowedExternalDependencies
    wrapper_game_dependencies = $wrapperGameDependencies
    pre_stage_safety = [ordered]@{
        result = 'PASS'
        evidence = $preStageSafetyEvidence
        evidence_sha256 = Get-Sha256 -Path $preStageSafetyEvidence
    }
    source_tree_mounted = $false
    junctions_or_symlinks_present = $false
    source_package_saves = 0
    target_content_writes = 0
    raw_asset_policy = 'Exact hashes are copied only into target Saved/Migration for isolated UE 5.7 inspection. Live target Content may be populated only by Unreal AssetTools after the read-only gate passes.'
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "DIRTYPAWN_ASSETS57_HARNESS_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
