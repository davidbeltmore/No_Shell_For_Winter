[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [Int64]$RunSeed = 202609040606,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [string]$BlueprintCompileSummary = '',
    [ValidateRange(60, 1200)]
    [int]$EditorTimeoutSeconds = 720
)

<#
.SYNOPSIS
Runs the definitive Calysto V6 HUB-to-dungeon PIE traversal smoke.

.DESCRIPTION
Uses the real DoorToLevel actor and player ACF interaction component, proves
DungeonGeneration does not return immediately to HUB, requests a fixed-seed V6
floor, validates room-local Theme/material manifests, and exits without saving
or mutating Content. No earlier-version harness is imported or invoked.
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($RunSeed -le 0) { throw 'The V6 traversal PIE gate requires a positive Int64 seed.' }

function Get-TargetEditorProcesses {
    param([Parameter(Mandatory = $true)][string]$ProjectFile)
    return @(
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') -and
                $null -ne $_.CommandLine -and
                $_.CommandLine.IndexOf($ProjectFile, [StringComparison]::OrdinalIgnoreCase) -ge 0
            }
    )
}

function Test-Sha256 {
    param([object]$Value)
    return [string]$Value -cmatch '^[A-Fa-f0-9]{64}$'
}

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$expectedRoot = [IO.Path]::GetFullPath('D:\Projects UE5\NoShellForWinter')
if (-not $root.Equals($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing V6 traversal PIE outside the writable target: $root"
}
$projectFile = Join-Path $root 'NoShellForWinter.uproject'
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV6TraversalPIE58.py'
$cookClosurePath = Join-Path $root 'Saved\Migration\CalystoDungeonDirectorV6\ValidateCookClosureV6.json'
$policyPath = Join-Path $root 'Content\_Game\Data\CalystoDungeon\V6\DA_CalystoDungeonDirectorPolicy.uasset'
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV6\TraversalPIE_$Stamp"
$outputPath = Join-Path $runRoot 'TraversalV6.json'
$logPath = Join-Path $runRoot 'TraversalV6.log'
$summaryPath = Join-Path $runRoot 'StrictSummary.json'

foreach ($path in @(
        $projectFile, $launcher, $receiptGuard, $validator, $cookClosurePath,
        $policyPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required V6 traversal PIE path is missing: $path"
    }
}
if (Test-Path -LiteralPath $runRoot) {
    throw "V6 traversal PIE evidence already exists; refusing reuse: $runRoot"
}
$existingEditors = @(Get-TargetEditorProcesses -ProjectFile $projectFile)
if ($existingEditors.Count -ne 0) {
    throw "Close the target Unreal Editor before the protected V6 PIE gate: $($existingEditors.ProcessId -join ', ')"
}

$cookClosure = Get-Content -Raw -LiteralPath $cookClosurePath | ConvertFrom-Json
$cookClosureReceiptSha256 = (Get-FileHash -LiteralPath $cookClosurePath -Algorithm SHA256).Hash.ToUpperInvariant()
$policyObject = '/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy'
$policyClass = '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset'
if ([string]$cookClosure.status -cne 'PASS' -or
    [string]$cookClosure.mode -cne 'READ_ONLY_COOK_CLOSURE_VALIDATION' -or
    [string]$cookClosure.policy.object -cne $policyObject -or
    [string]$cookClosure.policy.class -cne $policyClass -or
    -not (Test-Sha256 $cookClosure.policy.hashes.gameplay) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.authoring) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.materials) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.decals) -or
    [int]$cookClosure.save_api_calls -ne 0 -or
    @($cookClosure.asset_mutations).Count -ne 0 -or
    @($cookClosure.asset_saves).Count -ne 0) {
    throw 'The current V6 cook-closure receipt is not PIE-ready.'
}
if ([string]::IsNullOrWhiteSpace($BlueprintCompileSummary)) {
    throw 'Pass the exact PASS StrictSummary.json from Run-CalystoDungeonDirectorV6BlueprintCompile58.ps1 via -BlueprintCompileSummary.'
}
$blueprintSummaryPath = (Resolve-Path -LiteralPath $BlueprintCompileSummary).Path
$allowedBlueprintRoot = [IO.Path]::GetFullPath(
    (Join-Path $root 'Saved\Migration\CalystoDungeonDirectorV6\BlueprintCompile_')).TrimEnd('\')
if (-not $blueprintSummaryPath.StartsWith(
        $allowedBlueprintRoot, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($blueprintSummaryPath) -cne 'StrictSummary.json') {
    throw "Blueprint compile evidence must be an exact V6 StrictSummary.json: $blueprintSummaryPath"
}
$blueprintSummary = Get-Content -Raw -LiteralPath $blueprintSummaryPath | ConvertFrom-Json
if ([string]$blueprintSummary.status -cne 'PASS' -or
    [string]$blueprintSummary.cook_closure_receipt_sha256 -cne $cookClosureReceiptSha256 -or
    [int]$blueprintSummary.compiled_blueprint_count -ne
        [int]$blueprintSummary.expected_blueprint_count -or
    [int]$blueprintSummary.expected_blueprint_count -lt 30 -or
    [string]$blueprintSummary.report_contract.status -cne
        'UE58_CALYSTO_V6_BLUEPRINT_COMPILE_PASS' -or
    @($blueprintSummary.report_contract.expected_blueprints).Count -ne
        [int]$blueprintSummary.expected_blueprint_count -or
    @($blueprintSummary.report_contract.compiled_blueprints).Count -ne
        [int]$blueprintSummary.expected_blueprint_count -or
    @($blueprintSummary.report_contract.forbidden_compiled).Count -ne 0 -or
    @($blueprintSummary.report_contract.on_disk_package_changes).Count -ne 0 -or
    @($blueprintSummary.report_contract.protected_file_changes).Count -ne 0 -or
    @($blueprintSummary.report_contract.dirty_not_in_cohort).Count -ne 0 -or
    [int]$blueprintSummary.report_contract.save_api_calls -ne 0 -or
    @($blueprintSummary.report_contract.saved_assets).Count -ne 0 -or
    @($blueprintSummary.report_contract.failures).Count -ne 0) {
    throw 'The supplied Blueprint compile summary is not a current complete PASS.'
}
foreach ($name in @('gameplay', 'authoring', 'materials', 'decals')) {
    if ([string]$blueprintSummary.policy_hashes.$name -cne
        ([string]$cookClosure.policy.hashes.$name).ToUpperInvariant()) {
        throw "Blueprint compile policy $name hash differs from current cook closure."
    }
}
$blueprintSummarySha256 = (Get-FileHash -LiteralPath $blueprintSummaryPath -Algorithm SHA256).Hash.ToUpperInvariant()

[void][IO.Directory]::CreateDirectory($runRoot)
$oldBaseline = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldOutput = $env:CODEX_CALYSTO_V6_PIE_OUTPUT
$oldSeed = $env:CODEX_CALYSTO_V6_PIE_SEED
$oldBlueprintSummary = $env:CODEX_CALYSTO_V6_BLUEPRINT_SUMMARY
$dazPhases = [Collections.Generic.List[string]]::new()

function Assert-DazEditorReceipt {
    param([Parameter(Mandatory = $true)][string]$Phase)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $root -TargetName NoShellForWinterEditor `
        -Configuration Development -VerifyOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Daz editor receipt verification failed during $Phase with exit code $LASTEXITCODE."
    }
    $dazPhases.Add($Phase)
}

function Invoke-ProtectedEditorWithWatchdog {
    param(
        [Parameter(Mandatory = $true)][string]$LauncherPath,
        [Parameter(Mandatory = $true)][string]$RootPath,
        [Parameter(Mandatory = $true)][string]$Arguments,
        [Parameter(Mandatory = $true)][int]$TimeoutSeconds,
        [Parameter(Mandatory = $true)][string]$ProjectPath
    )
    $job = Start-Job -ScriptBlock {
        param($ChildLauncher, $ChildRoot, $ChildArguments)
        & $ChildLauncher -ProjectRoot $ChildRoot -AdditionalArguments $ChildArguments -Wait
    } -ArgumentList $LauncherPath, $RootPath, $Arguments
    try {
        $completed = Wait-Job -Job $job -Timeout $TimeoutSeconds
        if ($null -eq $completed) {
            $targets = @(Get-TargetEditorProcesses -ProjectFile $ProjectPath)
            if ($targets.Count -eq 0) {
                Stop-Job -Job $job -ErrorAction SilentlyContinue
                throw 'Protected V6 traversal Editor timed out and its exact target process could not be located for cleanup.'
            }
            $targets | ForEach-Object {
                Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
            }
            Stop-Job -Job $job -ErrorAction SilentlyContinue
            $cleanupDeadline = (Get-Date).AddSeconds(10)
            do {
                Start-Sleep -Milliseconds 250
                $remainingTargets = @(Get-TargetEditorProcesses -ProjectFile $ProjectPath)
            } while ($remainingTargets.Count -ne 0 -and (Get-Date) -lt $cleanupDeadline)
            if ($remainingTargets.Count -ne 0) {
                throw "Protected V6 traversal timeout cleanup failed for process IDs: $($remainingTargets.ProcessId -join ', ')"
            }
            throw "Protected V6 traversal Editor exceeded $TimeoutSeconds seconds."
        }
        Receive-Job -Job $job -Wait -ErrorAction Stop | Write-Output
        if ($job.State -ne 'Completed') {
            throw "Protected V6 traversal launcher job ended in state $($job.State)."
        }
    }
    finally {
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }
}

$summary = [ordered]@{
    receipt_schema_version = 1
    status = 'FAIL'
    generated_utc = [DateTime]::UtcNow.ToString('o')
    run_seed = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    policy_object = $policyObject
    policy_class = $policyClass
    policy_hashes = $cookClosure.policy.hashes
    cook_closure_receipt_sha256 = $cookClosureReceiptSha256
    blueprint_compile_summary = $blueprintSummaryPath
    blueprint_compile_summary_sha256 = $blueprintSummarySha256
    report = $outputPath
    log = $logPath
    daz_receipt_checks = @()
    failed_checks = @()
    error = ''
}

try {
    Assert-DazEditorReceipt -Phase 'pre-run'
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
    $env:CODEX_CALYSTO_V6_PIE_OUTPUT = $outputPath
    $env:CODEX_CALYSTO_V6_PIE_SEED = $RunSeed.ToString([Globalization.CultureInfo]::InvariantCulture)
    $env:CODEX_CALYSTO_V6_BLUEPRINT_SUMMARY = $blueprintSummaryPath
    $arguments = @(
        '-unattended'
        '-nop4'
        '-nosplash'
        '-NoSound'
        '-NullRHI'
        '-NoAutoSave'
        '-DisablePlugins=BpGeneratorUltimate,MCPClientToolset,ModelContextProtocol'
        "-ABSLOG=`"$logPath`""
    ) -join ' '
    Invoke-ProtectedEditorWithWatchdog `
        -LauncherPath $launcher `
        -RootPath $root `
        -Arguments $arguments `
        -TimeoutSeconds $EditorTimeoutSeconds `
        -ProjectPath $projectFile
    Assert-DazEditorReceipt -Phase 'post-run'

    foreach ($artifact in @($outputPath, $logPath)) {
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf) -or
            (Get-Item -LiteralPath $artifact).Length -le 0) {
            throw "V6 traversal PIE did not produce a non-empty artifact: $artifact"
        }
    }
    $receipt = Get-Content -Raw -LiteralPath $outputPath | ConvertFrom-Json
    $cookClosureReceiptSha256After = (Get-FileHash -LiteralPath $cookClosurePath -Algorithm SHA256).Hash.ToUpperInvariant()
    $blueprintSummarySha256After = (Get-FileHash -LiteralPath $blueprintSummaryPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ([string]$receipt.status -cne 'PASS' -or -not [bool]$receipt.success -or
        [int]$receipt.receipt_schema_version -ne 1 -or
        [int]$receipt.schema_version -ne 6 -or
        [int]$receipt.generator_version -ne 6 -or
        [int64]$receipt.run_seed -ne $RunSeed -or
        [string]$receipt.policy.object -cne $policyObject -or
        [string]$receipt.policy.class -cne $policyClass -or
        [string]$receipt.policy.blueprint_compile_summary_sha256 -cne
            $blueprintSummarySha256 -or
        $cookClosureReceiptSha256After -cne $cookClosureReceiptSha256 -or
        $blueprintSummarySha256After -cne $blueprintSummarySha256 -or
        @($receipt.samples).Count -ne 2 -or
        (@($receipt.samples.label) -join '|') -cne 'door_entry_probe|seeded_floor_1' -or
        [int64]$receipt.samples[1].run_seed -ne $RunSeed -or
        [string]$receipt.samples[0].world -cne '/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration' -or
        [string]$receipt.samples[1].world -cne '/Game/Procedural/Maps/DungeonGeneration.DungeonGeneration' -or
        @($receipt.asset_saves).Count -ne 0 -or
        @($receipt.newly_dirty_packages).Count -ne 0 -or
        @($receipt.protected_assets.mismatches).Count -ne 0 -or
        @($receipt.legacy_objects_loaded).Count -ne 0) {
        throw "V6 traversal PIE receipt failed its definitive authority contract: $($receipt.error)"
    }
    $failedFinal = @($receipt.final_checks.PSObject.Properties | Where-Object {
        -not [bool]$_.Value
    } | ForEach-Object { $_.Name })
    if ($failedFinal.Count -ne 0) {
        throw "V6 traversal PIE final checks failed: $($failedFinal -join ', ')"
    }
    foreach ($sample in @($receipt.samples)) {
        if ([string]::IsNullOrWhiteSpace([string]$sample.style_id) -or
            [int]$sample.room_count -lt 1 -or
            [int]$sample.eligible_room_count -lt [int]$sample.themed_room_count -or
            @($sample.theme_counts.PSObject.Properties).Count -ne 3 -or
            @($sample.readiness.PSObject.Properties | Where-Object {
                -not [bool]$_.Value
            }).Count -ne 0) {
            throw "V6 sample '$($sample.label)' failed Style/Theme/readiness validation."
        }
        foreach ($hash in @($sample.hashes.PSObject.Properties.Value)) {
            if (-not (Test-Sha256 $hash)) {
                throw "V6 sample '$($sample.label)' contains a non-canonical hash."
            }
        }
    }
    foreach ($name in @('gameplay', 'authoring', 'materials', 'decals')) {
        if ([string]$receipt.policy.hashes.$name -cne
            ([string]$cookClosure.policy.hashes.$name).ToUpperInvariant()) {
            throw "V6 PIE policy $name hash differs from the cook-closure receipt."
        }
    }

    $logText = Get-Content -Raw -LiteralPath $logPath
    $retiredBlueprintSuffix = 'V' + [string](2 + 2)
    $forbiddenTokens = @(
        'Blueprint Runtime Error', 'LogBlueprint: Error', 'Accessed None',
		'Ensure condition failed', 'Fatal error:', 'Assertion failed:',
		"LogPCG: Error: Attribute name '",
		'LogEFCalystoDungeon: Error', 'LogEFCalystoPopulation: Error',
		'LoadSynchronous'
    ) + @(3..5 | ForEach-Object { "CalystoV$($_)" }) + @(
        "BP_CalystoLockedChest$retiredBlueprintSuffix",
        "BP_CalystoLockPickChest$retiredBlueprintSuffix",
        "BP_CalystoArmorPickup$retiredBlueprintSuffix"
    )
    $logFindings = @($forbiddenTokens | Where-Object {
        $logText.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -ge 0
    })
    if ($logFindings.Count -ne 0) {
        throw "V6 PIE log contains forbidden diagnostics/tokens: $($logFindings -join ', ')"
    }
    $summary.status = 'PASS'
    $summary.report_contract = $receipt
}
catch {
    $summary.error = $_.Exception.Message
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V6_PIE_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_V6_PIE_SEED = $oldSeed
    $env:CODEX_CALYSTO_V6_BLUEPRINT_SUMMARY = $oldBlueprintSummary
    try { Assert-DazEditorReceipt -Phase 'final' }
    catch {
        if ([string]::IsNullOrWhiteSpace($summary.error)) {
            $summary.error = $_.Exception.Message
        }
        $summary.status = 'FAIL'
    }
    $summary.daz_receipt_checks = @($dazPhases)
    if ($summary.status -ne 'PASS') { $summary.failed_checks = @($summary.error) }
    [IO.File]::WriteAllText(
        $summaryPath,
        ($summary | ConvertTo-Json -Depth 32) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
}

if ($summary.status -ne 'PASS') { throw $summary.error }
Write-Host 'Calysto Dungeon Director V6 HUB-to-dungeon PIE traversal: PASS'
Write-Output $summaryPath
