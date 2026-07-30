[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('PRE_STAGE', 'POST_MIGRATION57', 'POST_RESAVE58')]
    [string]$Stage,
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "DIRTYPAWN_ASSETS_SAFETY_GATE_FAIL: $Message"
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

Assert-True ($assets.Count -eq $expectedPackageCount) 'Internal allowlist count differs.'
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
$gateRoot = Join-Path $phaseRoot 'Gates'
$sourceGateScript = Join-Path $target 'Tools\Migration\Test-SourceReadOnly.ps1'
$protectedGateScript = Join-Path $target 'Tools\Migration\Test-ProtectedInvariants.ps1'
$sourceGateOutput = Join-Path $gateRoot ("{0}_SourceReadOnly.json" -f $Stage)
$protectedGateOutput = Join-Path $gateRoot ("{0}_ProtectedInvariants.json" -f $Stage)
$aggregateOutput = Join-Path $gateRoot ("{0}_SafetyGate.json" -f $Stage)

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-IsUnderRoot -Path $phaseRoot -Root (Join-Path $target 'Saved\Migration')) 'Gate evidence escapes target Saved/Migration.'
Assert-True (Test-Path -LiteralPath $sourceGateScript -PathType Leaf) 'Source read-only gate script is absent.'
Assert-True (Test-Path -LiteralPath $protectedGateScript -PathType Leaf) 'Protected-invariant gate script is absent.'

$sourceRows = @()
foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Frozen source asset is absent: $($asset.Package)"
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq $asset.Length) "Frozen source length changed: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $sourceFile) -eq $asset.Sha256) "Frozen source hash changed: $($asset.Package)"
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($sourceFile, $extension)))) "Unexpected source sidecar exists for $($asset.Package): $extension"
    }
    $sourceRows += [ordered]@{
        package = $asset.Package
        class = $asset.Class
        file = $sourceFile
        length = $asset.Length
        sha256 = $asset.Sha256
    }
}

New-Item -ItemType Directory -Path $gateRoot -Force | Out-Null
$powerShellExe = Join-Path $PSHOME 'powershell.exe'
Assert-True (Test-Path -LiteralPath $powerShellExe -PathType Leaf) 'Windows PowerShell executable is absent.'

& $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $sourceGateScript `
    -ProjectRoot $target `
    -SourceRoot $source `
    -OutputPath $sourceGateOutput
Assert-True ($LASTEXITCODE -eq 0) "Source read-only gate failed for $Stage."

& $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $protectedGateScript `
    -ProjectRoot $target `
    -OutputPath $protectedGateOutput
Assert-True ($LASTEXITCODE -eq 0) "Protected-invariant gate failed for $Stage."

$sourceGate = Get-Content -Raw -LiteralPath $sourceGateOutput | ConvertFrom-Json
$protectedGate = Get-Content -Raw -LiteralPath $protectedGateOutput | ConvertFrom-Json
Assert-True ([bool]$sourceGate.pass) 'Source read-only evidence is not PASS.'
Assert-True ([string]$protectedGate.result -eq 'PASS') 'Protected-invariant evidence is not PASS.'

$migrationEvidence = $null
$resaveEvidence = $null
$evidenceRows = @{}
if ($Stage -eq 'POST_MIGRATION57') {
    $migrationEvidencePath = Join-Path $phaseRoot 'DirtyPawnAssets57Migration.json'
    Assert-True (Test-Path -LiteralPath $migrationEvidencePath -PathType Leaf) 'UE 5.7 migration evidence is absent.'
    $migrationEvidence = Get-Content -Raw -LiteralPath $migrationEvidencePath | ConvertFrom-Json
    Assert-True ([string]$migrationEvidence.status -eq 'ASSETTOOLS_EXACT_DIRTYPAWN_ASSETS_MIGRATION_PASS') 'UE 5.7 migration evidence is not PASS.'
    Assert-True ([int]$migrationEvidence.package_count -eq $expectedPackageCount) 'UE 5.7 migration evidence count differs.'
    Assert-True ([string]$migrationEvidence.source_fingerprint -eq $expectedSourceFingerprint) 'UE 5.7 source fingerprint differs.'
    foreach ($row in @($migrationEvidence.packages)) { $evidenceRows[[string]$row.package] = $row }
    Assert-True ($evidenceRows.Count -eq $expectedPackageCount) 'UE 5.7 migration evidence package set differs.'
}
elseif ($Stage -eq 'POST_RESAVE58') {
    $resaveEvidencePath = Join-Path $phaseRoot 'DirtyPawnAssets58Resave.json'
    Assert-True (Test-Path -LiteralPath $resaveEvidencePath -PathType Leaf) 'UE 5.8 resave evidence is absent.'
    $resaveEvidence = Get-Content -Raw -LiteralPath $resaveEvidencePath | ConvertFrom-Json
    Assert-True ([string]$resaveEvidence.status -eq 'UE58_DIRTYPAWN_ASSETS_LOAD_COMPILE_RESAVE_RELOAD_PASS') 'UE 5.8 resave evidence is not PASS.'
    Assert-True ([int]$resaveEvidence.package_count -eq $expectedPackageCount) 'UE 5.8 resave evidence count differs.'
    Assert-True ([string]$resaveEvidence.source_fingerprint -eq $expectedSourceFingerprint) 'UE 5.8 source fingerprint differs.'
    foreach ($row in @($resaveEvidence.packages)) { $evidenceRows[[string]$row.package] = $row }
    Assert-True ($evidenceRows.Count -eq $expectedPackageCount) 'UE 5.8 resave evidence package set differs.'
}

$targetRows = @()
foreach ($asset in $assets) {
    $targetFile = Join-Path $targetContent $asset.RelativeFile
    $exists = Test-Path -LiteralPath $targetFile -PathType Leaf
    if ($Stage -eq 'PRE_STAGE') {
        Assert-True (-not $exists) "Target collision exists before staging: $($asset.Package)"
    }
    else {
        Assert-True $exists "Target asset is absent after migration/resave: $($asset.Package)"
        Assert-True ($evidenceRows.ContainsKey($asset.Package)) "Evidence row is absent: $($asset.Package)"
        $row = $evidenceRows[$asset.Package]
        $actualLength = [int64](Get-Item -LiteralPath $targetFile).Length
        $actualSha256 = Get-Sha256 -Path $targetFile
        Assert-True ($actualLength -eq [int64]$row.length) "Target length differs from evidence: $($asset.Package)"
        Assert-True ($actualSha256 -eq [string]$row.sha256) "Target hash differs from evidence: $($asset.Package)"
    }
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($targetFile, $extension)))) "Unexpected target sidecar exists for $($asset.Package): $extension"
    }
    $targetRows += [ordered]@{
        package = $asset.Package
        file = $targetFile
        exists = $exists
        length = if ($exists) { [int64](Get-Item -LiteralPath $targetFile).Length } else { $null }
        sha256 = if ($exists) { Get-Sha256 -Path $targetFile } else { $null }
    }
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'DIRTYPAWN_ASSETS_SOURCE_PROTECTED_SAFETY_PASS'
    stage = $Stage
    source_root = $source
    target_root = $target
    package_count = $expectedPackageCount
    source_bytes = $expectedSourceBytes
    source_fingerprint = $expectedSourceFingerprint
    source_assets = $sourceRows
    target_assets = $targetRows
    source_read_only = [ordered]@{
        result = 'PASS'
        evidence = $sourceGateOutput
        evidence_sha256 = Get-Sha256 -Path $sourceGateOutput
    }
    protected_invariants = [ordered]@{
        result = 'PASS'
        evidence = $protectedGateOutput
        evidence_sha256 = Get-Sha256 -Path $protectedGateOutput
    }
    batch_evidence = if ($Stage -eq 'POST_MIGRATION57') { $migrationEvidencePath } elseif ($Stage -eq 'POST_RESAVE58') { $resaveEvidencePath } else { $null }
    source_tree_mounted = $false
    raw_target_asset_copy_requested = $false
    target_sidecars_absent = $true
}
$payload | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $aggregateOutput -Encoding UTF8

Write-Host "DIRTYPAWN_ASSETS_SAFETY_GATE_PASS: $Stage"
Write-Host "Evidence: $aggregateOutput"
