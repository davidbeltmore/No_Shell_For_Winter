[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$OutputRoot = '',
    [ValidatePattern('^[A-Za-z0-9_.-]+$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [ValidateRange(120, 7200)]
    [int]$TimeoutSeconds = 3600
)

<#
.SYNOPSIS
Compiles every Blueprint asset under /Game and mounted project-plugin content without saving assets.

.DESCRIPTION
The wrapper uses Launch-NoShellForWinterEditor58.ps1, so the Daz receipt is
repaired before the editor starts and both required Daz plugins remain enabled.
It reuses the existing environment-gated Content/Python/init_unreal.py hook.
The Python gate writes a JSON receipt below Saved/Artifacts and exits the editor.
No source, config, plugin content, or Unreal asset is saved by this wrapper.
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function ConvertTo-SingleQuotedPowerShellLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Get-GitPorcelain {
    param([Parameter(Mandatory = $true)][string]$Root)
    $lines = @(& git -C $Root status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to capture target worktree state.'
    }
    return @($lines)
}

function Get-ProjectEditorProcesses {
    param([Parameter(Mandatory = $true)][string]$ProjectFile)
    return @(
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') -and
                $null -ne $_.CommandLine -and
                $_.CommandLine.IndexOf($ProjectFile, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
            }
    )
}

function Stop-OwnedProjectEditors {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectFile,
        [Parameter(Mandatory = $true)][datetime]$StartedAfter
    )

    $owned = @(
        Get-ProjectEditorProcesses -ProjectFile $ProjectFile |
            Where-Object {
                try {
                    (Get-Process -Id $_.ProcessId -ErrorAction Stop).StartTime -ge $StartedAfter
                }
                catch {
                    $false
                }
            }
    )
    foreach ($editor in $owned) {
        try {
            [void](Get-Process -Id $editor.ProcessId -ErrorAction Stop).CloseMainWindow()
        }
        catch {}
    }
    Start-Sleep -Seconds 5
    foreach ($editor in $owned) {
        if (Get-Process -Id $editor.ProcessId -ErrorAction SilentlyContinue) {
            Stop-Process -Id $editor.ProcessId -Force -ErrorAction SilentlyContinue
        }
    }
    return @($owned | Select-Object ProcessId, Name, CommandLine)
}

$projectRootPath = (Resolve-Path -LiteralPath $ProjectRoot).Path
$engineRootPath = (Resolve-Path -LiteralPath $EngineRoot).Path
$projectFile = Join-Path $projectRootPath 'NoShellForWinter.uproject'
$launchWrapper = Join-Path $projectRootPath 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$pythonGate = Join-Path $projectRootPath 'Tools\Migration\Test-AllProjectBlueprints58.py'
$artifactsRoot = [IO.Path]::GetFullPath((Join-Path $projectRootPath 'Saved\Artifacts')).TrimEnd('\') + '\'

foreach ($requiredPath in @($projectFile, $launchWrapper, $pythonGate)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required path is missing: $requiredPath"
    }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $runDirectory = Join-Path $artifactsRoot ("PendingValidation\BlueprintCompile\$Stamp")
}
elseif ([IO.Path]::IsPathRooted($OutputRoot)) {
    $runDirectory = [IO.Path]::GetFullPath($OutputRoot)
}
else {
    $runDirectory = [IO.Path]::GetFullPath((Join-Path $projectRootPath $OutputRoot))
}
$runDirectory = $runDirectory.TrimEnd('\')
if (-not ($runDirectory + '\').StartsWith($artifactsRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must remain below Saved\Artifacts: $runDirectory"
}

$reportPath = Join-Path $runDirectory 'AllProjectBlueprintCompile.json'
$summaryPath = Join-Path $runDirectory 'RunSummary.json'
$editorLog = Join-Path $runDirectory 'UnrealEditor_AllProjectBlueprintCompile.log'
$launcherStdout = Join-Path $runDirectory 'Launcher.stdout.log'
$launcherStderr = Join-Path $runDirectory 'Launcher.stderr.log'
$launcherExitCodePath = Join-Path $runDirectory 'Launcher.exitcode.txt'
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

$summary = [ordered]@{
    schema_version = 1
    status = 'IN_PROGRESS'
    failure = $null
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    finished_utc = $null
    project = $projectFile
    launch_wrapper = $launchWrapper
    python_gate = $pythonGate
    report = $reportPath
    run_directory = $runDirectory
    editor_log = $editorLog
    launcher_stdout = $launcherStdout
    launcher_stderr = $launcherStderr
    launcher_exit_code = $null
    gate_status = $null
    discovered_blueprint_count = $null
    compiled_blueprint_count = $null
    failure_count = $null
    on_disk_package_change_count = $null
    git_state_unchanged = $null
    forced_editor_cleanup = @()
}

$previousEnvironment = [ordered]@{}
$launcher = $null
$gitBefore = @()
$launchStarted = Get-Date

try {
    $existingEditors = @(Get-ProjectEditorProcesses -ProjectFile $projectFile)
    if ($existingEditors.Count -gt 0) {
        throw "Close the target Unreal Editor before this isolated Blueprint compile gate. PID(s): $($existingEditors.ProcessId -join ', ')"
    }

    $gitBefore = Get-GitPorcelain -Root $projectRootPath
    $environmentValues = [ordered]@{
        CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
        CODEX_MIGRATION_PIE_SCRIPT = $pythonGate
        CODEX_ALL_PROJECT_BLUEPRINT_REPORT = $reportPath
    }
    foreach ($entry in $environmentValues.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, [string]$entry.Value, 'Process')
    }

    $additionalArguments = '-NoSplash -NoP4 -Unattended -NullRHI -NoSound -NoAutoSave -stdout -FullStdOutLogOutput ' +
        ('-abslog="{0}"' -f $editorLog)
    $launchInvocation = '& ' + (ConvertTo-SingleQuotedPowerShellLiteral $launchWrapper) +
        ' -ProjectRoot ' + (ConvertTo-SingleQuotedPowerShellLiteral $projectRootPath) +
        ' -EngineRoot ' + (ConvertTo-SingleQuotedPowerShellLiteral $engineRootPath) +
        ' -AdditionalArguments ' + (ConvertTo-SingleQuotedPowerShellLiteral $additionalArguments) +
        ' -Wait'
    $launchCommand = '$ErrorActionPreference = ''Stop''; $gateExit = 1; try { ' +
        $launchInvocation +
        '; $gateExit = 0 } catch { [Console]::Error.WriteLine($_.Exception.ToString()); $gateExit = 1 } finally { ' +
        '[IO.File]::WriteAllText(' + (ConvertTo-SingleQuotedPowerShellLiteral $launcherExitCodePath) +
        ', [string]$gateExit) }; exit $gateExit'
    $encodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($launchCommand))
    $launchStarted = Get-Date
    $launcher = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $encodedCommand
    ) -RedirectStandardOutput $launcherStdout -RedirectStandardError $launcherStderr -PassThru -WindowStyle Hidden

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $launcher.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $launcher.Refresh()
    }
    if (-not $launcher.HasExited) {
        $summary.forced_editor_cleanup = @(Stop-OwnedProjectEditors -ProjectFile $projectFile -StartedAfter $launchStarted.AddSeconds(-2))
        Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
        throw "All-project Blueprint compile exceeded $TimeoutSeconds seconds."
    }

    if (-not (Test-Path -LiteralPath $launcherExitCodePath -PathType Leaf)) {
        throw "Launch wrapper did not publish an exit-code sidecar: $launcherExitCodePath"
    }
    $exitText = (Get-Content -LiteralPath $launcherExitCodePath -Raw).Trim()
    if ($exitText -notmatch '^-?\d+$') {
        throw "Launch wrapper published an invalid exit code: $exitText"
    }
    $summary.launcher_exit_code = [int]$exitText
    if ($summary.launcher_exit_code -ne 0) {
        throw "Launch-NoShellForWinterEditor58.ps1 failed with exit code $($summary.launcher_exit_code)."
    }

    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw "Blueprint compile gate did not write its report: $reportPath"
    }
    $gate = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $summary.gate_status = [string]$gate.status
    $summary.discovered_blueprint_count = [int]$gate.discovered_blueprint_count
    $summary.compiled_blueprint_count = [int]$gate.compiled_blueprint_count
    $summary.failure_count = @($gate.failures).Count
    $summary.on_disk_package_change_count = @($gate.on_disk_package_changes).Count
    if (
        $summary.gate_status -ne 'UE58_ALL_PROJECT_BLUEPRINT_COMPILE_PASS' -or
        $summary.discovered_blueprint_count -le 0 -or
        $summary.compiled_blueprint_count -ne $summary.discovered_blueprint_count -or
        $summary.failure_count -ne 0 -or
        $summary.on_disk_package_change_count -ne 0 -or
        [bool]$gate.saved_assets -or
        [bool]$gate.save_api_called
    ) {
        throw "All-project Blueprint compile gate failed. Report: $reportPath"
    }

    $gitAfter = Get-GitPorcelain -Root $projectRootPath
    $summary.git_state_unchanged = (($gitBefore -join "`n") -ceq ($gitAfter -join "`n"))
    if (-not $summary.git_state_unchanged) {
        throw 'The Git worktree changed during all-project Blueprint compilation.'
    }
    $summary.status = 'UE58_ALL_PROJECT_BLUEPRINT_COMPILE_PASS'
}
catch {
    $summary.status = 'UE58_ALL_PROJECT_BLUEPRINT_COMPILE_FAIL'
    $summary.failure = $_.Exception.Message
}
finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    if ($null -ne $launcher) {
        try { $launcher.Refresh() } catch {}
        if (-not $launcher.HasExited) {
            $summary.forced_editor_cleanup = @(Stop-OwnedProjectEditors -ProjectFile $projectFile -StartedAfter $launchStarted.AddSeconds(-2))
            Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
        }
    }
    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
    $summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
}

Write-Host "All-project Blueprint compile summary: $summaryPath"
if ($summary.status -ne 'UE58_ALL_PROJECT_BLUEPRINT_COMPILE_PASS') {
    throw $summary.failure
}
