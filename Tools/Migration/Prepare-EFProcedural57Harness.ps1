[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "EFPROCEDURAL57_HARNESS_GATE_FAIL: $Message" }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$migrationRoot = Join-Path $target 'Saved\Migration\Phase3'
$harnessRoot = Join-Path $migrationRoot 'EFProcedural57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'EFProcedural57Harness.uproject'
$receiptPath = Join-Path $migrationRoot 'EFProcedural57HarnessReceipt.json'
$stagedAssets = @(
    [pscustomobject]@{
        RelativePath = 'Procedural\Maps\DungeonGeneration.umap'
        Length = 58016
        Sha256 = 'B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F'
    },
    [pscustomobject]@{
        RelativePath = 'Calysto\Dungeon\Blueprint\BP_MassiveDungeon.uasset'
        Length = 411781
        Sha256 = '334CB0FDABE322EC42B79DB3280567A942FB3D113CF327A271E19300E1BFCDDF'
    },
    [pscustomobject]@{
        RelativePath = 'Calysto\Dungeon\Blueprint\Utility\BP_StartPoint.uasset'
        Length = 29816
        Sha256 = '611B8EF004978597D20A110DF748FFFFBC2CDD3F934BEF4AF52F24B14824EE2B'
    }
)

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target are identical.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $migrationRoot) 'Harness root escapes target Saved/Migration/Phase3.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source project descriptor is absent.'

if (Test-Path -LiteralPath $harnessContent) {
    $contentItem = Get-Item -LiteralPath $harnessContent -Force
    if ($contentItem.LinkType -eq 'Junction') {
        $expectedLegacyTarget = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
        $linkTarget = [System.IO.Path]::GetFullPath([string]$contentItem.Target).TrimEnd('\')
        Assert-True ($linkTarget.Equals($expectedLegacyTarget, [System.StringComparison]::OrdinalIgnoreCase)) 'Legacy harness junction target is not the audited source Content root.'
        [System.IO.Directory]::Delete($harnessContent, $false)
    }
}
if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $migrationRoot) 'Refusing to clean a harness outside the migration staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'EFProcedural57Harness') 'Unexpected harness directory name.'
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
$receiptAssets = @()
foreach ($asset in $stagedAssets) {
    $sourceFile = Join-Path (Join-Path $source 'Content') $asset.RelativePath
    $stagedFile = Join-Path $harnessContent $asset.RelativePath
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Missing allowlisted source seed: $sourceFile"
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq $asset.Length) "Source seed length differs from baseline: $sourceFile"
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFile).Hash -eq $asset.Sha256) "Source seed hash differs from baseline: $sourceFile"
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $stagedFile).Hash -eq $asset.Sha256) "Staged seed hash mismatch: $stagedFile"
    $receiptAssets += [ordered]@{
        source = $sourceFile
        staged = $stagedFile
        length = $asset.Length
        sha256 = $asset.Sha256
    }
}

$projectDescriptor = [ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Target-hosted UE 5.7 procedural dependency inspection harness with three exact-hash staged seeds.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'PCG'; Enabled = $true },
        [ordered]@{ Name = 'PCGExtendedToolkit'; Enabled = $true }
    )
}
$projectDescriptor | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_SEED_STAGING_HARNESS_PASS'
    harness_root = $harnessRoot
    harness_project = $harnessProject
    harness_content = $harnessContent
    staged_assets = $receiptAssets
    policy = 'Three exact-hash source seeds are copied under target Saved; no source junction, startup script, dependency, or package save is permitted.'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "EFPROCEDURAL57_HARNESS_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
