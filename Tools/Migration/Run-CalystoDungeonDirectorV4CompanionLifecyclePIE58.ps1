[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202608210404,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunSeed -le 0) { throw 'The V4 companion lifecycle gate requires a positive Int64 seed.' }

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV4CompanionLifecyclePIE58.py'
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV4\CompanionLifecyclePIE_$Stamp"
$output = Join-Path $runRoot 'CompanionLifecycle.json'
$log = Join-Path $runRoot 'CompanionLifecycle.log'
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
$oldOutput = $env:CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_OUTPUT
$oldSeed = $env:CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_SEED
Assert-DazEditorReceipt -Phase 'pre-run'
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
    $env:CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_OUTPUT = $output
    $env:CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_SEED = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    $arguments = @('-unattended','-nop4','-nosplash','-NoSound','-NullRHI',"-ABSLOG=`"$log`"") -join ' '
    & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
    if ($LASTEXITCODE -ne 0) { throw "Protected launcher failed with exit code $LASTEXITCODE." }
    Assert-DazEditorReceipt -Phase 'post-run'
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V4_COMPANION_LIFECYCLE_SEED = $oldSeed
    Assert-DazEditorReceipt -Phase 'final'
}

foreach ($artifact in @($output, $log)) {
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or (Get-Item -LiteralPath $artifact).Length -le 0) {
        throw "The lifecycle gate did not produce a non-empty artifact: $artifact"
    }
}
$receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
if (-not [bool]$receipt.success -or [string]$receipt.status -ne 'PASS') {
    throw "The lifecycle gate failed in phase $($receipt.phase): $($receipt.error)"
}
if ([int]$receipt.schema_version -ne 4 -or [int]$receipt.generator_version -ne 4) {
    throw 'The lifecycle receipt does not identify V4.'
}
if (@($receipt.asset_saves).Count -ne 0 -or @($receipt.asset_mutations).Count -ne 0 -or @($receipt.protected_assets.mismatches).Count -ne 0) {
    throw 'The lifecycle gate changed Content or a protected package.'
}
if ([string]$receipt.policy_sha256_before -cne [string]$receipt.policy_sha256_after) {
    throw 'The lifecycle gate changed the authored V4 policy bytes.'
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
    if (@($entry.Value).Count -ne 6) { throw "Lifecycle gate emitted $(@($entry.Value).Count) $($entry.Key) records; expected exactly 6." }
}
for ($index = 0; $index -lt 6; ++$index) {
    if (-not ($pcg[$index].LineNumber -le $nav[$index].LineNumber -and
        $nav[$index].LineNumber -le $levels[$index].LineNumber -and
        $levels[$index].LineNumber -le $population[$index].LineNumber -and
        $population[$index].LineNumber -le $roster[$index].LineNumber -and
        $roster[$index].LineNumber -le $door[$index].LineNumber)) {
        throw "Lifecycle readiness sequence $index violated PCG <= Nav <= EnemyLevels <= Population <= Companion <= Door."
    }
}

$verified = @($lines | Select-String -SimpleMatch 'CALYSTO_V4_COMPANION_VERIFIED')
if ($verified.Count -ne 5) { throw "Lifecycle gate emitted $($verified.Count) verified companion records; expected 5." }
$local = @($verified | Where-Object { $_.Line -match 'hook=true\s+roster_projection=false' })
$projected = @($verified | Where-Object { $_.Line -match 'hook=false\s+roster_projection=true' })
if ($local.Count -ne 2 -or $projected.Count -ne 3) {
    throw "Lifecycle gate expected 2 local and 3 roster-projected companion records; found $($local.Count)/$($projected.Count)."
}
$death = @($lines | Select-String -Pattern 'Companion\s+(?<id>[0-9A-Fa-f]{32})\s+entered PendingDead from ACF at floor 2 serial (?<serial>[23])\.')
if ($death.Count -ne 3) { throw "Lifecycle gate emitted $($death.Count) canonical ACF PendingDead records; expected 3." }
$deathIds = @($death | ForEach-Object { $_.Matches[0].Groups['id'].Value.ToUpperInvariant() } | Select-Object -Unique)
$deathSerials = @($death | ForEach-Object { [int]$_.Matches[0].Groups['serial'].Value })
if ($deathIds.Count -ne 1 -or @($deathSerials | Where-Object { $_ -eq 2 }).Count -ne 2 -or @($deathSerials | Where-Object { $_ -eq 3 }).Count -ne 1) {
    throw 'Lifecycle death records did not preserve one stable ID and serial sequence 2,2,3.'
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
if ($findings.Count -ne 0) { throw "Lifecycle log contains blocked diagnostics:`n$($findings -join [Environment]::NewLine)" }

$summary = [ordered]@{
    schema_version = 4
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'PASS'
    run_seed = $RunSeed
    receipt = $output
    log = $log
    generation_count = 6
    verified_local_companions = $local.Count
    verified_roster_projections = $projected.Count
    canonical_acf_deaths = $death.Count
    stable_companion_id = $deathIds[0]
    lifecycle = $receipt.lifecycle
}
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
Write-Host 'Dungeon Director V4 companion lifecycle PIE gate: PASS'
Write-Host "Evidence: $summaryPath"
