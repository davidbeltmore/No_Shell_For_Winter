[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet(
        'CodeWidgetDesignerBridge',
        'DirtyPawnRuntime',
        'EFBlink',
        'EFCharacterCreation',
        'EFCharacterCreationDazBridge',
        'EFClothingMorph',
        'EFCharacterCreationACFUBridge',
        'ACFTrainingSystem',
        'EFProcedural',
        'EFLevelFlow',
        'EFProjectSystems'
    )]
    [string]$PluginName,
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$allowedExtensions = @('.cs', '.cpp', '.h')
$excludedTrees = @('Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', 'Content', 'Config')

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "PLUGIN_IMPORT_GATE_FAIL: $Message"
    }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'

$sourcePlugin = (Resolve-Path -LiteralPath (Join-Path $source "Plugins\$PluginName")).Path.TrimEnd('\')
$sourceDescriptor = Join-Path $sourcePlugin "$PluginName.uplugin"
$sourceCode = Join-Path $sourcePlugin 'Source'
$targetPluginsRoot = Join-Path $target 'Plugins'
$targetPlugin = Join-Path $targetPluginsRoot $PluginName
$receiptPath = Join-Path $target "Docs\Migration\Evidence\Phase3_${PluginName}_ImportReceipt.json"

Assert-True (Test-Path -LiteralPath $sourceDescriptor -PathType Leaf) "Missing source descriptor: $sourceDescriptor"
Assert-True (Test-Path -LiteralPath $sourceCode -PathType Container) "Missing source code root: $sourceCode"
Assert-True (Test-IsUnderRoot -Path $sourcePlugin -Root $source) 'Source plugin escapes the source project.'
Assert-True (Test-IsUnderRoot -Path $targetPlugin -Root $targetPluginsRoot) 'Target plugin escapes the target Plugins root.'
Assert-True (-not (Test-Path -LiteralPath $targetPlugin)) "Target plugin already exists: $targetPlugin"
Assert-True (-not (Test-Path -LiteralPath $receiptPath)) "Import receipt already exists: $receiptPath"

$descriptor = Get-Content -Raw -LiteralPath $sourceDescriptor | ConvertFrom-Json
$declaredModules = @($descriptor.Modules | ForEach-Object { [string]$_.Name })
Assert-True ($declaredModules.Count -gt 0) 'Descriptor declares no modules.'

$sourceFiles = @(
    Get-Item -LiteralPath $sourceDescriptor
    Get-ChildItem -LiteralPath $sourceCode -Recurse -File -Force |
        Where-Object { $allowedExtensions -contains $_.Extension.ToLowerInvariant() }
)
$allSourceFiles = @(Get-ChildItem -LiteralPath $sourceCode -Recurse -File -Force)
Assert-True ($sourceFiles.Count -eq ($allSourceFiles.Count + 1)) 'A Source file has an extension outside the explicit allowlist.'

foreach ($file in $sourceFiles) {
    Assert-True (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) "Reparse point rejected: $($file.FullName)"
    $relative = $file.FullName.Substring($sourcePlugin.Length).TrimStart('\')
    Assert-True (-not ($excludedTrees | Where-Object { $relative.StartsWith($_ + '\', [System.StringComparison]::OrdinalIgnoreCase) })) "Excluded tree reached: $relative"
}

$manifest = @(
    foreach ($file in $sourceFiles | Sort-Object FullName) {
        $relative = $file.FullName.Substring($sourcePlugin.Length).TrimStart('\')
        [pscustomobject][ordered]@{
            relative_path = $relative.Replace('\', '/')
            length = [int64]$file.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
        }
    }
)

if ($DryRun) {
    [ordered]@{
        status = 'DRY_RUN_PASS'
        plugin = $PluginName
        modules = $declaredModules
        file_count = $manifest.Count
        total_bytes = [int64](($manifest | Measure-Object -Property length -Sum).Sum)
        source_plugin = $sourcePlugin
        target_plugin = $targetPlugin
        excluded_trees = $excludedTrees
    } | ConvertTo-Json -Depth 5
    exit 0
}

try {
    New-Item -ItemType Directory -Path $targetPlugin -Force | Out-Null
    foreach ($entry in $manifest) {
        $relativeWindows = $entry.relative_path.Replace('/', '\')
        $sourcePath = Join-Path $sourcePlugin $relativeWindows
        $targetPath = Join-Path $targetPlugin $relativeWindows
        Assert-True (Test-IsUnderRoot -Path $targetPath -Root $targetPlugin) "Destination escapes plugin root: $targetPath"
        New-Item -ItemType Directory -Path (Split-Path -Parent $targetPath) -Force | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $targetPath
        $targetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $targetPath).Hash
        Assert-True ($targetHash -eq $entry.sha256) "Post-copy hash mismatch: $relativeWindows"
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $receiptPath) -Force | Out-Null
    [ordered]@{
        schema_version = 1
        generated_utc = [DateTime]::UtcNow.ToString('o')
        status = 'SOURCE_ONLY_IMPORT_PASS'
        plugin = $PluginName
        modules = $declaredModules
        source_plugin = $sourcePlugin
        target_plugin = $targetPlugin
        policy = 'Descriptor and Source files only; per-file allowlisted import.'
        excluded_trees = $excludedTrees
        file_count = $manifest.Count
        total_bytes = [int64](($manifest | Measure-Object -Property length -Sum).Sum)
        files = $manifest
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
}
catch {
    if ((Test-Path -LiteralPath $targetPlugin) -and (Test-IsUnderRoot -Path $targetPlugin -Root $targetPluginsRoot)) {
        Remove-Item -LiteralPath $targetPlugin -Recurse -Force
    }
    throw
}

Write-Host "PLUGIN_SOURCE_IMPORT_PASS: $PluginName ($($manifest.Count) files)"
Write-Host "Receipt: $receiptPath"
