[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "ACFTRAIN57_HARNESS_GATE_FAIL: $Message" }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceAsset = Join-Path $source 'Content\ExportedAnimations\Anim_KA_Idle53_Seiza_Loop1.uasset'
$sourceFemale = Join-Path $source 'Content\DazToUnreal\Female\Female.uasset'
$sourceSkeleton = Join-Path $source 'Content\FullSample\GASP\UEFN_Mannequin\Meshes\SK_UEFN_Mannequin.uasset'
$migrationRoot = Join-Path $target 'Saved\Migration\Phase3'
$harnessRoot = Join-Path $migrationRoot 'ACFTrainingAnimation57Harness'
$harnessAsset = Join-Path $harnessRoot 'Content\ExportedAnimations\Anim_KA_Idle53_Seiza_Loop1.uasset'
$harnessFemale = Join-Path $harnessRoot 'Content\DazToUnreal\Female\Female.uasset'
$harnessSkeleton = Join-Path $harnessRoot 'Content\FullSample\GASP\UEFN_Mannequin\Meshes\SK_UEFN_Mannequin.uasset'
$harnessProject = Join-Path $harnessRoot 'ACFTrainingAnimation57Harness.uproject'
$receiptPath = Join-Path $migrationRoot 'ACFTrainingAnimation57HarnessReceipt.json'
$expectedLength = 621597
$expectedSha256 = 'B5397DA1F395DE4C3D2BEC31B6D8498C36FC17EBE6A77BADEAEE378D36DA1DCC'
$expectedFemaleLength = 65856669
$expectedFemaleSha256 = 'C6C64A94F4C8FCE1F24CF2965933E23BAC30A2C05045135B278492413A4E3FC6'
$expectedSkeletonLength = 179592
$expectedSkeletonSha256 = 'E106C020A151F46345133D3C95A4E3817060F923AEC32ECB98C7343ADE53D036'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target are identical.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $migrationRoot) 'Harness root escapes target Saved/Migration/Phase3.'
Assert-True (Test-Path -LiteralPath $sourceAsset -PathType Leaf) "Missing allowlisted source asset: $sourceAsset"
Assert-True ((Get-Item -LiteralPath $sourceAsset).Length -eq $expectedLength) 'Source asset length differs from the audited baseline.'
Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $sourceAsset).Hash -eq $expectedSha256) 'Source asset hash differs from the audited baseline.'
Assert-True (Test-Path -LiteralPath $sourceFemale -PathType Leaf) "Missing staging-only Female dependency: $sourceFemale"
Assert-True ((Get-Item -LiteralPath $sourceFemale).Length -eq $expectedFemaleLength) 'Source Female length differs from the audited baseline.'
Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFemale).Hash -eq $expectedFemaleSha256) 'Source Female hash differs from the audited baseline.'
Assert-True (Test-Path -LiteralPath $sourceSkeleton -PathType Leaf) "Missing staging-only UEFN skeleton dependency: $sourceSkeleton"
Assert-True ((Get-Item -LiteralPath $sourceSkeleton).Length -eq $expectedSkeletonLength) 'Source UEFN skeleton length differs from the audited baseline.'
Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $sourceSkeleton).Hash -eq $expectedSkeletonSha256) 'Source UEFN skeleton hash differs from the audited baseline.'

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $migrationRoot) 'Refusing to clean a harness outside the migration staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'ACFTrainingAnimation57Harness') 'Unexpected harness directory name.'
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

$stagingCopies = @(
    [pscustomobject]@{ Source = $sourceAsset; Destination = $harnessAsset; Sha256 = $expectedSha256; Role = 'migration_root' },
    [pscustomobject]@{ Source = $sourceFemale; Destination = $harnessFemale; Sha256 = $expectedFemaleSha256; Role = 'load_dependency_only' },
    [pscustomobject]@{ Source = $sourceSkeleton; Destination = $harnessSkeleton; Sha256 = $expectedSkeletonSha256; Role = 'load_dependency_only' }
)
foreach ($copy in $stagingCopies) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $copy.Destination) -Force | Out-Null
    Copy-Item -LiteralPath $copy.Source -Destination $copy.Destination
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $copy.Destination).Hash -eq $copy.Sha256) "Staging hash mismatch: $($copy.Destination)"
}

$projectDescriptor = [ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated target-hosted UE 5.7 harness for one ACFTrainingSystem animation.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true }
    )
}
$projectDescriptor | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'HARNESS_STAGING_PASS'
    source_asset = $sourceAsset
    harness_root = $harnessRoot
    harness_project = $harnessProject
    policy = 'One migration root plus two load-only dependencies staged under target Saved; source remains read-only and AssetTools ignores dependencies.'
    staged_assets = @(
        [ordered]@{ source = $sourceAsset; destination = $harnessAsset; role = 'migration_root'; length = $expectedLength; sha256 = $expectedSha256 },
        [ordered]@{ source = $sourceFemale; destination = $harnessFemale; role = 'load_dependency_only'; length = $expectedFemaleLength; sha256 = $expectedFemaleSha256 },
        [ordered]@{ source = $sourceSkeleton; destination = $harnessSkeleton; role = 'load_dependency_only'; length = $expectedSkeletonLength; sha256 = $expectedSkeletonSha256 }
    )
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "ACFTRAIN57_HARNESS_STAGING_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
