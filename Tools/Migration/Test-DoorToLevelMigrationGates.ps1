[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('PRE_STAGE', 'PRE_MIGRATION57', 'POST_MIGRATION57', 'POST_RESAVE58')]
    [string]$Stage,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{7,63}$')]
    [string]$RunId,
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter',
    [switch]$AllowKnownAcfuPdbDrift
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "DOOR_TO_LEVEL_SAFETY_GATE_FAIL: $Message"
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

function Test-SamePath {
    param([string]$Left, [string]$Right)
    if ([string]::IsNullOrWhiteSpace($Left) -or [string]::IsNullOrWhiteSpace($Right)) {
        return $false
    }
    return [System.IO.Path]::GetFullPath($Left).TrimEnd('\').Equals(
        [System.IO.Path]::GetFullPath($Right).TrimEnd('\'),
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

function Test-KnownAcfuPdbDrift {
    param($Evidence)
    $mismatches = @($Evidence.mismatches)
    $sets = @($Evidence.sets)
    $authoritative = @($Evidence.authoritative_assets)
    if ([int]$Evidence.mismatch_count -ne 1 -or $mismatches.Count -ne 1) { return $false }
    $mismatch = $mismatches[0]
    if ([string]$mismatch.Set -ne 'ACFU_4_3_5' -or
        [string]$mismatch.RelativePath -ne 'Binaries\Win64\UnrealEditor-AscentSaveSystem.pdb' -or
        [string]$mismatch.Kind -ne 'HASH_CHANGED' -or
        [string]$mismatch.Expected -ne 'A1AE0CBC7666FF2EEABAB90A2A294768A3066A4FF8CC372CC2260547E8CA296E' -or
        [string]$mismatch.Actual -ne '8B60C050DFA054D77CB3CFCD12F366F7F8F738FA4E79C6F1C2DFB7427B273A90') { return $false }
    $acf = @($sets | Where-Object Name -eq 'ACFU_4_3_5')
    if ($acf.Count -ne 1 -or [int]$acf[0].ExpectedFileCount -ne 5043 -or
        [int]$acf[0].CurrentFileCount -ne 5043 -or [int]$acf[0].MismatchCount -ne 1) { return $false }
    if (@($sets | Where-Object { $_.Name -ne 'ACFU_4_3_5' -and $_.Result -ne 'PASS' }).Count -ne 0) { return $false }
    if (@($authoritative | Where-Object Result -ne 'PASS').Count -ne 0) { return $false }
    return $true
}

function Read-Evidence {
    param([string]$Path, [string]$Label)
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "$Label is absent: $Path"
    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}

function Assert-GateChildEvidence {
    param([object]$Gate, [string]$Label)
    $sourceEvidencePath = [string]$Gate.source_read_only.evidence
    $protectedEvidencePath = [string]$Gate.protected_invariants.evidence
    Assert-True (Test-Path -LiteralPath $sourceEvidencePath -PathType Leaf) "$Label nested source evidence is absent."
    Assert-True (Test-Path -LiteralPath $protectedEvidencePath -PathType Leaf) "$Label nested protected evidence is absent."
    Assert-True ((Get-Sha256 -Path $sourceEvidencePath) -eq [string]$Gate.source_read_only.evidence_sha256) "$Label nested source evidence hash differs."
    Assert-True ((Get-Sha256 -Path $protectedEvidencePath) -eq [string]$Gate.protected_invariants.evidence_sha256) "$Label nested protected evidence hash differs."
}

Assert-True (-not $RunId.Contains('..')) 'RunId contains a forbidden traversal token.'

$expectedPackageCount = 4
$expectedSourceBytes = [int64]189704
$expectedSourceFingerprint = '4477D83F3722FA80674C18791BB2A85DCCE5DBE19FD57D17B52C20BE716212CC'
$legacyBlueprint = [pscustomobject]@{
    Package = '/Game/Procedural/DoorToLevel'
    RelativeFile = 'Procedural\DoorToLevel.uasset'
    Class = 'Blueprint'
    Length = [int64]53280
    Sha256 = '7EDF9F4A24D14F03AF2AE3F6A111696CF4AAC79052C225BCE90429D06935D016'
}
$assets = @(
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial'; RelativeFile = 'Calysto\Dungeon\Demo\LowPoly\Material\M_BaseMaterial.uasset'; Class = 'Material'; Length = [int64]15251; Sha256 = '1DA354F36752F99E8741529372A580CB4B174C390DB080A62037E55CC8771941' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal'; RelativeFile = 'Calysto\Dungeon\Demo\LowPoly\Material\M_Metal.uasset'; Class = 'Material'; Length = [int64]19454; Sha256 = '8722C616F22B81315E266A471785B4169F45F2E9CCEF5229F08E27A6ED824B23' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette'; RelativeFile = 'Calysto\Dungeon\Demo\LowPoly\Texture\T_Palette.uasset'; Class = 'Texture2D'; Length = [int64]100800; Sha256 = '515E0A851F19035D612620101F32133243442FFAEF0F0DC93FA049F0C931D26B' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors'; RelativeFile = 'Calysto\Dungeon\Mesh\DungeonMesh\SM_SquaredArchedWoodenDoors.uasset'; Class = 'StaticMesh'; Length = [int64]54199; Sha256 = '40AF92CF0E91C356B13AB171065C17A649CD874BBB6724A29793A7B91EB8A3A7' }
)

Assert-True ($assets.Count -eq $expectedPackageCount) 'Internal visual allowlist count differs.'
Assert-True (@($assets.Package | Sort-Object -Unique).Count -eq $expectedPackageCount) 'Internal visual allowlist contains duplicate packages.'
Assert-True ((($assets | Measure-Object -Property Length -Sum).Sum) -eq $expectedSourceBytes) 'Internal visual source-byte total differs.'
$fingerprintText = @(
    $assets |
        Sort-Object Package |
        ForEach-Object { '{0}|{1}|{2}' -f $_.Package, $_.Length, $_.Sha256 }
) -join "`n"
Assert-True ((Get-TextSha256 -Text $fingerprintText) -eq $expectedSourceFingerprint) 'Internal visual source fingerprint differs.'

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4\DoorToLevel'
$runsRoot = Join-Path $phaseRoot 'Runs'
$runRoot = Join-Path $runsRoot $RunId
$gateRoot = Join-Path $runRoot 'Gates'
$sourceGateScript = Join-Path $target 'Tools\Migration\Test-SourceReadOnly.ps1'
$protectedGateScript = Join-Path $target 'Tools\Migration\Test-ProtectedInvariants.ps1'
$sourceGateOutput = Join-Path $gateRoot ("{0}_SourceReadOnly.json" -f $Stage)
$protectedGateOutput = Join-Path $gateRoot ("{0}_ProtectedInvariants.json" -f $Stage)
$aggregateOutput = Join-Path $gateRoot ("{0}_SafetyGate.json" -f $Stage)
$preStageGatePath = Join-Path $gateRoot 'PRE_STAGE_SafetyGate.json'
$preMigrationGatePath = Join-Path $gateRoot 'PRE_MIGRATION57_SafetyGate.json'
$postMigrationGatePath = Join-Path $gateRoot 'POST_MIGRATION57_SafetyGate.json'
$receiptPath = Join-Path $runRoot 'DoorToLevel57HarnessReceipt.json'
$validationEvidencePath = Join-Path $runRoot 'DoorToLevelVisualAssets57Validation.json'
$migrationEvidencePath = Join-Path $runRoot 'DoorToLevelVisualAssets57Migration.json'
$resaveEvidencePath = Join-Path $runRoot 'DoorToLevel58Rebuild.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-IsUnderRoot -Path $runRoot -Root $runsRoot) 'Run evidence escapes target Saved/Migration.'
Assert-True (Test-Path -LiteralPath $sourceGateScript -PathType Leaf) 'Source read-only gate script is absent.'
Assert-True (Test-Path -LiteralPath $protectedGateScript -PathType Leaf) 'Protected-invariant gate script is absent.'
Assert-True (-not (Test-Path -LiteralPath $aggregateOutput)) "Stage evidence already exists for immutable run ${RunId}: $Stage"

$sourceRows = @()
foreach ($asset in $assets) {
    $sourceFile = Join-Path $sourceContent $asset.RelativeFile
    Assert-True (Test-Path -LiteralPath $sourceFile -PathType Leaf) "Frozen source visual asset is absent: $($asset.Package)"
    Assert-True ((Get-Item -LiteralPath $sourceFile).Length -eq $asset.Length) "Frozen source visual length changed: $($asset.Package)"
    Assert-True ((Get-Sha256 -Path $sourceFile) -eq $asset.Sha256) "Frozen source visual hash changed: $($asset.Package)"
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

$legacySourceFile = Join-Path $sourceContent $legacyBlueprint.RelativeFile
Assert-True (Test-Path -LiteralPath $legacySourceFile -PathType Leaf) 'Frozen legacy DoorToLevel Blueprint is absent.'
Assert-True ((Get-Item -LiteralPath $legacySourceFile).Length -eq $legacyBlueprint.Length) 'Frozen legacy DoorToLevel length changed.'
Assert-True ((Get-Sha256 -Path $legacySourceFile) -eq $legacyBlueprint.Sha256) 'Frozen legacy DoorToLevel hash changed.'
foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
    Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($legacySourceFile, $extension)))) "Unexpected legacy source Blueprint sidecar exists: $extension"
}

New-Item -ItemType Directory -Path $gateRoot -Force | Out-Null
$powerShellExe = (Get-Process -Id $PID).Path
if (-not (Test-Path -LiteralPath $powerShellExe -PathType Leaf)) {
    $powerShellExe = Join-Path $PSHOME 'powershell.exe'
}
Assert-True (Test-Path -LiteralPath $powerShellExe -PathType Leaf) 'Current PowerShell executable is absent.'

& $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $sourceGateScript `
    -ProjectRoot $target `
    -SourceRoot $source `
    -OutputPath $sourceGateOutput
Assert-True ($LASTEXITCODE -eq 0) "Source read-only gate failed for $Stage."

& $powerShellExe -NoProfile -ExecutionPolicy Bypass -File $protectedGateScript `
    -ProjectRoot $target `
    -OutputPath $protectedGateOutput
$protectedExitCode = $LASTEXITCODE

$sourceGate = Read-Evidence -Path $sourceGateOutput -Label 'Source read-only evidence'
$protectedGate = Read-Evidence -Path $protectedGateOutput -Label 'Protected-invariant evidence'
Assert-True ([bool]$sourceGate.pass) 'Source read-only evidence is not PASS.'
$knownAcfuPdbDrift = $false
if ([string]$protectedGate.result -eq 'PASS') {
    Assert-True ($protectedExitCode -eq 0) "Protected-invariant process failed for $Stage despite PASS evidence."
}
elseif ($AllowKnownAcfuPdbDrift -and (Test-KnownAcfuPdbDrift -Evidence $protectedGate)) {
    $knownAcfuPdbDrift = $true
}
else {
    Assert-True $false "Protected-invariant gate failed for $Stage with an unapproved delta."
}

$receipt = $null
$validationEvidence = $null
$migrationEvidence = $null
$resaveEvidence = $null
$preStageGate = $null
$preMigrationGate = $null
$postMigrationGate = $null
$evidenceRows = @{}
$inputChain = [ordered]@{}

if ($Stage -ne 'PRE_STAGE') {
    $preStageGate = Read-Evidence -Path $preStageGatePath -Label 'PRE_STAGE gate evidence'
    $receipt = Read-Evidence -Path $receiptPath -Label 'DoorToLevel harness receipt'
    $preStageGateHash = Get-Sha256 -Path $preStageGatePath
    $receiptHash = Get-Sha256 -Path $receiptPath
    Assert-True ([string]$preStageGate.run_id -eq $RunId) 'PRE_STAGE gate belongs to a different run.'
    Assert-True ([string]$preStageGate.stage -eq 'PRE_STAGE') 'PRE_STAGE gate stage differs.'
    Assert-True ([string]$preStageGate.status -eq 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS') 'PRE_STAGE gate is not PASS.'
    Assert-GateChildEvidence -Gate $preStageGate -Label 'PRE_STAGE'
    Assert-True ([string]$receipt.run_id -eq $RunId) 'Harness receipt belongs to a different run.'
    Assert-True ([string]$receipt.status -eq 'ISOLATED_DOOR_VISUAL57_HARNESS_PASS') 'Harness receipt is not PASS.'
    Assert-True (Test-SamePath -Left ([string]$receipt.pre_stage_safety.evidence) -Right $preStageGatePath) 'Harness receipt points to a different PRE_STAGE gate.'
    Assert-True ([string]$receipt.pre_stage_safety.evidence_sha256 -eq $preStageGateHash) 'Harness receipt PRE_STAGE hash differs.'
    $inputChain.pre_stage_gate = [ordered]@{ path = $preStageGatePath; sha256 = $preStageGateHash }
    $inputChain.receipt = [ordered]@{ path = $receiptPath; sha256 = $receiptHash }
}

if ($Stage -in @('PRE_MIGRATION57', 'POST_MIGRATION57', 'POST_RESAVE58')) {
    $validationEvidence = Read-Evidence -Path $validationEvidencePath -Label 'UE 5.7 validation evidence'
    $validationHash = Get-Sha256 -Path $validationEvidencePath
    $receiptHash = Get-Sha256 -Path $receiptPath
    Assert-True ([string]$validationEvidence.run_id -eq $RunId) 'UE 5.7 validation belongs to a different run.'
    Assert-True ([string]$validationEvidence.status -eq 'UE57_DOOR_VISUAL_READ_ONLY_LOAD_PASS') 'UE 5.7 validation is not PASS.'
    Assert-True ([string]$validationEvidence.receipt_sha256 -eq $receiptHash) 'UE 5.7 validation receipt hash differs.'
    Assert-True ([string]$validationEvidence.pre_stage_gate_sha256 -eq (Get-Sha256 -Path $preStageGatePath)) 'UE 5.7 validation PRE_STAGE hash differs.'
    $inputChain.validation = [ordered]@{ path = $validationEvidencePath; sha256 = $validationHash }
}

if ($Stage -in @('POST_MIGRATION57', 'POST_RESAVE58')) {
    $preMigrationGate = Read-Evidence -Path $preMigrationGatePath -Label 'PRE_MIGRATION57 gate evidence'
    $migrationEvidence = Read-Evidence -Path $migrationEvidencePath -Label 'UE 5.7 visual migration evidence'
    $preMigrationGateHash = Get-Sha256 -Path $preMigrationGatePath
    $migrationHash = Get-Sha256 -Path $migrationEvidencePath
    Assert-True ([string]$preMigrationGate.run_id -eq $RunId) 'PRE_MIGRATION57 gate belongs to a different run.'
    Assert-True ([string]$preMigrationGate.stage -eq 'PRE_MIGRATION57') 'PRE_MIGRATION57 gate stage differs.'
    Assert-True ([string]$preMigrationGate.status -eq 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS') 'PRE_MIGRATION57 gate is not PASS.'
    Assert-GateChildEvidence -Gate $preMigrationGate -Label 'PRE_MIGRATION57'
    Assert-True ([string]$migrationEvidence.run_id -eq $RunId) 'UE 5.7 migration belongs to a different run.'
    Assert-True ([string]$migrationEvidence.status -eq 'ASSETTOOLS_EXACT_DOOR_VISUAL_MIGRATION_PASS') 'UE 5.7 visual migration evidence is not PASS.'
    Assert-True ([int]$migrationEvidence.package_count -eq $expectedPackageCount) 'UE 5.7 visual migration count differs.'
    Assert-True (@($migrationEvidence.packages).Count -eq $expectedPackageCount) 'UE 5.7 visual migration evidence row count differs.'
    Assert-True ([string]$migrationEvidence.source_fingerprint -eq $expectedSourceFingerprint) 'UE 5.7 visual source fingerprint differs.'
    Assert-True ([bool]$migrationEvidence.target_delta_exact) 'UE 5.7 visual target delta is not exact.'
    Assert-True ([string]$migrationEvidence.global_dirty_save_gate -eq 'PASS_EMPTY_CONTENT_AND_MAPS') 'UE 5.7 dirty-package gate is not PASS_EMPTY_CONTENT_AND_MAPS.'
    Assert-True (@($migrationEvidence.dirty_content_packages_immediately_before_migration).Count -eq 0) 'UE 5.7 migration began with dirty content packages.'
    Assert-True (@($migrationEvidence.dirty_map_packages_immediately_before_migration).Count -eq 0) 'UE 5.7 migration began with dirty map packages.'
    Assert-True (@($migrationEvidence.dirty_content_packages_immediately_after_migration).Count -eq 0) 'UE 5.7 migration left dirty content packages.'
    Assert-True (@($migrationEvidence.dirty_map_packages_immediately_after_migration).Count -eq 0) 'UE 5.7 migration left dirty map packages.'
    Assert-True ([string]$migrationEvidence.receipt_sha256 -eq (Get-Sha256 -Path $receiptPath)) 'UE 5.7 migration receipt hash differs.'
    Assert-True ([string]$migrationEvidence.validation_sha256 -eq (Get-Sha256 -Path $validationEvidencePath)) 'UE 5.7 migration validation hash differs.'
    Assert-True ([string]$migrationEvidence.pre_migration_gate_sha256 -eq $preMigrationGateHash) 'UE 5.7 migration PRE_MIGRATION57 hash differs.'
    Assert-True ([string]$preMigrationGate.input_chain.pre_stage_gate.sha256 -eq (Get-Sha256 -Path $preStageGatePath)) 'PRE_MIGRATION57 PRE_STAGE chain hash differs.'
    Assert-True ([string]$preMigrationGate.input_chain.receipt.sha256 -eq (Get-Sha256 -Path $receiptPath)) 'PRE_MIGRATION57 receipt chain hash differs.'
    Assert-True ([string]$preMigrationGate.input_chain.validation.sha256 -eq (Get-Sha256 -Path $validationEvidencePath)) 'PRE_MIGRATION57 validation chain hash differs.'
    foreach ($row in @($migrationEvidence.packages)) { $evidenceRows[[string]$row.package] = $row }
    Assert-True ($evidenceRows.Count -eq $expectedPackageCount) 'UE 5.7 visual migration package set differs.'
    $inputChain.pre_migration_gate = [ordered]@{ path = $preMigrationGatePath; sha256 = $preMigrationGateHash }
    $inputChain.migration = [ordered]@{ path = $migrationEvidencePath; sha256 = $migrationHash }
}

if ($Stage -eq 'POST_RESAVE58') {
    $postMigrationGate = Read-Evidence -Path $postMigrationGatePath -Label 'POST_MIGRATION57 gate evidence'
    $resaveEvidence = Read-Evidence -Path $resaveEvidencePath -Label 'UE 5.8 DoorToLevel rebuild evidence'
    $postMigrationGateHash = Get-Sha256 -Path $postMigrationGatePath
    $resaveHash = Get-Sha256 -Path $resaveEvidencePath
    Assert-True ([string]$postMigrationGate.run_id -eq $RunId) 'POST_MIGRATION57 gate belongs to a different run.'
    Assert-True ([string]$postMigrationGate.stage -eq 'POST_MIGRATION57') 'POST_MIGRATION57 gate stage differs.'
    Assert-True ([string]$postMigrationGate.status -eq 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS') 'POST_MIGRATION57 gate is not PASS.'
    Assert-GateChildEvidence -Gate $postMigrationGate -Label 'POST_MIGRATION57'
    Assert-True ([string]$resaveEvidence.run_id -eq $RunId) 'UE 5.8 rebuild belongs to a different run.'
    Assert-True ([string]$resaveEvidence.status -eq 'UE58_DOOR_TO_LEVEL_REBUILD_RESAVE_RELOAD_PASS') 'UE 5.8 DoorToLevel rebuild evidence is not PASS.'
    Assert-True ([int]$resaveEvidence.visual_package_count -eq $expectedPackageCount) 'UE 5.8 visual package count differs.'
    Assert-True (@($resaveEvidence.target_packages).Count -eq ($expectedPackageCount + 1)) 'UE 5.8 target evidence row count differs.'
    Assert-True ([string]$resaveEvidence.source_fingerprint -eq $expectedSourceFingerprint) 'UE 5.8 visual source fingerprint differs.'
    Assert-True ([bool]$resaveEvidence.target_delta_exact) 'UE 5.8 target delta is not exact.'
    Assert-True ([bool]$resaveEvidence.dirty_content_probe_fail_closed) 'UE 5.8 dirty-content probe was not fail-closed.'
    Assert-True (@($resaveEvidence.new_dirty_packages_after_rebuild).Count -eq 0) 'UE 5.8 rebuild introduced dirty packages.'
    Assert-True (@($resaveEvidence.unexpected_dirty_packages_after_rebuild).Count -eq 0) 'UE 5.8 rebuild left unrelated dirty packages.'
    Assert-True ([string]$resaveEvidence.receipt_sha256 -eq (Get-Sha256 -Path $receiptPath)) 'UE 5.8 rebuild receipt hash differs.'
    Assert-True ([string]$resaveEvidence.validation_sha256 -eq (Get-Sha256 -Path $validationEvidencePath)) 'UE 5.8 rebuild validation hash differs.'
    Assert-True ([string]$resaveEvidence.migration_sha256 -eq (Get-Sha256 -Path $migrationEvidencePath)) 'UE 5.8 rebuild migration hash differs.'
    Assert-True ([string]$resaveEvidence.post_migration_gate_sha256 -eq $postMigrationGateHash) 'UE 5.8 rebuild POST_MIGRATION57 hash differs.'
    Assert-True ([string]$postMigrationGate.input_chain.receipt.sha256 -eq (Get-Sha256 -Path $receiptPath)) 'POST_MIGRATION57 receipt chain hash differs.'
    Assert-True ([string]$postMigrationGate.input_chain.validation.sha256 -eq (Get-Sha256 -Path $validationEvidencePath)) 'POST_MIGRATION57 validation chain hash differs.'
    Assert-True ([string]$postMigrationGate.input_chain.migration.sha256 -eq (Get-Sha256 -Path $migrationEvidencePath)) 'POST_MIGRATION57 migration chain hash differs.'
    $evidenceRows = @{}
    foreach ($row in @($resaveEvidence.target_packages)) { $evidenceRows[[string]$row.package] = $row }
    Assert-True ($evidenceRows.Count -eq ($expectedPackageCount + 1)) 'UE 5.8 target package set differs.'
    $inputChain.post_migration_gate = [ordered]@{ path = $postMigrationGatePath; sha256 = $postMigrationGateHash }
    $inputChain.rebuild = [ordered]@{ path = $resaveEvidencePath; sha256 = $resaveHash }
}

$targetRows = @()
foreach ($asset in $assets) {
    $targetFile = Join-Path $targetContent $asset.RelativeFile
    $exists = Test-Path -LiteralPath $targetFile -PathType Leaf
    if ($Stage -in @('PRE_STAGE', 'PRE_MIGRATION57')) {
        Assert-True (-not $exists) "Target visual collision exists before migration: $($asset.Package)"
    }
    else {
        Assert-True $exists "Target visual asset is absent after migration/resave: $($asset.Package)"
        Assert-True ($evidenceRows.ContainsKey($asset.Package)) "Target visual evidence row is absent: $($asset.Package)"
        $row = $evidenceRows[$asset.Package]
        $actualLength = [int64](Get-Item -LiteralPath $targetFile).Length
        $actualSha256 = Get-Sha256 -Path $targetFile
        Assert-True ($actualLength -eq [int64]$row.length) "Target visual length differs from evidence: $($asset.Package)"
        Assert-True ($actualSha256 -eq [string]$row.sha256) "Target visual hash differs from evidence: $($asset.Package)"
    }
    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($targetFile, $extension)))) "Unexpected target visual sidecar exists for $($asset.Package): $extension"
    }
    $targetRows += [ordered]@{
        package = $asset.Package
        file = $targetFile
        exists = $exists
        length = if ($exists) { [int64](Get-Item -LiteralPath $targetFile).Length } else { $null }
        sha256 = if ($exists) { Get-Sha256 -Path $targetFile } else { $null }
    }
}

$targetBlueprintFile = Join-Path $targetContent $legacyBlueprint.RelativeFile
$targetBlueprintExists = Test-Path -LiteralPath $targetBlueprintFile -PathType Leaf
foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
    Assert-True (-not (Test-Path -LiteralPath ([System.IO.Path]::ChangeExtension($targetBlueprintFile, $extension)))) "Unexpected target DoorToLevel sidecar exists: $extension"
}
if ($Stage -ne 'POST_RESAVE58') {
    Assert-True (-not $targetBlueprintExists) "DoorToLevel target Blueprint must remain absent at $Stage."
}
else {
    Assert-True $targetBlueprintExists 'Rebuilt DoorToLevel target Blueprint is absent.'
    Assert-True ($evidenceRows.ContainsKey($legacyBlueprint.Package)) 'Rebuilt DoorToLevel evidence row is absent.'
    $blueprintRow = $evidenceRows[$legacyBlueprint.Package]
    Assert-True ((Get-Item -LiteralPath $targetBlueprintFile).Length -eq [int64]$blueprintRow.length) 'Rebuilt DoorToLevel length differs from evidence.'
    Assert-True ((Get-Sha256 -Path $targetBlueprintFile) -eq [string]$blueprintRow.sha256) 'Rebuilt DoorToLevel hash differs from evidence.'
}

$payload = [ordered]@{
    schema_version = 2
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS'
    run_id = $RunId
    stage = $Stage
    source_root = $source
    target_root = $target
    run_root = $runRoot
    visual_package_count = $expectedPackageCount
    source_bytes = $expectedSourceBytes
    source_fingerprint = $expectedSourceFingerprint
    source_assets = $sourceRows
    legacy_blueprint_reference = [ordered]@{
        package = $legacyBlueprint.Package
        file = $legacySourceFile
        length = $legacyBlueprint.Length
        sha256 = $legacyBlueprint.Sha256
        policy = 'READ_ONLY_REFERENCE_ONLY_REBUILD_IN_UE58'
    }
    target_visual_assets = $targetRows
    target_blueprint = [ordered]@{
        package = $legacyBlueprint.Package
        file = $targetBlueprintFile
        exists = $targetBlueprintExists
        length = if ($targetBlueprintExists) { [int64](Get-Item -LiteralPath $targetBlueprintFile).Length } else { $null }
        sha256 = if ($targetBlueprintExists) { Get-Sha256 -Path $targetBlueprintFile } else { $null }
    }
    source_read_only = [ordered]@{
        result = 'PASS'
        evidence = $sourceGateOutput
        evidence_sha256 = Get-Sha256 -Path $sourceGateOutput
    }
    protected_invariants = [ordered]@{
        result = 'PASS'
        baseline_result = [string]$protectedGate.result
        disposition = if ($knownAcfuPdbDrift) { 'PASS_EXPECTED_DELTA_KNOWN_ACFU_PDB_QUARANTINE' } else { 'PASS' }
        known_acfu_pdb_drift_allowed = $knownAcfuPdbDrift
        evidence = $protectedGateOutput
        evidence_sha256 = Get-Sha256 -Path $protectedGateOutput
    }
    input_chain = $inputChain
    batch_evidence = if ($Stage -eq 'POST_MIGRATION57') { $migrationEvidencePath } elseif ($Stage -eq 'POST_RESAVE58') { $resaveEvidencePath } else { $null }
    source_tree_mounted = $false
    raw_target_asset_copy_requested = $false
    target_sidecars_absent = $true
}
$payload | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $aggregateOutput -Encoding UTF8

Write-Host "DOOR_TO_LEVEL_SAFETY_GATE_PASS: $Stage / $RunId"
Write-Host "Evidence: $aggregateOutput"
