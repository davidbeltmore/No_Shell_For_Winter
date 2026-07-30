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
        throw "DUNGEON_GENERATION_SAFETY_GATE_FAIL: $Message"
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

$expectedMapLength = [int64]58016
$expectedMapSha256 = 'B6758C3A98EC06B3E31DCFA6A1A5179403F63C0A53B227E41BF1902E1569FF8F'
$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceMap = Join-Path $source 'Content\Procedural\Maps\DungeonGeneration.umap'
$targetMap = Join-Path $target 'Content\Procedural\Maps\DungeonGeneration.umap'
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4\DungeonGeneration'
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
Assert-True (Test-Path -LiteralPath $sourceMap -PathType Leaf) 'Frozen source map is absent.'
Assert-True ((Get-Item -LiteralPath $sourceMap).Length -eq $expectedMapLength) 'Frozen source map length changed.'
Assert-True ((Get-Sha256 -Path $sourceMap) -eq $expectedMapSha256) 'Frozen source map hash changed.'

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

$forbiddenTargetPaths = @(
    [System.IO.Path]::ChangeExtension($targetMap, '.uexp'),
    [System.IO.Path]::ChangeExtension($targetMap, '.ubulk'),
    [System.IO.Path]::ChangeExtension($targetMap, '.uptnl'),
    (Join-Path $target 'Content\Procedural\Maps\DungeonGeneration_BuiltData.uasset'),
    (Join-Path $target 'Content\__ExternalActors__\Procedural\Maps\DungeonGeneration'),
    (Join-Path $target 'Content\__ExternalObjects__\Procedural\Maps\DungeonGeneration')
)
foreach ($path in $forbiddenTargetPaths) {
    Assert-True (-not (Test-Path -LiteralPath $path)) "Unexpected target sidecar or external-package path exists: $path"
}

$targetState = [ordered]@{
    exists = Test-Path -LiteralPath $targetMap -PathType Leaf
    file = $targetMap
    length = $null
    sha256 = $null
    evidence = $null
}

if ($Stage -eq 'PRE_STAGE') {
    Assert-True (-not $targetState.exists) 'Target map collision exists before staging.'
}
elseif ($Stage -eq 'POST_MIGRATION57') {
    $migrationEvidencePath = Join-Path $phaseRoot 'DungeonGeneration57Migration.json'
    Assert-True (Test-Path -LiteralPath $migrationEvidencePath -PathType Leaf) 'UE 5.7 migration evidence is absent.'
    $migration = Get-Content -Raw -LiteralPath $migrationEvidencePath | ConvertFrom-Json
    Assert-True ([string]$migration.status -eq 'ASSETTOOLS_EXACT_DUNGEON_GENERATION_MIGRATION_PASS') 'UE 5.7 migration evidence is not PASS.'
    Assert-True ([string]$migration.package.package -eq '/Game/Procedural/Maps/DungeonGeneration') 'UE 5.7 migration evidence names a different package.'
    Assert-True ($targetState.exists) 'Target map is absent after UE 5.7 migration.'
    $targetState.length = [int64](Get-Item -LiteralPath $targetMap).Length
    $targetState.sha256 = Get-Sha256 -Path $targetMap
    $targetState.evidence = $migrationEvidencePath
    Assert-True ($targetState.length -eq [int64]$migration.package.length) 'Target map length differs from UE 5.7 migration evidence.'
    Assert-True ($targetState.sha256 -eq [string]$migration.package.sha256) 'Target map hash differs from UE 5.7 migration evidence.'
}
elseif ($Stage -eq 'POST_RESAVE58') {
    $resaveEvidencePath = Join-Path $phaseRoot 'DungeonGeneration58Resave.json'
    Assert-True (Test-Path -LiteralPath $resaveEvidencePath -PathType Leaf) 'UE 5.8 resave evidence is absent.'
    $resave = Get-Content -Raw -LiteralPath $resaveEvidencePath | ConvertFrom-Json
    Assert-True ([string]$resave.status -eq 'UE58_DUNGEON_GENERATION_LOAD_RESAVE_RELOAD_PASS') 'UE 5.8 resave evidence is not PASS.'
    Assert-True ([string]$resave.package -eq '/Game/Procedural/Maps/DungeonGeneration') 'UE 5.8 resave evidence names a different package.'
    Assert-True ($targetState.exists) 'Target map is absent after UE 5.8 resave.'
    $targetState.length = [int64](Get-Item -LiteralPath $targetMap).Length
    $targetState.sha256 = Get-Sha256 -Path $targetMap
    $targetState.evidence = $resaveEvidencePath
    Assert-True ($targetState.length -eq [int64]$resave.length_after_resave) 'Target map length differs from UE 5.8 resave evidence.'
    Assert-True ($targetState.sha256 -eq [string]$resave.sha256_after_resave) 'Target map hash differs from UE 5.8 resave evidence.'
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'DUNGEON_GENERATION_SOURCE_PROTECTED_SAFETY_PASS'
    stage = $Stage
    source_root = $source
    target_root = $target
    source_map = [ordered]@{
        package = '/Game/Procedural/Maps/DungeonGeneration'
        file = $sourceMap
        length = $expectedMapLength
        sha256 = $expectedMapSha256
    }
    target_map = $targetState
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
    forbidden_target_paths_absent = @($forbiddenTargetPaths)
    source_tree_mounted = $false
    raw_target_asset_copy_requested = $false
}
$payload | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $aggregateOutput -Encoding UTF8

Write-Host "DUNGEON_GENERATION_SAFETY_GATE_PASS: $Stage"
Write-Host "Evidence: $aggregateOutput"
