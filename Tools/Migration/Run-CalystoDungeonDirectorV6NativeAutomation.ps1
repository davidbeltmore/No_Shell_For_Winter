[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [ValidateRange(1, 128)]
    [int]$ExpectedTestCount = 39
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-ContentState {
    param([Parameter(Mandatory = $true)][string]$ContentRoot)
    $state = @{}
    foreach ($file in Get-ChildItem -LiteralPath $ContentRoot -Recurse -File | Where-Object {
        $_.Extension -in @('.uasset', '.umap', '.uexp', '.ubulk', '.uptnl')
    }) {
        $relative = $file.FullName.Substring($ContentRoot.Length).TrimStart('\').Replace('\', '/')
        $state[$relative] = "$($file.Length):$($file.LastWriteTimeUtc.Ticks)"
    }
    return $state
}

function Compare-ContentState {
    param([hashtable]$Before, [hashtable]$After)
    return @(
        foreach ($path in @($Before.Keys + $After.Keys | Sort-Object -Unique)) {
            if ($Before[$path] -cne $After[$path]) { $path }
        }
    )
}

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$expectedRoot = [IO.Path]::GetFullPath('D:\Projects UE5\NoShellForWinter')
if (-not $root.Equals($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing V6 Automation outside the writable target: $root"
}
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$project = Join-Path $root 'NoShellForWinter.uproject'
$content = Join-Path $root 'Content'
foreach ($required in @($launcher, $receiptGuard, $project)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required V6 native Automation path is missing: $required"
    }
}

$editors = @(Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue)
if ($editors.Count -ne 0) {
    throw "V6 native Automation requires zero pre-existing UnrealEditor processes: $(@($editors.Id) -join ', ')"
}

$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV6\NativeAutomation_$Stamp"
if (Test-Path -LiteralPath $runRoot) {
    throw "Refusing to reuse V6 native Automation evidence: $runRoot"
}
$reportRoot = Join-Path $runRoot 'Report'
$log = Join-Path $runRoot 'CalystoDungeonDirectorV6Native.log'
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
[void][IO.Directory]::CreateDirectory($runRoot)

$prefix = 'NoShellForWinter.CalystoDungeon.V6'
$expectedTestPaths = @(
    'NoShellForWinter.CalystoDungeon.V6.Companions.SameFloorRevivalFreeze',
    'NoShellForWinter.CalystoDungeon.V6.Companions.SnapshotCanonicalHash',
    'NoShellForWinter.CalystoDungeon.V6.Companions.SnapshotFailClosed',
    'NoShellForWinter.CalystoDungeon.V6.Editor.PolicyDetailsSnapshot',
    'NoShellForWinter.CalystoDungeon.V6.GameplayDebug.DungeonHarness.NullWorldSafe',
    'NoShellForWinter.CalystoDungeon.V6.GameplayDebug.DungeonHarness.OutcomeNormalization',
    'NoShellForWinter.CalystoDungeon.V6.GameplayDebug.DungeonHarness.PersistentClassification',
    'NoShellForWinter.CalystoDungeon.V6.PCG.DecalPlacementIsDeterministicAndThemeScoped',
    'NoShellForWinter.CalystoDungeon.V6.PCG.DecalPlacementProtectsEveryReservedRoomRole',
    'NoShellForWinter.CalystoDungeon.V6.PCG.DoorClearanceGeometryIsDeterministicAndFailsClosed',
    'NoShellForWinter.CalystoDungeon.V6.PCG.CookedResidentClosureSignature',
    'NoShellForWinter.CalystoDungeon.V6.PCG.InlineArchitectureBridge',
    'NoShellForWinter.CalystoDungeon.V6.PCG.ProbabilityLanesAreIndependentAndOrderStable',
    'NoShellForWinter.CalystoDungeon.V6.PCG.ProtectedRoomResolutionFailsClosedOnExactTie',
    'NoShellForWinter.CalystoDungeon.V6.PCG.RoomIdentityIgnoresDungeonWorldTranslation',
    'NoShellForWinter.CalystoDungeon.V6.PCG.SynchronousCompletionReadinessOrder',
    'NoShellForWinter.CalystoDungeon.V6.PCG.VendorMainSideTopologySignature',
    'NoShellForWinter.CalystoDungeon.V6.Policy.FrozenFloorAndIndependentLanes',
    'NoShellForWinter.CalystoDungeon.V6.Policy.HashesValidationAndCookClosure',
    'NoShellForWinter.CalystoDungeon.V6.Policy.PhasedStreamingClosure',
    'NoShellForWinter.CalystoDungeon.V6.Policy.RoomThemeStatistics100K',
    'NoShellForWinter.CalystoDungeon.V6.Policy.SingleAssetShapeAndDefaults',
    'NoShellForWinter.CalystoDungeon.V6.Policy.StableRoomManifest',
    'NoShellForWinter.CalystoDungeon.V6.Population.DeterminismAndInputOrder',
    'NoShellForWinter.CalystoDungeon.V6.Population.GlobalBudgetCaps',
    'NoShellForWinter.CalystoDungeon.V6.Population.MaterializerPlanValidation',
    'NoShellForWinter.CalystoDungeon.V6.Population.NativeFloorCandidates',
    'NoShellForWinter.CalystoDungeon.V6.Population.OverlaySemantics',
    'NoShellForWinter.CalystoDungeon.V6.Population.PlacementIsFrozenAndHashed',
    'NoShellForWinter.CalystoDungeon.V6.Population.PreloadClosureIsCanonicalAndDeduplicated',
    'NoShellForWinter.CalystoDungeon.V6.Population.ThemeCatalogIsolation',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.CanonicalHashes',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.Contracts',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.IntentDeterminism',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.InitialEcology',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.PublicContract',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.RealizedEvidence',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.Subsystem.ThemeWeightIsolation',
    'NoShellForWinter.CalystoDungeon.V6.Runtime.ValidationBounds'
)
if ($ExpectedTestCount -ne $expectedTestPaths.Count) {
    throw "ExpectedTestCount must equal the pinned definitive V6 test inventory ($($expectedTestPaths.Count))."
}
$arguments = @(
    '-unattended'
    '-nop4'
    '-nosplash'
    '-NullRHI'
    '-NoSound'
    '-DisablePlugins=BpGeneratorUltimate,MCPClientToolset,ModelContextProtocol'
    '-stdout'
    '-FullStdOutLogOutput'
    "-ExecCmds=`"Automation RunTests StartsWith:$prefix;Quit`""
    '-TestExit="Automation Test Queue Empty"'
    "-ReportExportPath=`"$reportRoot`""
    "-ABSLOG=`"$log`""
) -join ' '

$failures = [Collections.Generic.List[string]]::new()
$launcherExit = $null
$contentBefore = Get-ContentState -ContentRoot $content

& powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $root -TargetName NoShellForWinterEditor `
    -Configuration Development -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt preflight failed with exit code $LASTEXITCODE"
}
try {
    try {
        & $launcher -ProjectRoot $root -AdditionalArguments $arguments -Wait
        $launcherExit = $LASTEXITCODE
    }
    catch {
        $failures.Add("Launcher: $($_.Exception.Message)")
    }
}
finally {
    try {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
            -ProjectRoot $root -TargetName NoShellForWinterEditor `
            -Configuration Development -VerifyOnly
        if ($LASTEXITCODE -ne 0) {
            $failures.Add("Daz receipt postflight exit code: $LASTEXITCODE")
        }
    }
    catch {
        $failures.Add("Daz receipt postflight: $($_.Exception.Message)")
    }
}

$contentAfter = Get-ContentState -ContentRoot $content
$contentMutations = @(Compare-ContentState -Before $contentBefore -After $contentAfter)
if ($contentMutations.Count -ne 0) {
    $failures.Add("Native Automation mutated Content: $($contentMutations -join ', ')")
}
if ($null -eq $launcherExit -or $launcherExit -ne 0) {
    $failures.Add("Launcher exit code must be exactly 0; actual=$launcherExit")
}

$index = Join-Path $reportRoot 'index.json'
$tests = @()
$results = $null
if (-not (Test-Path -LiteralPath $index -PathType Leaf)) {
    $failures.Add("Automation report is missing: $index")
} else {
    try {
        $results = Get-Content -Raw -LiteralPath $index | ConvertFrom-Json
        $tests = @($results.Tests)
    }
    catch {
        $failures.Add("Automation report is invalid JSON: $($_.Exception.Message)")
    }
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    $failures.Add("Automation log is missing: $log")
}

$unexpected = [Collections.Generic.List[string]]::new()
if ($null -ne $results) {
    foreach ($property in @('Succeeded', 'SucceededWithWarnings', 'Failed', 'NotRun', 'InProcess', 'Tests')) {
        if ($null -eq $results.PSObject.Properties[$property]) {
            $failures.Add("Automation report is missing property '$property'.")
        }
    }
    $duplicates = @($tests | Group-Object FullTestPath | Where-Object Count -ne 1)
    if ($duplicates.Count -ne 0) {
        $failures.Add("Duplicate V6 tests: $(@($duplicates.Name) -join ', ')")
    }
    foreach ($test in $tests) {
        $path = [string]$test.FullTestPath
        $underPrefix = $path.Equals($prefix, [StringComparison]::Ordinal) -or
            $path.StartsWith("$prefix.", [StringComparison]::Ordinal)
        if (-not $underPrefix -or [string]$test.State -cne 'Success' -or
            [int]$test.Errors -ne 0 -or [int]$test.Warnings -ne 0) {
            $unexpected.Add("$path state=$($test.State) warnings=$($test.Warnings) errors=$($test.Errors)")
        }
    }
    if ($tests.Count -ne $ExpectedTestCount) {
        $failures.Add("Expected exactly $ExpectedTestCount V6 tests; observed $($tests.Count).")
    }
    $observedPaths = @($tests | ForEach-Object { [string]$_.FullTestPath } | Sort-Object)
    $pathDelta = @(Compare-Object -CaseSensitive `
        -ReferenceObject $expectedTestPaths -DifferenceObject $observedPaths)
    if ($pathDelta.Count -ne 0) {
        $failures.Add(
            "Definitive V6 test path set drifted: $($pathDelta | ForEach-Object { '{0}{1}' -f $_.SideIndicator, $_.InputObject } | Sort-Object | Join-String -Separator '; ')"
        )
    }
    if ($unexpected.Count -ne 0) {
        $failures.Add("Non-strict V6 test results: $($unexpected -join '; ')")
    }
    if ([int]$results.Succeeded -ne $ExpectedTestCount -or
        [int]$results.SucceededWithWarnings -ne 0 -or
        [int]$results.Failed -ne 0 -or [int]$results.NotRun -ne 0 -or
        [int]$results.InProcess -ne 0) {
        $failures.Add('Aggregate Automation counts violate the exact, warning-free V6 contract.')
    }
}

$logFindings = @()
if (Test-Path -LiteralPath $log -PathType Leaf) {
    $forbidden = @(
        'Blueprint Runtime Error', 'LogBlueprint: Error', 'Accessed None',
        'Ensure condition failed', 'Fatal error:', 'Assertion failed:',
        'Automation Test Failed', 'LogEFCalystoDungeon: Error',
        'LogEFCalystoPopulation: Error', 'LogEFProceduralPCGRuntime: Error'
    )
    $logFindings = @(
        foreach ($pattern in $forbidden) {
            Select-String -LiteralPath $log -SimpleMatch -Pattern $pattern |
                ForEach-Object { "[$pattern] $($_.Line.Trim())" }
        }
    )
    if ($logFindings.Count -ne 0) {
        $failures.Add("Forbidden V6 diagnostics were found in the log.")
    }
}

$summary = [ordered]@{
    receipt_schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = if ($failures.Count -eq 0) { 'PASS' } else { 'FAIL' }
    project = $project
    namespace = $prefix
    expected_test_count = $ExpectedTestCount
    observed_test_count = $tests.Count
    observed_test_paths = @($tests | ForEach-Object { [string]$_.FullTestPath } | Sort-Object)
    launcher_exit_code = $launcherExit
    content_mutations = $contentMutations
    unexpected_results = @($unexpected)
    log_findings = $logFindings
    failure_reasons = @($failures)
    report = $index
    log = $log
}
[IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 8) + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
if ($summary.status -ne 'PASS') {
    throw ($failures -join [Environment]::NewLine)
}
Write-Host "Calysto Dungeon Director V6 native Automation: PASS ($ExpectedTestCount tests)"
Write-Output $summaryPath
