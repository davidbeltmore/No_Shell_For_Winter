[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202608210404,
    [ValidateSet('Extremes', 'CompanionVariants', 'All')]
    [string]$Matrix = 'Extremes',
    [string]$OnlyScenario = '',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunSeed -le 0) {
    throw 'Dungeon Director V4 extreme PIE requires a positive Int64 seed.'
}

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV4ExtremesPIE58.py'
$evidencePrefix = if ($Matrix -eq 'CompanionVariants') {
    'CompanionVariantsPIE'
} elseif ($Matrix -eq 'All') {
    'PopulationPIE'
} else {
    'ExtremePIE'
}
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV4\$($evidencePrefix)_$Stamp"
$logRoot = Join-Path $runRoot 'Logs'
foreach ($requiredPath in @($launcher, $receiptGuard, $validator)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required V4 extreme PIE path not found: $requiredPath"
    }
}
if (Test-Path -LiteralPath $runRoot) {
    throw "V4 extreme PIE evidence already exists; refusing reuse: $runRoot"
}
[void][IO.Directory]::CreateDirectory($runRoot)
[void][IO.Directory]::CreateDirectory($logRoot)

function Assert-DazEditorReceipt {
    param([Parameter(Mandatory = $true)][string]$Phase)

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $root `
        -TargetName NoShellForWinterEditor `
        -Configuration Development `
        -VerifyOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Daz editor receipt verification failed during $Phase with exit code $LASTEXITCODE."
    }
}

$extremeScenarios = @(
    'Zero',
    'EnemyCap25',
    'ResourceMin',
    'ResourceMax',
    'NPCTotal4',
    'SpecialEvents6'
)
$companionScenarios = @(
    'NPCGeneralistFemale',
    'NPCGeneralistMale',
    'NPCMeleeFemale',
    'NPCMeleeMale',
    'NPCRangedFemale',
    'NPCRangedMale'
)
$scenarios = if ($Matrix -eq 'CompanionVariants') {
    $companionScenarios
} elseif ($Matrix -eq 'All') {
    @($extremeScenarios + $companionScenarios)
} else {
    $extremeScenarios
}
if (-not [string]::IsNullOrWhiteSpace($OnlyScenario)) {
    if ($OnlyScenario -notin $scenarios) {
        throw "Scenario '$OnlyScenario' does not belong to matrix '$Matrix'."
    }
    $scenarios = @($OnlyScenario)
}
$expectedCounts = [ordered]@{
    Zero = [ordered]@{ enemy = 0; npc = 0; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    EnemyCap25 = [ordered]@{ enemy = 25; npc = 0; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    ResourceMin = [ordered]@{ enemy = 0; npc = 0; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    ResourceMax = [ordered]@{ enemy = 0; npc = 0; food = 30; chest = 10; loose_loot = 0; clothing = 0; special_event = 0 }
    NPCTotal4 = [ordered]@{ enemy = 0; npc = 4; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    SpecialEvents6 = [ordered]@{ enemy = 0; npc = 0; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 6 }
    NPCGeneralistFemale = [ordered]@{ enemy = 0; npc = 1; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    NPCGeneralistMale = [ordered]@{ enemy = 0; npc = 1; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    NPCMeleeFemale = [ordered]@{ enemy = 0; npc = 1; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    NPCMeleeMale = [ordered]@{ enemy = 0; npc = 1; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    NPCRangedFemale = [ordered]@{ enemy = 0; npc = 1; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
    NPCRangedMale = [ordered]@{ enemy = 0; npc = 1; food = 0; chest = 0; loose_loot = 0; clothing = 0; special_event = 0 }
}
$expectedCompanionVerification = [ordered]@{
    NPCGeneralistFemale = [ordered]@{
        catalog = 'NPC.Companion.Generalist.Female'
        class = '/Game/_Game/Characters/Female/ACFBaseCompanionBPFemale.ACFBaseCompanionBPFemale_C'
    }
    NPCGeneralistMale = [ordered]@{
        catalog = 'NPC.Companion.Generalist.Male'
        class = '/Game/_Game/Characters/Male/ACFBaseCompanionBPMale.ACFBaseCompanionBPMale_C'
    }
    NPCMeleeFemale = [ordered]@{
        catalog = 'NPC.Companion.Melee.Female'
        class = '/Game/_Game/Characters/Female/ACFMeleeCompanionBPFemale.ACFMeleeCompanionBPFemale_C'
    }
    NPCMeleeMale = [ordered]@{
        catalog = 'NPC.Companion.Melee.Male'
        class = '/Game/_Game/Characters/Male/ACFMeleeCompanionBPMale.ACFMeleeCompanionBPMale_C'
    }
    NPCRangedFemale = [ordered]@{
        catalog = 'NPC.Companion.Ranged.Female'
        class = '/Game/_Game/Characters/Female/ACFRangedCompanionBPFemale.ACFRangedCompanionBPFemale_C'
    }
    NPCRangedMale = [ordered]@{
        catalog = 'NPC.Companion.Ranged.Male'
        class = '/Game/_Game/Characters/Male/ACFRangedCompanionBPMale.ACFRangedCompanionBPMale_C'
    }
}
$statisticsRepairTable = '/AscentCombatFramework/Configuration/ACF_SampleAttributesInit_DT.ACF_SampleAttributesInit_DT'
$statisticsRepairRow = 'MMEnemy'
$companionVerificationPattern =
    'CALYSTO_V4_COMPANION_VERIFIED\s+' +
    'instance=(?<instance>\S+)\s+' +
    'hook=(?<hook>true|false)\s+' +
    'roster_projection=(?<rosterProjection>true|false)\s+' +
    'repair_used=(?<repairUsed>true|false)\s+' +
    'catalog=(?<catalog>\S+)\s+' +
    'class=(?<class>\S+)\s+' +
    'controller=(?<controller>\S+)\s+' +
    'stats_table=(?<statsTable>\S+)\s+' +
    'stats_row=(?<statsRow>\S+)\s+' +
    'logical=(?<logical>\d+)\s+' +
    'physical=(?<physical>\d+)\s+' +
    'team=(?<team>true|false)\s+' +
    'perception=(?<perception>true|false)\s+' +
    'social=(?<social>true|false)(?:\s|$)'

$oldBaseline = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldOutput = $env:CODEX_CALYSTO_V4_EXTREME_OUTPUT
$oldScenario = $env:CODEX_CALYSTO_V4_EXTREME_SCENARIO
$oldSeed = $env:CODEX_CALYSTO_V4_EXTREME_SEED
$receipts = @()
Assert-DazEditorReceipt -Phase 'pre-run'
try {
    foreach ($scenario in $scenarios) {
        $output = Join-Path $runRoot "$scenario.json"
        $log = Join-Path $logRoot "$scenario.log"
        $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
        $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
        $env:CODEX_CALYSTO_V4_EXTREME_OUTPUT = $output
        $env:CODEX_CALYSTO_V4_EXTREME_SCENARIO = $scenario
        $env:CODEX_CALYSTO_V4_EXTREME_SEED = $RunSeed.ToString(
            [Globalization.CultureInfo]::InvariantCulture)

        $arguments = @(
            '-unattended'
            '-nop4'
            '-nosplash'
            '-NoSound'
            # Logic/cap validation has a separate visible QA gate. Avoid charging
            # renderer startup and off-screen frame work to the 30 s PCG budget.
            '-NullRHI'
            "-ABSLOG=`"$log`""
        ) -join ' '
        & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
        if ($LASTEXITCODE -ne 0) {
            throw "Protected launcher failed for $scenario with exit code $LASTEXITCODE."
        }
        Assert-DazEditorReceipt -Phase "post-$scenario"

        foreach ($artifact in @($output, $log)) {
            if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or
                (Get-Item -LiteralPath $artifact).Length -le 0) {
                throw "Scenario $scenario did not produce a non-empty artifact: $artifact"
            }
        }
        try {
            $receipt = Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
        }
        catch {
            throw "Scenario $scenario receipt is invalid JSON: $($_.Exception.Message)"
        }
        if (-not [bool]$receipt.success -or [string]$receipt.status -ne 'PASS') {
            throw "Scenario $scenario failed in phase $($receipt.phase): $($receipt.error)"
        }
        if ([int]$receipt.schema_version -ne 4 -or [int]$receipt.generator_version -ne 4 -or
            [int]$receipt.policy.schema_version -ne 4 -or
            [int]$receipt.policy.generator_version -ne 4 -or
            [string]$receipt.policy.native_validation -ne 'PASS') {
            throw "Scenario $scenario did not validate the exact V4 policy/schema."
        }
        if (@($receipt.asset_mutations).Count -ne 0 -or
            @($receipt.asset_saves).Count -ne 0 -or
            @($receipt.protected_assets.mismatches).Count -ne 0) {
            throw "Scenario $scenario changed Content or a protected package."
        }
        if ([string]$receipt.policy_sha256_before -cne [string]$receipt.policy_sha256_after) {
            throw "Scenario $scenario changed the authored V4 policy bytes."
        }
        $falseChecks = @(
            $receipt.realized.checks.PSObject.Properties |
                Where-Object { -not [bool]$_.Value } |
                ForEach-Object { $_.Name }
        )
        if ($falseChecks.Count -ne 0) {
            throw "Scenario $scenario reported false checks: $($falseChecks -join ', ')"
        }
        foreach ($entry in $expectedCounts[$scenario].GetEnumerator()) {
            if ([int]$receipt.realized.counts.($entry.Key) -ne [int]$entry.Value) {
                throw "Scenario $scenario count '$($entry.Key)' was $($receipt.realized.counts.($entry.Key)); expected $($entry.Value)."
            }
        }

        $lines = Get-Content -LiteralPath $log
        $generateLines = @($lines | Select-String -SimpleMatch `
            'Calysto V4 adapter requested GenerateLocal exactly once')
        $scenarioLines = @($lines | Select-String -SimpleMatch `
            "CALYSTO_POPULATION_SCENARIO scenario=$scenario")
        $pcgLines = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: PCGComplete world=')
        $navLines = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: NavigationPathReady world=')
        $enemyLevelLines = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: EnemyLevelsReady world=')
        $populationLines = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: PopulationRealized world=')
        $companionLines = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: CompanionRosterReady world=')
        $verifiedCompanionLines = @(if ($scenario -like 'NPC*') {
            $lines | Select-String -SimpleMatch 'CALYSTO_V4_COMPANION_VERIFIED'
        })
        $doorLines = @($lines | Select-String -SimpleMatch `
            'LogEFProceduralPCGRuntime: DoorEnabled world=')
        if ($generateLines.Count -ne 1 -or $scenarioLines.Count -ne 1 -or
            $pcgLines.Count -ne 1 -or $navLines.Count -ne 1 -or
            $enemyLevelLines.Count -ne 1 -or
            $populationLines.Count -ne 1 -or $companionLines.Count -ne 1 -or
            $doorLines.Count -ne 1) {
            throw "Scenario $scenario did not emit exactly one complete V4 readiness sequence."
        }
        $expectedCompanionLineCount = if ($scenario -eq 'NPCTotal4') {
            4
        } elseif ($expectedCompanionVerification.Contains($scenario)) {
            1
        } else {
            0
        }
        if ($verifiedCompanionLines.Count -ne $expectedCompanionLineCount) {
            throw "Scenario $scenario emitted $($verifiedCompanionLines.Count) verified companion adapter records; expected $expectedCompanionLineCount."
        }
        $companionVerification = $null
        if ($scenario -eq 'NPCTotal4') {
            $companionVerification = @(
                foreach ($verifiedLine in $verifiedCompanionLines) {
                    $verificationMatch = [regex]::Match(
                        [string]$verifiedLine.Line,
                        $companionVerificationPattern,
                        [Text.RegularExpressions.RegexOptions]::CultureInvariant)
                    if (-not $verificationMatch.Success) {
                        throw "Scenario $scenario emitted a malformed CALYSTO_V4_COMPANION_VERIFIED record: $($verifiedLine.Line)"
                    }
                    $record = [ordered]@{
                        instance = $verificationMatch.Groups['instance'].Value
                        hook = $verificationMatch.Groups['hook'].Value
                        roster_projection = $verificationMatch.Groups['rosterProjection'].Value
                        catalog = $verificationMatch.Groups['catalog'].Value
                        class = $verificationMatch.Groups['class'].Value
                        controller = $verificationMatch.Groups['controller'].Value
                        stats_table = $verificationMatch.Groups['statsTable'].Value
                        stats_row = $verificationMatch.Groups['statsRow'].Value
                        repair_used = $verificationMatch.Groups['repairUsed'].Value
                        logical = [int]$verificationMatch.Groups['logical'].Value
                        physical = [int]$verificationMatch.Groups['physical'].Value
                        team = $verificationMatch.Groups['team'].Value
                        perception = $verificationMatch.Groups['perception'].Value
                        social = $verificationMatch.Groups['social'].Value
                    }
                    if ([string]::IsNullOrWhiteSpace($record.instance) -or
                        $record.instance -ieq 'None' -or
                        $record.hook -cne 'true' -or
                        $record.roster_projection -cne 'false' -or
                        $record.controller -notmatch '(?i)ACF\S*Controller' -or
                        [string]::IsNullOrWhiteSpace($record.stats_table) -or
                        $record.stats_table -ieq 'None' -or
                        [string]::IsNullOrWhiteSpace($record.stats_row) -or
                        $record.stats_row -ieq 'None' -or
                        $record.logical -lt 1 -or
                        $record.physical -ne [Math]::Min($record.logical, 100) -or
                        $record.team -cne 'true' -or
                        $record.perception -cne 'true' -or
                        $record.social -cne 'true') {
                        throw "Scenario $scenario emitted an invalid local Recruitable companion adapter record: $($verifiedLine.Line)"
                    }
                    $record
                }
            )
            $verifiedInstanceIds = @($companionVerification | ForEach-Object { $_.instance })
            if (@($verifiedInstanceIds | Select-Object -Unique).Count -ne 4) {
                throw "Scenario $scenario did not verify four unique local companion instance IDs."
            }
        }
        elseif ($scenario -like 'NPC*') {
            if (-not $expectedCompanionVerification.Contains($scenario)) {
                throw "Scenario $scenario has no exact companion verification contract."
            }
            $verificationMatch = [regex]::Match(
                [string]$verifiedCompanionLines[0].Line,
                $companionVerificationPattern,
                [Text.RegularExpressions.RegexOptions]::CultureInvariant)
            if (-not $verificationMatch.Success) {
                throw "Scenario $scenario emitted a malformed CALYSTO_V4_COMPANION_VERIFIED record: $($verifiedCompanionLines[0].Line)"
            }

            $expectedCompanion = $expectedCompanionVerification[$scenario]
            $companionVerification = [ordered]@{
                instance = $verificationMatch.Groups['instance'].Value
                hook = $verificationMatch.Groups['hook'].Value
                roster_projection = $verificationMatch.Groups['rosterProjection'].Value
                catalog = $verificationMatch.Groups['catalog'].Value
                class = $verificationMatch.Groups['class'].Value
                controller = $verificationMatch.Groups['controller'].Value
                stats_table = $verificationMatch.Groups['statsTable'].Value
                stats_row = $verificationMatch.Groups['statsRow'].Value
                repair_used = $verificationMatch.Groups['repairUsed'].Value
                logical = [int]$verificationMatch.Groups['logical'].Value
                physical = [int]$verificationMatch.Groups['physical'].Value
                team = $verificationMatch.Groups['team'].Value
                perception = $verificationMatch.Groups['perception'].Value
                social = $verificationMatch.Groups['social'].Value
            }
            if ([string]::IsNullOrWhiteSpace($companionVerification.instance) -or
                $companionVerification.instance -ieq 'None') {
                throw "Scenario $scenario emitted an invalid companion instance ID."
            }
            if ($companionVerification.catalog -cne [string]$expectedCompanion['catalog'] -or
                $companionVerification.class -cne [string]$expectedCompanion['class']) {
                throw "Scenario $scenario verified catalog/class '$($companionVerification.catalog)'/'$($companionVerification.class)'; expected '$($expectedCompanion['catalog'])'/'$($expectedCompanion['class'])'."
            }
            if ($companionVerification.hook -cne 'true' -or
                $companionVerification.roster_projection -cne 'false') {
                throw "Scenario $scenario did not realize one unrecruited NPC with hook=true and roster_projection=false."
            }
            if ($companionVerification.controller -notmatch '(?i)ACF\S*Controller') {
                throw "Scenario $scenario did not verify an ACF controller: $($companionVerification.controller)"
            }
            if ([string]::IsNullOrWhiteSpace($companionVerification.stats_table) -or
                $companionVerification.stats_table -ieq 'None' -or
                [string]::IsNullOrWhiteSpace($companionVerification.stats_row) -or
                $companionVerification.stats_row -ieq 'None') {
                throw "Scenario $scenario did not realize a valid ACF statistics table/row."
            }
            if ($scenario -like 'NPCRanged*' -and
                ($companionVerification.stats_table -cne $statisticsRepairTable -or
                 $companionVerification.stats_row -cne $statisticsRepairRow)) {
                throw "Scenario $scenario did not realize the required Ranged statistics repair table/row."
            }
            if ($companionVerification.logical -lt 1 -or
                $companionVerification.physical -ne [Math]::Min($companionVerification.logical, 100)) {
                throw "Scenario $scenario violated the logical/physical companion level contract."
            }
            foreach ($readyField in @('team', 'perception', 'social')) {
                if ($companionVerification[$readyField] -cne 'true') {
                    throw "Scenario $scenario did not verify companion field '$readyField'."
                }
            }
        }
        $pcg = $pcgLines[0].LineNumber
        $nav = $navLines[0].LineNumber
        $enemyLevels = $enemyLevelLines[0].LineNumber
        $population = $populationLines[0].LineNumber
        $companion = $companionLines[0].LineNumber
        $door = $doorLines[0].LineNumber
        if (-not ($pcg -le $nav -and $nav -le $enemyLevels -and
            $enemyLevels -le $population -and
            $population -le $companion -and $companion -le $door)) {
            throw "Scenario $scenario violated PCG <= Nav <= EnemyLevels <= Population <= Companion <= Door."
        }

        $blockedPatterns = @(
            'Blueprint Runtime Error', 'LogBlueprint: Error', 'Accessed None',
            'Ensure condition failed', 'Fatal error:', 'Assertion failed:',
            'Object Transform', 'GetAttributeFromPointIndex_0',
            'Calysto controlled generation failed', 'Calysto generation failed',
            'duplicate generation', 'duplicated generation',
            'LogEFCalystoDungeon: Error', 'LogEFCalystoPopulationV4: Error',
            'LogEFProceduralPCGRuntime: Error'
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
            authored_policy_hash = [string]$receipt.policy.policy_hash
            scenario_policy_hash = [string]$receipt.realized.hashes.policy
            manifest_hash = [string]$receipt.realized.hashes.manifest
            companion_verification = $companionVerification
            ordering = [ordered]@{
                pcg_complete_line = $pcg
                navigation_ready_line = $nav
                population_realized_line = $population
                companion_roster_ready_line = $companion
                door_enabled_line = $door
            }
        }
        Write-Host "Dungeon Director V4 extreme scenario $scenario`: PASS"
    }
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V4_EXTREME_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V4_EXTREME_SCENARIO = $oldScenario
    $env:CODEX_CALYSTO_V4_EXTREME_SEED = $oldSeed
    Assert-DazEditorReceipt -Phase 'final'
}

$summary = [ordered]@{
    schema_version = 4
    generator_version = 4
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'PASS'
    matrix = $Matrix
    run_seed = $RunSeed
    scenarios = $receipts
}
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
[IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 12),
    [Text.UTF8Encoding]::new($false))
Write-Host "Dungeon Director V4 $Matrix population materialization matrix: PASS"
Write-Host "Evidence: $summaryPath"
