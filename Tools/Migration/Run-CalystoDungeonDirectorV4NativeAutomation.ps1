[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [string]$TestFilter = '',
    [ValidateRange(1, 64)]
    [int]$ExpectedTestCount = 23
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$project = Join-Path $root 'NoShellForWinter.uproject'
foreach ($requiredPath in @($launcher, $receiptGuard, $project)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required Dungeon Director V4 native Automation path is missing: $requiredPath"
    }
}

$evidenceRoot = Join-Path $root 'Saved\Migration\CalystoDungeonDirectorV4'
$runRoot = Join-Path $evidenceRoot "NativeAutomation_$Stamp"
$report = Join-Path $runRoot 'Report'
$log = Join-Path $runRoot 'CalystoDungeonDirectorV4Native.log'
$summaryPath = Join-Path $runRoot 'StrictSummary.json'

# A reused report could make a failed or empty run look successful. Every strict
# invocation therefore owns a new, initially empty evidence directory.
if (Test-Path -LiteralPath $runRoot) {
    throw "Dungeon Director V4 native Automation evidence path already exists: $runRoot"
}
[void][IO.Directory]::CreateDirectory($runRoot)

$filters = if ([string]::IsNullOrWhiteSpace($TestFilter)) {
    @(
        'StartsWith:NoShellForWinter.CalystoDungeon.V4'
        'StartsWith:NoShellForWinter.GameplayDebug.DungeonHarness.V4'
        'StartsWith:NoShellForWinter.Enemies.Leveling'
    ) -join '+'
} else {
    $TestFilter
}
$allowedTestPrefixes = @(
    'NoShellForWinter.CalystoDungeon.V4'
    'NoShellForWinter.GameplayDebug.DungeonHarness.V4'
    'NoShellForWinter.Enemies.Leveling'
)

function Assert-DazEditorReceipt {
    param([Parameter(Mandatory = $true)][string]$Phase)

    & powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $root `
        -TargetName 'NoShellForWinterEditor' `
        -Configuration 'Development' `
        -VerifyOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Daz receipt verification failed during $Phase with exit code $LASTEXITCODE."
    }
}

$arguments = @(
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    # The editor-only Marketplace assistant performs unrelated Supabase/Internet
    # health checks.  Keep it out of deterministic native Automation so an
    # external network timeout cannot be attributed to whichever V4 test happens
    # to be active.  This does not alter any cooked/runtime plugin contract.
    '-DisablePlugins=BpGeneratorUltimate,MCPClientToolset,ModelContextProtocol'
    '-stdout'
    '-FullStdOutLogOutput'
    "-ExecCmds=`"Automation RunTests $filters;Quit`""
    '-TestExit="Automation Test Queue Empty"'
    "-ReportExportPath=`"$report`""
    "-ABSLOG=`"$log`""
) -join ' '

Assert-DazEditorReceipt -Phase 'pre-run'
$launcherFailure = $null
$postRunReceiptFailure = $null
try {
    try {
        & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
        if ($LASTEXITCODE -ne 0) {
            $launcherFailure = "Protected launcher failed with exit code $LASTEXITCODE."
        }
    }
    catch {
        # Unreal returns a non-zero process code when an Automation test fails.
        # Preserve that fact, but still parse and retain the generated report.
        $launcherFailure = $_.Exception.Message
    }
}
finally {
    # The protected launch contract must still be true after Unreal exits, even
    # when the editor or Automation command itself fails.
    try {
        Assert-DazEditorReceipt -Phase 'post-run'
    }
    catch {
        $postRunReceiptFailure = $_.Exception.Message
    }
}

$indexPath = Join-Path $report 'index.json'
foreach ($artifactPath in @($indexPath, $log)) {
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
        throw "Dungeon Director V4 native Automation artifact is missing: $artifactPath"
    }
    if ((Get-Item -LiteralPath $artifactPath).Length -le 0) {
        throw "Dungeon Director V4 native Automation artifact is empty: $artifactPath"
    }
}

try {
    $results = Get-Content -Raw -LiteralPath $indexPath | ConvertFrom-Json
}
catch {
    throw "Dungeon Director V4 Automation report is not valid JSON: $($_.Exception.Message)"
}

foreach ($requiredProperty in @(
    'Succeeded',
    'SucceededWithWarnings',
    'Failed',
    'NotRun',
    'InProcess',
    'Tests'
)) {
    if ($null -eq $results.PSObject.Properties[$requiredProperty]) {
        throw "Dungeon Director V4 Automation report is missing property '$requiredProperty'."
    }
}

$tests = @($results.Tests)
$duplicatePaths = @(
    $tests |
        Group-Object -Property FullTestPath |
        Where-Object { $_.Count -ne 1 } |
        ForEach-Object { $_.Name }
)
if ($duplicatePaths.Count -ne 0) {
    throw "Dungeon Director V4 report contains duplicated tests: $($duplicatePaths -join ', ')"
}

$acceptedConnectivityWarning =
    'LogHttp: HTTP request timed out after 3.00 seconds URL=https://www.google.com/generate_204'
$acceptedEnvironmentalWarnings = [Collections.Generic.List[string]]::new()
$strictPassingTestCount = 0
$unexpectedTests = @(
    foreach ($test in $tests) {
        foreach ($requiredTestProperty in @(
            'FullTestPath',
            'State',
            'Warnings',
            'Errors'
        )) {
            if ($null -eq $test.PSObject.Properties[$requiredTestProperty]) {
                throw "Dungeon Director V4 report test is missing property '$requiredTestProperty'."
            }
        }

        $testPath = [string]$test.FullTestPath
        $hasAllowedPrefix = $false
        foreach ($allowedPrefix in $allowedTestPrefixes) {
            if ($testPath.StartsWith($allowedPrefix, [StringComparison]::Ordinal)) {
                $hasAllowedPrefix = $true
                break
            }
        }

        # CEF performs this exact external connectivity probe independently of
        # the active test.  It is accepted only when every warning attached to
        # the otherwise-successful test is this byte-exact message.  Any other
        # warning remains a strict failure.
        $warningMessages = @(
            foreach ($entry in @($test.Entries)) {
                if ($null -ne $entry.Event -and
                    [string]$entry.Event.Type -eq 'Warning') {
                    [string]$entry.Event.Message
                }
            }
        )
        $warningsAreAccepted = [int]$test.Warnings -gt 0 -and
            $warningMessages.Count -eq [int]$test.Warnings -and
            @($warningMessages | Where-Object {
                $_ -cne $acceptedConnectivityWarning
            }).Count -eq 0
        if ($warningsAreAccepted) {
            foreach ($message in $warningMessages) {
                $acceptedEnvironmentalWarnings.Add("$testPath :: $message")
            }
        }

        $strictPass = $hasAllowedPrefix -and
            [string]$test.State -eq 'Success' -and
            [int]$test.Errors -eq 0 -and
            ([int]$test.Warnings -eq 0 -or $warningsAreAccepted)
        if ($strictPass) {
            ++$strictPassingTestCount
        }

        if (-not $strictPass) {
            $testPath
        }
    }
)

$summary = [ordered]@{
    SchemaVersion = 4
    GeneratorVersion = 4
    Status = 'PENDING'
    Succeeded = [int]$results.Succeeded
    SucceededWithWarnings = [int]$results.SucceededWithWarnings
    Failed = [int]$results.Failed
    NotRun = [int]$results.NotRun
    InProcess = [int]$results.InProcess
    TestCount = $tests.Count
    StrictPassed = $strictPassingTestCount
    AcceptedEnvironmentalWarnings = @($acceptedEnvironmentalWarnings)
    Report = $indexPath
    Log = $log
    LauncherFailure = $launcherFailure
    PostRunReceiptFailure = $postRunReceiptFailure
    UnexpectedTests = @($unexpectedTests)
}

$failureReasons = [Collections.Generic.List[string]]::new()
if ($launcherFailure) {
    $failureReasons.Add("Launcher: $launcherFailure")
}
if ($postRunReceiptFailure) {
    $failureReasons.Add("Daz post-run receipt: $postRunReceiptFailure")
}
if (
    $summary.StrictPassed -ne $expectedTestCount -or
    ($summary.Succeeded + $summary.SucceededWithWarnings) -ne $expectedTestCount -or
    $summary.Failed -ne 0 -or
    $summary.NotRun -ne 0 -or
    $summary.InProcess -ne 0 -or
    $summary.TestCount -ne $expectedTestCount -or
    $unexpectedTests.Count -ne 0
) {
    $unexpectedText = if ($unexpectedTests.Count -eq 0) {
        '<none>'
    }
    else {
        $unexpectedTests -join ', '
    }
    $failureReasons.Add(
        "Dungeon Director V4 strict native Automation gate failed; " +
        "expected exactly $expectedTestCount strict PASS results. " +
        "Unexpected tests: $unexpectedText"
    )
}

$forbiddenLogText = @(
    'Blueprint Runtime Error',
    'LogBlueprint: Error',
    'Accessed None',
    'Ensure condition failed',
    'Fatal error:',
    'Assertion failed:',
    'Object Transform',
    'GetAttributeFromPointIndex_0',
    'duplicate generation',
    'duplicated generation',
    'generation duplicated',
    'Calysto controlled generation failed',
    'Calysto generation failed',
    'Automation Test Failed',
    'LogEFCalystoDungeon: Error',
    'LogEFCalystoPopulation: Error',
    'LogEFProceduralPCGRuntime: Error'
)
$logFindings = @(
    foreach ($pattern in $forbiddenLogText) {
        Select-String -LiteralPath $log -SimpleMatch -Pattern $pattern |
            ForEach-Object { "[$pattern] $($_.Line.Trim())" }
    }
)
if ($logFindings.Count -ne 0) {
    $failureReasons.Add(
        "Dungeon Director V4 Automation log contains forbidden diagnostics:`n" +
        ($logFindings -join [Environment]::NewLine))
}

$summary.LogFindings = @($logFindings)
$summary.FailureReasons = @($failureReasons)
$summary.Status = if ($failureReasons.Count -eq 0) { 'PASS' } else { 'FAIL' }

[IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 4),
    [Text.UTF8Encoding]::new($false))
$summary | Format-List
if ($summary.Status -ne 'PASS') {
    throw ($failureReasons -join [Environment]::NewLine)
}
Write-Host 'Dungeon Director V4 native Automation: PASS'
