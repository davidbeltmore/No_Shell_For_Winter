[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "DEFEATVISUAL57_HARNESS_GATE_FAIL: $Message" }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$manifestPath = (Resolve-Path -LiteralPath (Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv')).Path
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4'
$harnessRoot = Join-Path $phaseRoot 'DefeatVisual57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'DefeatVisual57Harness.uproject'
$harnessConfig = Join-Path $harnessRoot 'Config\DefaultEngine.ini'
$receiptPath = Join-Path $phaseRoot 'DefeatVisual57HarnessReceipt.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target are identical.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $phaseRoot) 'Harness root escapes target Saved/Migration/Phase4.'

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $phaseRoot) 'Refusing to clean a harness outside the migration staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'DefeatVisual57Harness') 'Unexpected harness directory name.'
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

$packages = @(
    '/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensed',
    '/Game/UI/Defeat/Struggle/Fonts/FF_BarlowSemiCondensedMedium',
    '/Game/UI/Defeat/Struggle/Fonts/FF_Cinzel',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_Arrow',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_BackdropVignette',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_GlowStreak',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_MainPanel',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_Noise',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetChamber',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetPulse',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_TargetRing',
    '/Game/UI/Defeat/Struggle/Textures/T_Struggle_TopPanel'
)
$manifest = @{}
foreach ($row in @(Import-Csv -LiteralPath $manifestPath)) { $manifest[[string]$row.PackageName] = $row }

$staged = @()
foreach ($package in $packages) {
    Assert-True ($manifest.ContainsKey($package)) "Package is absent from manifest: $package"
    $row = $manifest[$package]
    Assert-True ($row.Presence -eq 'SOURCE_ONLY') "Package is not source-only: $package"
    $sourceFile = Join-Path $source ([string]$row.SourceFile)
    Assert-True (Test-IsUnderRoot -Path $sourceFile -Root $sourceContent) "Source package escapes Content: $sourceFile"
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source package is absent: $sourceFile"
    $sourceItem = Get-Item -LiteralPath $sourceFile
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFile).Hash
    Assert-True ($sourceItem.Length -eq [int64]$row.SourceLength) "Source length differs from manifest: $package"
    Assert-True ($sourceHash -eq [string]$row.SourceSHA256) "Source hash differs from manifest: $package"

    $relative = $sourceFile.Substring($sourceContent.Length).TrimStart('\')
    $destination = Join-Path $harnessContent $relative
    Assert-True (Test-IsUnderRoot -Path $destination -Root $harnessContent) "Staged package escapes harness: $destination"
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $destination
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash -eq $sourceHash) "Staging hash mismatch: $package"
    $staged += [ordered]@{ package = $package; source = $sourceFile; staged = $destination; length = [int64]$sourceItem.Length; sha256 = $sourceHash }
}
Assert-True ($staged.Count -eq 12) "Expected 12 packages but staged $($staged.Count)."

New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated target-hosted UE 5.7 harness for the exact Defeat visual resource batch.'
    Plugins = @([ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true })
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

New-Item -ItemType Directory -Path (Split-Path -Parent $harnessConfig) -Force | Out-Null
@'
[/Script/Engine.CollisionProfile]
+Profiles=(Name="WaterBodyCollision",CollisionEnabled=QueryOnly,bCanModify=False,ObjectTypeName="Water",CustomResponses=((Channel="WorldDynamic",Response=ECR_Overlap),(Channel="Pawn",Response=ECR_Overlap),(Channel="Visibility",Response=ECR_Ignore),(Channel="Camera",Response=ECR_Ignore),(Channel="PhysicsBody",Response=ECR_Overlap),(Channel="Vehicle",Response=ECR_Overlap),(Channel="Destructible",Response=ECR_Overlap)),HelpMessage="Default Water Collision Profile (Created by Water Plugin)")
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel12,DefaultResponse=ECR_Overlap,bTraceType=False,bStaticObject=False,Name="Water")
'@ | Set-Content -LiteralPath $harnessConfig -Encoding UTF8

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_DEFEAT_VISUAL_HARNESS_PASS'
    source_root = $source
    harness_root = $harnessRoot
    harness_project = $harnessProject
    package_count = $staged.Count
    packages = $staged
    policy = 'Exact 3 FontFace plus 9 Texture2D package allowlist staged below target Saved; source is never mounted or written.'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "DEFEATVISUAL57_HARNESS_PASS: $harnessProject"
Write-Host "Packages: $($staged.Count)"
Write-Host "Receipt: $receiptPath"
