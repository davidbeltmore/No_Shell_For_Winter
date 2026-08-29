[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202608210505,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunSeed -le 0) { throw 'The V4 companion recall gate requires a positive Int64 seed.' }

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV4CompanionRecallPIE58.py'
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV4\CompanionRecallPIE_$Stamp"
$output = Join-Path $runRoot 'CompanionRecall.json'
$log = Join-Path $runRoot 'CompanionRecall.log'
foreach ($path in @($launcher, $receiptGuard, $validator)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required path is missing: $path" }
}
if (Test-Path -LiteralPath $runRoot) { throw "Evidence already exists; refusing reuse: $runRoot" }
[void][IO.Directory]::CreateDirectory($runRoot)

function Assert-DazEditorReceipt {
    param([Parameter(Mandatory = $true)][string]$Phase)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $root -TargetName NoShellForWinterEditor -Configuration Development -VerifyOnly
    if ($LASTEXITCODE -ne 0) { throw "Daz editor receipt verification failed during $Phase." }
}

$oldBaseline = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldOutput = $env:CODEX_CALYSTO_V4_COMPANION_RECALL_OUTPUT
$oldSeed = $env:CODEX_CALYSTO_V4_COMPANION_RECALL_SEED
Assert-DazEditorReceipt -Phase 'pre-run'
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
    $env:CODEX_CALYSTO_V4_COMPANION_RECALL_OUTPUT = $output
    $env:CODEX_CALYSTO_V4_COMPANION_RECALL_SEED = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    $arguments = @('-unattended','-nop4','-nosplash','-NoSound','-NullRHI',"-ABSLOG=`"$log`"") -join ' '
    & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
    if ($LASTEXITCODE -ne 0) { throw "Protected launcher failed with exit code $LASTEXITCODE." }
    Assert-DazEditorReceipt -Phase 'post-run'
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V4_COMPANION_RECALL_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V4_COMPANION_RECALL_SEED = $oldSeed
    Assert-DazEditorReceipt -Phase 'final'
}

foreach ($artifact in @($output, $log)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or (Get-Item -LiteralPath $artifact).Length -le 0) {
        throw "The companion recall gate did not produce a non-empty artifact: $artifact"
    }
}
$receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
$allowedStatuses = @('PASS', 'PASS_WITH_GAPS')
if (-not [bool]$receipt.success -or [string]$receipt.status -notin $allowedStatuses) {
    throw "The companion recall gate failed in phase $($receipt.phase): $($receipt.error)"
}
if ([int]$receipt.schema_version -ne 4 -or [int]$receipt.generator_version -ne 4) {
    throw 'The companion recall receipt does not identify V4.'
}
if (@($receipt.asset_saves).Count -ne 0 -or @($receipt.asset_mutations).Count -ne 0 -or @($receipt.protected_assets.mismatches).Count -ne 0) {
    throw 'The companion recall gate changed Content or a protected package.'
}
if ([string]$receipt.policy_sha256_before -cne [string]$receipt.policy_sha256_after) {
    throw 'The companion recall gate changed the authored V4 policy bytes.'
}

$gapCount = @($receipt.capability_gaps).Count
if ([string]$receipt.status -eq 'PASS') {
    if (-not [bool]$receipt.strict_acceptance -or -not [bool]$receipt.full_transaction_pass -or $gapCount -ne 0) {
        throw 'A PASS receipt must prove the complete cancel/confirm transaction with zero capability gaps.'
    }
}
else {
    if ([bool]$receipt.strict_acceptance -or [bool]$receipt.full_transaction_pass -or $gapCount -eq 0) {
        throw 'PASS_WITH_GAPS must remain non-strict and name at least one Unreal Python reflection gap.'
    }
}

$lines = Get-Content -LiteralPath $log
$generate = @($lines | Select-String -SimpleMatch 'Calysto V4 adapter requested GenerateLocal exactly once')
$pcg = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PCGComplete world=')
$nav = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: NavigationPathReady world=')
$levels = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: EnemyLevelsReady world=')
$population = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: PopulationRealized world=')
$roster = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: CompanionRosterReady world=')
$door = @($lines | Select-String -SimpleMatch 'LogEFProceduralPCGRuntime: DoorEnabled world=')
foreach ($entry in ([ordered]@{ GenerateLocal=$generate; PCG=$pcg; Navigation=$nav; EnemyLevels=$levels; Population=$population; CompanionRoster=$roster; Door=$door }).GetEnumerator()) {
    if (@($entry.Value).Count -ne 3) { throw "Companion recall gate emitted $(@($entry.Value).Count) $($entry.Key) records; expected exactly 3." }
}
for ($index = 0; $index -lt 3; ++$index) {
    if (-not ($pcg[$index].LineNumber -le $nav[$index].LineNumber -and
        $nav[$index].LineNumber -le $levels[$index].LineNumber -and
        $levels[$index].LineNumber -le $population[$index].LineNumber -and
        $population[$index].LineNumber -le $roster[$index].LineNumber -and
        $roster[$index].LineNumber -le $door[$index].LineNumber)) {
        throw "Companion recall readiness sequence $index violated PCG <= Nav <= EnemyLevels <= Population <= Companion <= Door."
    }
}

$verified = @($lines | Select-String -SimpleMatch 'CALYSTO_V4_COMPANION_VERIFIED')
if ($verified.Count -ne 3) { throw "Companion recall gate emitted $($verified.Count) verified companion records; expected 3." }
$local = @($verified | Where-Object { $_.Line -match 'hook=true\s+roster_projection=false' })
$projected = @($verified | Where-Object { $_.Line -match 'roster_projection=true' })
if ($local.Count -ne 3 -or $projected.Count -ne 0) {
    throw "Companion recall fixture expected three local recruitable records and no travel projection; found $($local.Count)/$($projected.Count)."
}

$death = @($lines | Select-String -Pattern 'Companion\s+(?<id>[0-9A-Fa-f]{32})\s+entered PendingDead from ACF at floor (?<floor>[12]) serial (?<serial>[12])\.')
if ($death.Count -ne 2) { throw "Companion recall gate emitted $($death.Count) canonical ACF PendingDead records; expected 2." }
$deathIds = @($death | ForEach-Object { $_.Matches[0].Groups['id'].Value.ToUpperInvariant() } | Select-Object -Unique)
$deathPairs = @($death | ForEach-Object { "$($_.Matches[0].Groups['floor'].Value):$($_.Matches[0].Groups['serial'].Value)" })
if ($deathIds.Count -ne 2 -or $deathPairs -notcontains '1:1' -or $deathPairs -notcontains '2:2') {
    throw 'Companion recall death records did not preserve distinct Female/Male IDs at floor/serial 1:1 and 2:2.'
}

$selection = @($lines | Select-String -SimpleMatch 'CALYSTO_V4_COMPANION_RECALL_SELECTION')
if ([string]$receipt.status -eq 'PASS' -and $selection.Count -ne 2) {
    throw "Strict companion recall expected two real selection tokens; found $($selection.Count)."
}
if ([string]$receipt.status -eq 'PASS_WITH_GAPS' -and $selection.Count -gt 1) {
    throw 'Reflection-gap coverage emitted an unexpected number of captured selection tokens.'
}

$blockedPatterns = @(
    'Blueprint Runtime Error','LogBlueprint: Error','Accessed None','Ensure condition failed',
    'Fatal error:','Assertion failed:','Object Transform','GetAttributeFromPointIndex_0',
    'Calysto controlled generation failed','Calysto generation failed','duplicate generation',
    'duplicated generation','LogEFCalystoDungeon: Error','LogEFCalystoPopulationV4: Error',
    'LogEFProceduralPCGRuntime: Error','LogProjectRunCompanions: Error'
)
$findings = @(
    foreach ($pattern in $blockedPatterns) {
        $lines | Select-String -SimpleMatch $pattern | ForEach-Object { "[$pattern] $($_.Line.Trim())" }
    }
)
if ($findings.Count -ne 0) { throw "Companion recall log contains blocked diagnostics:`n$($findings -join [Environment]::NewLine)" }

$summary = [ordered]@{
    schema_version = 4
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = [string]$receipt.status
    strict_acceptance = [bool]$receipt.strict_acceptance
    full_transaction_pass = [bool]$receipt.full_transaction_pass
    capability_gaps = @($receipt.capability_gaps)
    run_seed = $RunSeed
    receipt = $output
    log = $log
    generation_count = 3
    verified_local_companions = $local.Count
    verified_roster_projections = $projected.Count
    canonical_acf_deaths = $death.Count
    stable_companion_ids = $deathIds
    selection_token_count = $selection.Count
    recall_lifecycle = $receipt.recall_lifecycle
}
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 30), [Text.UTF8Encoding]::new($false))
Write-Host "Dungeon Director V4 companion recall PIE gate: $($receipt.status)"
if ($gapCount -gt 0) {
    Write-Warning "Strict acceptance remains PENDING because Unreal Python could not expose: $(@($receipt.capability_gaps) -join '; ')"
}
Write-Host "Evidence: $summaryPath"
