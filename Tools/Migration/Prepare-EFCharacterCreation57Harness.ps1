[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "EFCC57_HARNESS_GATE_FAIL: $Message" }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourcePlugin = (Resolve-Path -LiteralPath (Join-Path $source 'Plugins\EFCharacterCreation')).Path.TrimEnd('\')
$migrationRoot = Join-Path $target 'Saved\Migration\Phase3'
$harnessRoot = Join-Path $migrationRoot 'EFCharacterCreation57Harness'
$harnessPlugin = Join-Path $harnessRoot 'Plugins\EFCharacterCreation'
$harnessProject = Join-Path $harnessRoot 'EFCharacterCreation57Harness.uproject'
$receiptPath = Join-Path $migrationRoot 'EFCharacterCreation57HarnessReceipt.json'

Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $migrationRoot) 'Harness root escapes target Saved/Migration/Phase3.'
Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target are identical.'

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $migrationRoot) 'Refusing to clean a harness outside the migration staging root.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'EFCharacterCreation57Harness') 'Unexpected harness directory name.'
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

$relativeFiles = @(
    'EFCharacterCreation.uplugin',
    'Binaries\Win64\UnrealEditor-EFCharacterCreationRuntime.dll',
    'Binaries\Win64\UnrealEditor-EFCharacterCreationEditor.dll',
    'Binaries\Win64\UnrealEditor.modules',
    'Content\UI\WBP_EFCharacterCreationRoot.uasset',
    'Content\UI\WBP_EFMorphSlider.uasset'
)

$manifest = @()
foreach ($relative in $relativeFiles) {
    $sourcePath = Join-Path $sourcePlugin $relative
    Assert-True (Test-Path -LiteralPath $sourcePath -PathType Leaf) "Missing explicit harness input: $sourcePath"
    $destinationPath = Join-Path $harnessPlugin $relative
    Assert-True (Test-IsUnderRoot -Path $destinationPath -Root $harnessPlugin) "Harness input escapes plugin root: $relative"
    New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
    $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourcePath).Hash
    $destinationHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $destinationPath).Hash
    Assert-True ($sourceHash -eq $destinationHash) "Staging hash mismatch: $relative"
    $manifest += [pscustomobject][ordered]@{
        relative_path = $relative.Replace('\', '/')
        length = [int64](Get-Item -LiteralPath $sourcePath).Length
        sha256 = $sourceHash
    }
}

$projectDescriptor = [ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated, target-hosted UE 5.7 harness for AssetTools migration of two EFCharacterCreation widgets.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'EFCharacterCreation'; Enabled = $true }
    )
}
New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
$projectDescriptor | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'HARNESS_STAGING_PASS'
    source_plugin = $sourcePlugin
    harness_root = $harnessRoot
    harness_project = $harnessProject
    policy = 'Six explicitly allowlisted staging files; staging is under target Saved and source remains read-only.'
    files = $manifest
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "EFCC57_HARNESS_STAGING_PASS: $harnessProject"
Write-Host "Receipt: $receiptPath"
