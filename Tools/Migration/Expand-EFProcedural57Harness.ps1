[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "EFPROCEDURAL57_EXPANSION_GATE_FAIL: $Message" }
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
$harnessRoot = Join-Path $target 'Saved\Migration\Phase3\EFProcedural57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$manifestPath = Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv'
$probePath = Join-Path $target 'Saved\Migration\Phase3\EFProceduralDependencies57.json'
$closureProbePath = Join-Path $target 'Saved\Migration\Phase3\EFProceduralClosure57.json'
$receiptPath = Join-Path $target 'Saved\Migration\Phase3\EFProcedural57ClosureStagingReceipt.json'

Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root (Join-Path $target 'Saved\Migration\Phase3')) 'Harness root escapes target Saved/Migration/Phase3.'
Assert-True (Test-Path -LiteralPath $harnessContent -PathType Container) 'Prepared harness Content is absent.'
Assert-True (Test-Path -LiteralPath $manifestPath -PathType Leaf) 'Migration manifest is absent.'
Assert-True (Test-Path -LiteralPath $probePath -PathType Leaf) 'Initial procedural dependency probe is absent.'

$rows = @(Import-Csv -LiteralPath $manifestPath)
$lookup = @{}
foreach ($row in $rows) { $lookup[$row.PackageName] = $row }
$probe = Get-Content -LiteralPath $probePath -Raw | ConvertFrom-Json
Assert-True ($probe.status -eq 'STAGED_SEED_REGISTRY_DEPENDENCY_PROBE_PASS') 'Initial dependency probe did not pass.'

$rootDependencies = @{}
$queue = [System.Collections.Generic.Queue[string]]::new()
foreach ($seed in $probe.seeds) {
    $package = [string]$seed.package
    $rootDependencies[$package] = @($seed.direct_dependencies | ForEach-Object { [string]$_ })
    $queue.Enqueue($package)
    foreach ($dependency in $rootDependencies[$package]) {
        if ($dependency.StartsWith('/Game/')) { $queue.Enqueue($dependency) }
    }
}
if (Test-Path -LiteralPath $closureProbePath -PathType Leaf) {
    $closureProbe = Get-Content -LiteralPath $closureProbePath -Raw | ConvertFrom-Json
    Assert-True ($closureProbe.status -eq 'STAGED_CLOSURE_REGISTRY_PROBE_PASS') 'Existing closure probe did not pass.'
    foreach ($property in $closureProbe.graph.PSObject.Properties) {
        $inspectedPackage = [string]$property.Name
        if ($inspectedPackage.StartsWith('/Game/')) {
            $queue.Enqueue($inspectedPackage)
        }
    }
    foreach ($external in $closureProbe.external_source_only) {
        $queue.Enqueue([string]$external.package)
    }
}

$visited = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
$closure = [System.Collections.Generic.List[object]]::new()
while ($queue.Count -gt 0) {
    $package = $queue.Dequeue()
    if (-not $visited.Add($package)) { continue }
    if (-not $lookup.ContainsKey($package)) { continue }
    $row = $lookup[$package]
    if ($row.Presence -ne 'SOURCE_ONLY') { continue }
    $closure.Add($row)

    $dependencies = if ($rootDependencies.ContainsKey($package)) {
        @($rootDependencies[$package])
    } elseif ($row.SourceRegistryPresent -eq 'True') {
        @($row.SourceDependencies -split ';')
    } else {
        @()
    }
    foreach ($dependency in $dependencies) {
        if ($dependency.StartsWith('/Game/')) { $queue.Enqueue($dependency) }
    }
}

Assert-True ($closure.Count -gt 3) 'Computed closure did not expand beyond the three seeds.'
$receiptAssets = @()
$stagedTotalBytes = [int64]0
$inspectionOnlyCount = 0
foreach ($row in @($closure | Sort-Object PackageName)) {
    Assert-True (-not [string]::IsNullOrWhiteSpace($row.SourceFile)) "Missing source file mapping: $($row.PackageName)"
    Assert-True (-not [string]::IsNullOrWhiteSpace($row.SourceSHA256)) "Missing source hash: $($row.PackageName)"
    $sourceFile = Join-Path $source $row.SourceFile
    $relativeContentPath = $sourceFile.Substring($sourceContent.Length).TrimStart('\')
    $stagedFile = Join-Path $harnessContent $relativeContentPath
    Assert-True (Test-IsUnderRoot -Path $sourceFile -Root $sourceContent) "Source package escapes Content: $sourceFile"
    Assert-True (Test-IsUnderRoot -Path $stagedFile -Root $harnessContent) "Staged package escapes harness Content: $stagedFile"
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Source package is absent: $sourceFile"
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq [int64]$row.SourceLength) "Source length differs from manifest: $sourceFile"
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFile).Hash -eq $row.SourceSHA256) "Source hash differs from manifest: $sourceFile"
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedFile) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $stagedFile -Force
    Assert-True ((Get-FileHash -Algorithm SHA256 -LiteralPath $stagedFile).Hash -eq $row.SourceSHA256) "Staged hash mismatch: $stagedFile"

    $role = if ($row.PackageName -match '^/Game/Calysto/Dungeon/Demo/LevelInstance/PCGDA_') { 'inspection_only_editor_dependency_candidate' } else { 'closure_candidate' }
    $stagedTotalBytes += [int64]$row.SourceLength
    if ($role -eq 'inspection_only_editor_dependency_candidate') { $inspectionOnlyCount++ }
    $receiptAssets += [ordered]@{
        package = $row.PackageName
        source = $sourceFile
        staged = $stagedFile
        role = $role
        length = [int64]$row.SourceLength
        sha256 = $row.SourceSHA256
        registry_present_in_phase2 = [bool]::Parse($row.SourceRegistryPresent)
    }
}

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'REGISTERED_CLOSURE_STAGING_PASS'
    algorithm = 'Three inspected seed dependencies plus the complete prior staged graph, newly discovered external dependencies, and recursive Phase 2 source registry dependencies; SOURCE_ONLY packages only and monotonic across iterations.'
    staged_package_count = $receiptAssets.Count
    staged_total_bytes = $stagedTotalBytes
    inspection_only_count = $inspectionOnlyCount
    assets = $receiptAssets
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "EFPROCEDURAL57_CLOSURE_STAGING_PASS: $($receiptAssets.Count) packages"
Write-Host "Receipt: $receiptPath"
