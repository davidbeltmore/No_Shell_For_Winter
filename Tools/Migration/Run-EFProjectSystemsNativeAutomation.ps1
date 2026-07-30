[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EditorCmd = 'D:\Unreal Engine 5\Library\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe',
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$editor = (Resolve-Path -LiteralPath $EditorCmd).Path
$project = Join-Path $root 'NoShellForWinter.uproject'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
    throw "Project descriptor is missing: $project"
}

$report = Join-Path $root "Saved\Migration\Automation\Phase3_EFProjectSystems_Native_$Stamp"
$log = Join-Path $root "Saved\Migration\Logs\Phase3_EFProjectSystems_Native_$Stamp.log"

# These 71 tests are native/transient-object tests. Asset-fallback, content-hard,
# and PIE tests are deliberately excluded until their allowlisted content exists.
$filters = @(
    'StartsWith:Project.RuntimePerformance'
    'StartsWith:ACFUltimateSample.DungeonCurse'
    'StartsWith:ACFUltimateSample.Enemies.Leveling'
    'StartsWith:ACFUltimateSample.Enemies.VisualVariation'
    'StartsWith:ACFUltimateSample.Intimacy.CombatShield'
    'StartsWith:ACFUltimateSample.SinfulAscension.Progression'
    'StartsWith:ACFUltimateSample.SinfulAscension.Cunning'
    'StartsWith:ACFUltimateSample.SinfulAscension.Celerity'
    'StartsWith:ACFUltimateSample.SinfulAscension.Faith'
    'StartsWith:ACFUltimateSample.SinfulAscension.General'
    'StartsWith:ACFUltimateSample.SinfulAscension.DynamicMaximums'
    'StartsWith:ACFUltimateSample.SinfulAscension.Background'
    '^ACFUltimateSample.GameplayDebug.Commands.RestoreHealth$'
    '^ACFUltimateSample.GameplayDebug.Commands.RestoreNeedsAndSensations$'
    '^ACFUltimateSample.GameplayDebug.Commands.SetNeedsSensationsPercent$'
    '^ACFUltimateSample.GameplayDebug.Commands.RaiseAttributeTargets$'
    '^ACFUltimateSample.Intimacy.LustScaling$'
    '^ACFUltimateSample.Intimacy.AllureDrainAndPlease$'
    '^ACFUltimateSample.Intimacy.Climax$'
    '^ACFUltimateSample.Intimacy.TalkRecruitVisibility$'
    '^ACFUltimateSample.Intimacy.ControlState$'
    '^ACFUltimateSample.Intimacy.RelationshipTags$'
    '^ACFUltimateSample.Intimacy.TalkTags$'
    '^ACFUltimateSample.Intimacy.SocialCardRows$'
    '^ACFUltimateSample.SinfulAscension.Willpower.EnduranceMaxTable$'
    '^ACFUltimateSample.SinfulAscension.Willpower.LustMaxStack$'
    '^ACFUltimateSample.SinfulAscension.Willpower.SecondBreath$'
    '^ACFUltimateSample.SinfulAscension.Defensive.FlatDamageNegation$'
    '^ACFUltimateSample.SinfulAscension.Defensive.PainOverflowAndReflectionMath$'
    '^ACFUltimateSample.SinfulAscension.Defensive.PainReflectionAccumulation$'
    '^ACFUltimateSample.Defeat.Pain.AppliedDamageScalar$'
    '^ACFUltimateSample.CharacterBackground.Data.InvalidAttributeId$'
    '^ACFUltimateSample.Survival.Status.DataTableOverridesDefinitions$'
    '^ACFUltimateSample.Survival.Status.MissingDataTableUsesFallback$'
    '^ACFUltimateSample.Survival.Status.VisibleTopFiveAndOverflow$'
    '^ACFUltimateSample.Defeat.Flow.CombatSessionPersistsWithNearbyEnemies$'
    '^ACFUltimateSample.Defeat.Flow.CombatSessionEndsWithoutNearbyEnemies$'
    '^ACFUltimateSample.Defeat.Flow.RepeatKnockoutDirectDefeat$'
    '^ACFUltimateSample.Defeat.Flow.MinimumStruggleNotes$'
) -join '+'

$arguments = @(
    $project
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    '-stdout'
    '-FullStdOutLogOutput'
    "-ExecCmds=Automation RunTests $filters;Quit"
    '-TestExit=Automation Test Queue Empty'
    "-ReportExportPath=$report"
    "-ABSLOG=$log"
)

& $editor @arguments
$exitCode = $LASTEXITCODE

$indexPath = Join-Path $report 'index.json'
if (-not (Test-Path -LiteralPath $indexPath -PathType Leaf)) {
    throw "Automation report is missing after exit code ${exitCode}: $indexPath"
}

$results = Get-Content -Raw -LiteralPath $indexPath | ConvertFrom-Json
$summary = [pscustomobject]@{
    ExitCode = $exitCode
    Succeeded = [int]$results.Succeeded
    SucceededWithWarnings = [int]$results.SucceededWithWarnings
    Failed = [int]$results.Failed
    NotRun = [int]$results.NotRun
    Report = $indexPath
    Log = $log
}
$summary | Format-List

if (
    $exitCode -ne 0 -or
    $summary.Succeeded -ne 71 -or
    $summary.SucceededWithWarnings -ne 0 -or
    $summary.Failed -ne 0 -or
    $summary.NotRun -ne 0
) {
    throw 'EFProjectSystems strict native Automation gate failed.'
}
