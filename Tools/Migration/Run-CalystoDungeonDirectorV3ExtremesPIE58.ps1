[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202608143030,
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
if ($RunSeed -le 0) {
    throw 'Dungeon Director V3 extreme PIE requires a positive Int64 seed.'
}

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV3ExtremesPIE58.py'
$evidenceRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV3\ExtremePIE_$Stamp"
$logRoot = Join-Path $root "Saved\Migration\Logs\CalystoDungeonDirectorV3_ExtremePIE_$Stamp"
foreach ($requiredPath in @($launcher, $validator)) {
    if (!(Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required V3 extreme PIE path not found: $requiredPath"
    }
}
[void][IO.Directory]::CreateDirectory($evidenceRoot)
[void][IO.Directory]::CreateDirectory($logRoot)

$scenarios = @('Zero', 'EnemyCap25', 'ResourceMin', 'ResourceMax')
$oldBaseline = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldOutput = $env:CODEX_CALYSTO_V3_EXTREME_OUTPUT
$oldScenario = $env:CODEX_CALYSTO_V3_EXTREME_SCENARIO
$oldSeed = $env:CODEX_CALYSTO_V3_EXTREME_SEED
$receipts = @()
try {
    foreach ($scenario in $scenarios) {
        $output = Join-Path $evidenceRoot "$scenario.json"
        $log = Join-Path $logRoot "$scenario.log"
        $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
        $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
        $env:CODEX_CALYSTO_V3_EXTREME_OUTPUT = $output
        $env:CODEX_CALYSTO_V3_EXTREME_SCENARIO = $scenario
        $env:CODEX_CALYSTO_V3_EXTREME_SEED = $RunSeed.ToString(
            [Globalization.CultureInfo]::InvariantCulture)

        $arguments = @(
            '-unattended'
            '-nop4'
            '-nosplash'
            '-NoSound'
            '-stdout'
            '-FullStdOutLogOutput'
            "-ABSLOG=`"$log`""
        ) -join ' '
        & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
        if ($LASTEXITCODE -ne 0) {
            throw "Protected launcher failed for $scenario with exit code $LASTEXITCODE."
        }
        if (!(Test-Path -LiteralPath $output -PathType Leaf) -or
            !(Test-Path -LiteralPath $log -PathType Leaf)) {
            throw "Scenario $scenario did not produce both receipt and log."
        }

        $receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
        if (!$receipt.success -or [string]$receipt.status -ne 'PASS') {
            throw "Scenario $scenario failed: $($receipt.error)"
        }
        if (@($receipt.asset_mutations).Count -ne 0 -or
            @($receipt.asset_saves).Count -ne 0 -or
            @($receipt.protected_assets.mismatches).Count -ne 0) {
            throw "Scenario $scenario changed assets."
        }
        if ([string]$receipt.policy_sha256_before -ne [string]$receipt.policy_sha256_after) {
            throw "Scenario $scenario changed the authored V3 policy bytes."
        }
        $falseChecks = @(
            $receipt.realized.checks.PSObject.Properties |
                Where-Object { ![bool]$_.Value } |
                ForEach-Object { $_.Name }
        )
        if ($falseChecks.Count -ne 0) {
            throw "Scenario $scenario reported false checks: $($falseChecks -join ', ')"
        }

        $lines = Get-Content -LiteralPath $log
        $generateLines = @($lines | Select-String -SimpleMatch `
            'adapter requested GenerateLocal exactly once')
        $scenarioLines = @($lines | Select-String -SimpleMatch `
            "CALYSTO_POPULATION_SCENARIO scenario=$scenario")
        $pcg = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: PCGComplete world=')[0].LineNumber
        $nav = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: NavigationPathReady world=')[0].LineNumber
        $population = @($lines | Select-String -SimpleMatch `
            'LogEFCalystoPopulation: PopulationRealized floor=')[0].LineNumber
        $door = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: DoorEnabled world=')[0].LineNumber
        if ($generateLines.Count -ne 1 -or $scenarioLines.Count -ne 1) {
            throw "Scenario $scenario did not arm once and call exactly one GenerateLocal."
        }
        if (!($pcg -le $nav -and $nav -le $population -and $population -le $door)) {
            throw "Scenario $scenario violated PCGComplete <= NavigationPathReady <= PopulationRealized <= DoorEnabled."
        }

        $blockedPatterns = @(
            'Blueprint Runtime Error', 'LogBlueprint: Error', 'Accessed None',
            'Ensure condition failed', 'Fatal error:', 'Assertion failed:',
            'Object Transform', 'GetAttributeFromPointIndex_0',
            'Calysto controlled generation failed', 'Calysto generation failed',
            'duplicate generation', 'LogEFCalystoDungeon: Error',
            'LogEFCalystoPopulation: Error', 'LogEFProceduralPCGRuntime: Error'
        )
        $findings = @(
            foreach ($pattern in $blockedPatterns) {
                $lines | Select-String -SimpleMatch $pattern |
                    ForEach-Object { "[$pattern] $($_.Line.Trim())" }
            }
        )
        if ($findings.Count -ne 0) {
            throw "Scenario $scenario log contains blocked diagnostics:`n$($findings -join [Environment]::NewLine)"
        }

        $receipts += [ordered]@{
            scenario = $scenario
            receipt = $output
            log = $log
            counts = $receipt.realized.counts
            actor_count = [int]$receipt.realized.spawned_actor_count
            policy_hash = [string]$receipt.realized.policy_hash
            manifest_hash = [string]$receipt.realized.manifest_hash
            ordering = [ordered]@{
                pcg_complete_line = $pcg
                navigation_ready_line = $nav
                population_realized_line = $population
                door_enabled_line = $door
            }
        }
        Write-Host "Dungeon Director V3 extreme scenario $scenario`: PASS"
    }
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V3_EXTREME_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V3_EXTREME_SCENARIO = $oldScenario
    $env:CODEX_CALYSTO_V3_EXTREME_SEED = $oldSeed
}

$summary = [ordered]@{
    schema_version = 3
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'PASS'
    run_seed = $RunSeed
    scenarios = $receipts
}
$summaryPath = Join-Path $evidenceRoot 'summary.json'
[IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 10),
    [Text.UTF8Encoding]::new($false))
Write-Host 'Dungeon Director V3 exact population materialization matrix: PASS'
Write-Host "Evidence: $summaryPath"
