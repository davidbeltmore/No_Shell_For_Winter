param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$Stamp = "20260924_safe_generated_cleanup"
)

$ErrorActionPreference = "Stop"
$projectRootResolved = (Resolve-Path -LiteralPath $ProjectRoot).Path
$rootPrefix = $projectRootResolved.TrimEnd("\") + "\"
$quarantineRoot = Join-Path $projectRootResolved "Saved\CleanupQuarantine\$Stamp"

$relativeTargets = @(
    "Binaries",
    "DerivedDataCache",
    "Intermediate\Build",
    "Intermediate\CachedAssetRegistry",
    "Intermediate\ShaderAutogen",
    "Intermediate\ReimportCache",
    "Intermediate\Sandboxes",
    "Intermediate\Staging",
    "Saved\StagedBuilds\Windows",
    "Saved\Cooked",
    "Saved\Crashes",
    "Saved\Logs",
    "Saved\Shaders",
    "Saved\ShaderDebugInfo",
    "Saved\Artifacts\PendingValidation\Development_20260904_060600_V5",
    "Saved\Artifacts\PendingValidation\CalystoV5_Development_20260831_134300",
    "Saved\Artifacts\PendingValidation\CalystoV5_Development_20260831_140500",
    "Saved\Artifacts\PendingValidation\Development_20260831_113908_605",
    "Saved\Artifacts\PendingValidation\Development_20260831_111042_029"
)

$projectOwnedPlugins = @(
    "ACFTrainingSystem",
    "CodeWidgetDesignerBridge",
    "DirtyPawnRuntime",
    "EFBlink",
    "EFCharacterCreation",
    "EFCharacterCreationACFUBridge",
    "EFCharacterCreationDazBridge",
    "EFClothingMorph",
    "EFEquipment",
    "EFLevelFlow",
    "EFProcedural",
    "EFProjectSystems"
)

foreach ($pluginName in $projectOwnedPlugins) {
    $relativeTargets += "Plugins\$pluginName\Binaries"
    $relativeTargets += "Plugins\$pluginName\Intermediate\Build"
}

$approved = [System.Collections.Generic.List[object]]::new()
foreach ($relativePath in $relativeTargets) {
    $fullPath = [System.IO.Path]::GetFullPath((Join-Path $projectRootResolved $relativePath))
    if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target escapes the project root: $fullPath"
    }
    if ($fullPath -eq $projectRootResolved) {
        throw "Refusing to target the project root."
    }
    if (Test-Path -LiteralPath $fullPath) {
        $bytes = (Get-ChildItem -LiteralPath $fullPath -Recurse -Force -File -ErrorAction SilentlyContinue |
            Measure-Object Length -Sum).Sum
        $approved.Add([pscustomobject]@{
            RelativePath = $relativePath
            FullPath = $fullPath
            Bytes = [int64]$bytes
        })
    }
}

New-Item -ItemType Directory -Path $quarantineRoot -Force | Out-Null
foreach ($item in $approved) {
    $destinationName = $item.RelativePath -replace '[\\/:*?"<>|]', "__"
    Move-Item -LiteralPath $item.FullPath -Destination (Join-Path $quarantineRoot $destinationName)
}

$quarantineResolved = (Resolve-Path -LiteralPath $quarantineRoot).Path
if (-not $quarantineResolved.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Cleanup quarantine escaped the project root."
}

$removedBytes = ($approved | Measure-Object Bytes -Sum).Sum
if ($null -eq $removedBytes) {
    $removedBytes = 0
}
$removedTargets = $approved.Count
$extendedQuarantine = if ($quarantineResolved.StartsWith("\\")) {
    "\\?\UNC\" + $quarantineResolved.TrimStart("\")
} else {
    "\\?\" + $quarantineResolved
}
[System.IO.Directory]::Delete($extendedQuarantine, $true)

[pscustomobject]@{
    RemovedTargets = $removedTargets
    RemovedBytes = $removedBytes
    RemovedGiB = [math]::Round($removedBytes / 1GB, 3)
    QuarantineDeleted = -not (Test-Path -LiteralPath $quarantineResolved)
} | ConvertTo-Json
