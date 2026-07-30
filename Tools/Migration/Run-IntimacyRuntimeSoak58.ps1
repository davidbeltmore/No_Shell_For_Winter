param(
    [ValidateRange(1, 10)]
    [int]$CyclesPerGender = 3,
    [ValidateRange(1, 5)]
    [int]$MaxAttemptsPerCycle = 3,
    [int]$DeadlineSeconds = 210,
    [string]$OutputRoot = "Saved\Migration\Phase5\Runtime\IntimacySoak58"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$editor = "D:\Unreal Engine 5\Library\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$uproject = Join-Path $root "NoShellForWinter.uproject"
$cycleScript = Join-Path $root "Tools\Migration\IntimacyRuntimeSoakCycle58.py"
$output = Join-Path $root $OutputRoot
$summaryPath = Join-Path $output "summary.json"

New-Item -ItemType Directory -Force -Path $output | Out-Null
$results = @()

function Stop-ProjectEditorProcesses {
    $processes = Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe'" |
        Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($uproject, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 }
    foreach ($candidate in $processes) {
        Stop-Process -Id $candidate.ProcessId -Force -ErrorAction SilentlyContinue
    }
    if (@($processes).Count -gt 0) {
        Start-Sleep -Seconds 2
    }
}

$previous = @{
    CODEX_RUN_MIGRATION_BASELINE_PIE = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
    CODEX_MIGRATION_PIE_SCRIPT = $env:CODEX_MIGRATION_PIE_SCRIPT
    CODEX_INTIMACY_SOAK_GENDER = $env:CODEX_INTIMACY_SOAK_GENDER
    CODEX_INTIMACY_SOAK_RESULT = $env:CODEX_INTIMACY_SOAK_RESULT
    CODEX_PROJECT_INTIMACY_VISUAL_OUTPUT_FILE = $env:CODEX_PROJECT_INTIMACY_VISUAL_OUTPUT_FILE
    CODEX_PROJECT_INTIMACY_VISUAL_HOLD_SECONDS = $env:CODEX_PROJECT_INTIMACY_VISUAL_HOLD_SECONDS
    CODEX_PROJECT_INTIMACY_VISUAL_DEADLINE_SECONDS = $env:CODEX_PROJECT_INTIMACY_VISUAL_DEADLINE_SECONDS
}

try {
    foreach ($gender in @("Male", "Female")) {
        for ($cycle = 1; $cycle -le $CyclesPerGender; $cycle++) {
            $stem = "{0}_{1:D2}" -f $gender, $cycle
            $resultPath = Join-Path $output "$stem.json"
            $runtimePath = Join-Path $output "$stem.runtime.txt"
            $result = $null
            for ($attempt = 1; $attempt -le $MaxAttemptsPerCycle; $attempt++) {
                Stop-ProjectEditorProcesses
                $attemptStem = "{0}.attempt{1:D2}" -f $stem, $attempt
                $logPath = Join-Path $output "$attemptStem.log"
                Remove-Item -LiteralPath $resultPath, $runtimePath, $logPath -Force -ErrorAction SilentlyContinue

                $env:CODEX_RUN_MIGRATION_BASELINE_PIE = "1"
                $env:CODEX_MIGRATION_PIE_SCRIPT = $cycleScript
                $env:CODEX_INTIMACY_SOAK_GENDER = $gender
                $env:CODEX_INTIMACY_SOAK_RESULT = $resultPath
                $env:CODEX_PROJECT_INTIMACY_VISUAL_OUTPUT_FILE = $runtimePath
                $env:CODEX_PROJECT_INTIMACY_VISUAL_HOLD_SECONDS = "0.05"
                $env:CODEX_PROJECT_INTIMACY_VISUAL_DEADLINE_SECONDS = "$DeadlineSeconds"

                $line = ('"{0}" -unattended -nop4 -nosplash -NoVSync -NoSound -stdout -FullStdOutLogOutput -ExecCmds="t.MaxFPS 120" -abslog="{1}"' -f $uproject, $logPath)
                $process = Start-Process -FilePath $editor -ArgumentList $line -PassThru
                try {
                    $deadline = [DateTime]::UtcNow.AddSeconds($DeadlineSeconds)
                    while ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $resultPath)) {
                        if ($process.HasExited) {
                            Start-Sleep -Seconds 3
                            break
                        }
                        Start-Sleep -Milliseconds 500
                    }

                    if (Test-Path -LiteralPath $resultPath) {
                        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
                        if (-not $process.HasExited -and -not $process.WaitForExit(30000)) {
                            Stop-Process -Id $process.Id -Force
                        }
                        if ($result.success) {
                            break
                        }
                    }
                    Write-Warning "[IntimacySoak58] RETRY $stem attempt=$attempt result=$([bool]$result)"
                }
                finally {
                    if (-not $process.HasExited) {
                        Stop-Process -Id $process.Id -Force
                    }
                    Start-Sleep -Seconds 2
                }
            }

            if ($null -eq $result -or -not $result.success) {
                throw "Intimacy soak cycle failed after $MaxAttemptsPerCycle attempts: $stem"
            }
            $results += $result
            $equipmentCount = @($result.pre.PSObject.Properties).Count
            Write-Host "[IntimacySoak58] PASS $stem equipment=$equipmentCount"
        }
    }

    $summary = [ordered]@{
        success = ($results.Count -eq (2 * $CyclesPerGender)) -and (@($results | Where-Object { -not $_.success }).Count -eq 0)
        cycles_per_gender = $CyclesPerGender
        total_cycles = $results.Count
        male_cycles = @($results | Where-Object { $_.gender -eq "Male" }).Count
        female_cycles = @($results | Where-Object { $_.gender -eq "Female" }).Count
        results = $results
    }
    $summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding utf8
    if (-not $summary.success) {
        throw "Intimacy runtime soak summary failed."
    }
    Write-Host "[IntimacySoak58] PASS total=$($summary.total_cycles) summary=$summaryPath"
}
finally {
    foreach ($entry in $previous.GetEnumerator()) {
        if ($null -eq $entry.Value) {
            Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue
        }
        else {
            Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value
        }
    }
}
