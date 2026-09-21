[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [ValidateRange(60, 1200)]
    [int]$EditorTimeoutSeconds = 720
)

<#
.SYNOPSIS
Compiles the exact project-owned Calysto V6 Blueprint cohort without saving.

.DESCRIPTION
The cohort is DoorToLevel plus every project-owned Blueprint derived from the
definitive V6 policy cook bundle. The known 29-Blueprint baseline must remain a
subset, but newly authored bundle Blueprints are included automatically. Vendor
/Game/Calysto assets are forbidden. The Python gate proves exact native parents
for the three unversioned runtime Blueprints and hashes every monitored package
before and after compilation.
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
    throw "Refusing V6 Blueprint validation outside the writable target: $root"
}

$projectFile = Join-Path $root 'NoShellForWinter.uproject'
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$receiptGuard = Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$validator = Join-Path $root 'Tools\Migration\Validate-CalystoDungeonDirectorV6Blueprints58.py'
$cookClosurePath = Join-Path $root 'Saved\Migration\CalystoDungeonDirectorV6\ValidateCookClosureV6.json'
$runtimeAssetsPath = Join-Path $root 'Saved\Migration\CalystoDungeonDirectorV6\CreateRuntimeAssetsV6.json'
$policyPath = Join-Path $root 'Content\_Game\Data\CalystoDungeon\V6\DA_CalystoDungeonDirectorPolicy.uasset'
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV6\BlueprintCompile_$Stamp"
$reportPath = Join-Path $runRoot 'BlueprintCompile.json'
$summaryPath = Join-Path $runRoot 'StrictSummary.json'
$logPath = Join-Path $runRoot 'BlueprintCompile.log'

foreach ($path in @(
        $projectFile, $launcher, $receiptGuard, $validator, $cookClosurePath,
        $runtimeAssetsPath, $policyPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required V6 Blueprint gate path is missing: $path"
    }
}
if (Test-Path -LiteralPath $runRoot) {
    throw "V6 Blueprint evidence already exists; refusing reuse: $runRoot"
}
$existingEditors = @(Get-TargetEditorProcesses -ProjectFile $projectFile)
if ($existingEditors.Count -ne 0) {
    throw "Close the target Unreal Editor before the no-save V6 Blueprint gate: $($existingEditors.ProcessId -join ', ')"
}

try {
    $cookClosure = Get-Content -Raw -LiteralPath $cookClosurePath | ConvertFrom-Json
    $runtimeAssets = Get-Content -Raw -LiteralPath $runtimeAssetsPath | ConvertFrom-Json
}
catch {
    throw "A required V6 authoring receipt is not valid JSON: $($_.Exception.Message)"
}
$expectedPolicyObject = '/Game/_Game/Data/CalystoDungeon/V6/DA_CalystoDungeonDirectorPolicy.DA_CalystoDungeonDirectorPolicy'
$expectedPolicyClass = '/Script/EFProceduralRuntime.EFCalystoDungeonDirectorPolicyV6Asset'
if ([string]$cookClosure.status -cne 'PASS' -or
    [string]$cookClosure.mode -cne 'READ_ONLY_COOK_CLOSURE_VALIDATION' -or
    [string]$cookClosure.policy.object -cne $expectedPolicyObject -or
    [string]$cookClosure.policy.class -cne $expectedPolicyClass -or
    -not (Test-Sha256 $cookClosure.policy.hashes.gameplay) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.authoring) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.materials) -or
    -not (Test-Sha256 $cookClosure.policy.hashes.decals) -or
    @($cookClosure.policy.exact_reference_validation.PSObject.Properties).Count -lt 1 -or
    [int]$cookClosure.save_api_calls -ne 0 -or
    @($cookClosure.asset_mutations).Count -ne 0 -or
    @($cookClosure.asset_saves).Count -ne 0) {
    throw 'The current V6 cook-closure receipt is not a clean definitive authority.'
}
if ([string]$runtimeAssets.status -cne 'PASS' -or
    [string]$runtimeAssets.mode -cne 'VALIDATE_EXISTING_READ_ONLY' -or
    @($runtimeAssets.blueprints).Count -ne 3 -or
    @($runtimeAssets.blueprints.package | Sort-Object -Unique).Count -ne 3 -or
    @($runtimeAssets.created).Count -ne 0 -or
    @($runtimeAssets.saved).Count -ne 0 -or
    @($runtimeAssets.asset_mutations).Count -ne 0 -or
    @($runtimeAssets.asset_mutations_final).Count -ne 0 -or
    [string]::IsNullOrWhiteSpace([string]$runtimeAssets.append_only_receipt)) {
    throw 'The definitive V6 runtime-asset receipt is missing or incomplete.'
}
$cookClosureReceiptSha256 = (Get-FileHash -LiteralPath $cookClosurePath -Algorithm SHA256).Hash.ToUpperInvariant()
$runtimeAssetsReceiptSha256 = (Get-FileHash -LiteralPath $runtimeAssetsPath -Algorithm SHA256).Hash.ToUpperInvariant()

[void][IO.Directory]::CreateDirectory($runRoot)
$oldBaseline = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldReport = $env:CODEX_CALYSTO_V6_BLUEPRINT_REPORT
$dazPhases = [Collections.Generic.List[string]]::new()

function Assert-DazEditorReceipt {
    param([Parameter(Mandatory = $true)][string]$Phase)
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $root `
        -TargetName 'NoShellForWinterEditor' `
        -Configuration 'Development' `
        -VerifyOnly
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
                throw 'Protected V6 Blueprint Editor timed out and its exact target process could not be located for cleanup.'
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
                throw "Protected V6 Blueprint timeout cleanup failed for process IDs: $($remainingTargets.ProcessId -join ', ')"
            }
            throw "Protected V6 Blueprint Editor exceeded $TimeoutSeconds seconds."
        }
        Receive-Job -Job $job -Wait -ErrorAction Stop | Write-Output
        if ($job.State -ne 'Completed') {
            throw "Protected V6 Blueprint launcher job ended in state $($job.State)."
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
    policy_object = $expectedPolicyObject
    policy_class = $expectedPolicyClass
    policy_hashes = $cookClosure.policy.hashes
    policy_gameplay_hash = ([string]$cookClosure.policy.hashes.gameplay).ToUpperInvariant()
    policy_authoring_hash = ([string]$cookClosure.policy.hashes.authoring).ToUpperInvariant()
    cook_closure_receipt = $cookClosurePath
    cook_closure_receipt_sha256 = $cookClosureReceiptSha256
    runtime_assets_receipt = $runtimeAssetsPath
    runtime_assets_receipt_sha256 = $runtimeAssetsReceiptSha256
    report = $reportPath
    log = $logPath
    expected_blueprint_count = 0
    compiled_blueprint_count = 0
    daz_receipt_checks = @()
    error = ''
}

try {
    Assert-DazEditorReceipt -Phase 'pre-run'
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $validator
    $env:CODEX_CALYSTO_V6_BLUEPRINT_REPORT = $reportPath
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

    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf) -or
        (Get-Item -LiteralPath $reportPath).Length -le 0) {
        throw "V6 Blueprint gate did not write a non-empty report: $reportPath"
    }
    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    $cookClosureReceiptSha256After = (Get-FileHash -LiteralPath $cookClosurePath -Algorithm SHA256).Hash.ToUpperInvariant()
    $runtimeAssetsReceiptSha256After = (Get-FileHash -LiteralPath $runtimeAssetsPath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ([string]$report.status -cne 'UE58_CALYSTO_V6_BLUEPRINT_COMPILE_PASS' -or
        [int]$report.baseline_bundle_blueprint_count -ne 29 -or
        [int]$report.expected_bundle_blueprint_count -lt 29 -or
        [int]$report.expected_blueprint_count -lt [int]$report.expected_bundle_blueprint_count -or
        @($report.expected_blueprints).Count -ne [int]$report.expected_blueprint_count -or
        @($report.expected_blueprints | Where-Object {
            [string]$_ -ceq '/Game/Procedural/DoorToLevel'
        }).Count -ne 1 -or
        [int]$report.compiled_blueprint_count -ne [int]$report.expected_blueprint_count -or
        @($report.compiled_blueprints).Count -ne [int]$report.expected_blueprint_count -or
        @($report.forbidden_compiled).Count -ne 0 -or
        @($report.on_disk_package_changes).Count -ne 0 -or
        @($report.protected_file_changes).Count -ne 0 -or
        @($report.dirty_not_in_cohort).Count -ne 0 -or
        @($report.legacy_runtime_packages_loaded).Count -ne 0 -or
        [int]$report.save_api_calls -ne 0 -or
        @($report.saved_assets).Count -ne 0 -or
        @($report.failures).Count -ne 0 -or
        [string]$report.cook_closure_receipt_sha256 -cne
            $cookClosureReceiptSha256 -or
        [string]$report.runtime_assets_receipt_sha256 -cne
            $runtimeAssetsReceiptSha256 -or
        $cookClosureReceiptSha256After -cne $cookClosureReceiptSha256 -or
        $runtimeAssetsReceiptSha256After -cne $runtimeAssetsReceiptSha256 -or
        [string]$report.policy_gameplay_hash -cne $summary.policy_gameplay_hash -or
        [string]$report.policy_authoring_hash -cne $summary.policy_authoring_hash -or
        [string]$report.policy_hashes.materials -cne
            ([string]$summary.policy_hashes.materials).ToUpperInvariant() -or
        [string]$report.policy_hashes.decals -cne
            ([string]$summary.policy_hashes.decals).ToUpperInvariant()) {
        throw 'V6 Blueprint report did not satisfy the exact no-save protected cohort contract.'
    }
    $forbidden = @($report.compiled_blueprints | Where-Object {
        ([string]$_.package).StartsWith('/Game/Calysto/', [StringComparison]::OrdinalIgnoreCase)
    })
    if ($forbidden.Count -ne 0) {
        throw "V6 Blueprint gate compiled forbidden vendor assets: $($forbidden.package -join ', ')"
    }
    $summary.status = 'PASS'
    $summary.compiled_blueprint_count = [int]$report.compiled_blueprint_count
    $summary.expected_blueprint_count = [int]$report.expected_blueprint_count
    $summary.report_contract = $report
}
catch {
    $summary.error = $_.Exception.Message
}
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldBaseline
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:CODEX_CALYSTO_V6_BLUEPRINT_REPORT = $oldReport
    try { Assert-DazEditorReceipt -Phase 'final' }
    catch {
        if ([string]::IsNullOrWhiteSpace($summary.error)) {
            $summary.error = $_.Exception.Message
        }
        $summary.status = 'FAIL'
    }
    $summary.daz_receipt_checks = @($dazPhases)
    [IO.File]::WriteAllText(
        $summaryPath,
        ($summary | ConvertTo-Json -Depth 32) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
}

if ($summary.status -ne 'PASS') {
    throw $summary.error
}
Write-Host 'Calysto Dungeon Director V6 protected Blueprint compile gate: PASS'
Write-Output $summaryPath
