[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EditorExe = 'D:\Unreal Engine 5\Library\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe',
    [int]$TimeoutSeconds = 210
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$project = Join-Path $root 'NoShellForWinter.uproject'
$script = Join-Path $root 'Tools\Migration\Validate-FoodKitAlcoholPIE58.py'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$output = Join-Path $root "Saved\Migration\FoodKitAlcohol\PIE_$stamp"
$markers = Join-Path $output 'markers.txt'
$ack = Join-Path $output 'capture_ack.txt'
$report = Join-Path $output 'FoodKitAlcoholPIE58.json'
$wrapperLog = Join-Path $output 'wrapper.log'
New-Item -ItemType Directory -Path $output -Force | Out-Null

$existing = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*NoShellForWinter.uproject*' })
foreach ($process in $existing) {
    Stop-Process -Id $process.ProcessId -Force
    Wait-Process -Id $process.ProcessId -Timeout 20 -ErrorAction SilentlyContinue
}

$env:CODEX_FOODKIT81_MANIFEST = Join-Path $root 'Saved\Migration\FoodKitAlcohol\FoodKit81Manifest.json'
$env:CODEX_FOODKIT81_PIE_OUTPUT = $output
$env:CODEX_RUN_FOODKIT_ALCOHOL_PIE = '1'
$env:CODEX_FOODKIT_ALCOHOL_PIE_SCRIPT = $script
$arguments = '"' + $project + '" -EnablePlugin=DazToUnreal -EnablePlugin=EFCharacterCreationDazBridge -NoLoadStartupPackages -NoSplash -NoLiveCoding'
$process = Start-Process -FilePath $EditorExe -ArgumentList $arguments -WindowStyle Normal -PassThru
"launched_pid=$($process.Id) output=$output" | Set-Content -LiteralPath $wrapperLog

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CodexFoodKitWindow {
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
'@
$shell = New-Object -ComObject WScript.Shell
$captured = New-Object 'System.Collections.Generic.HashSet[string]'
$timer = [Diagnostics.Stopwatch]::StartNew()
while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
    Start-Sleep -Milliseconds 250
    $process.Refresh()
    if ($process.HasExited) { break }
    if (-not (Test-Path -LiteralPath $markers)) { continue }
    $lines = @(Get-Content -LiteralPath $markers -ErrorAction SilentlyContinue)

    foreach ($line in $lines) {
        $key = $null
        $filename = $null
        $ackValue = $null
        if ($line -match '^wellfed_ui_ready=1') {
            $key = 'wellfed_ui'
            $ackValue = $key
        }
        elseif ($line -match '^alcoholized_ui_ready=1') {
            $key = 'alcoholized_ui'
            $filename = 'alcoholized_needs_status.png'
            $ackValue = $key
        }
        if (-not $key -or $captured.Contains($key)) { continue }

        $process.Refresh()
        $windowHandle = $process.MainWindowHandle
        if ($windowHandle -eq [IntPtr]::Zero) { continue }
        $null = [CodexFoodKitWindow]::ShowWindowAsync($windowHandle, 3)
        $null = [CodexFoodKitWindow]::SetForegroundWindow($windowHandle)
        $null = $shell.AppActivate($process.Id)
        Start-Sleep -Milliseconds 600
        if ($key -eq 'wellfed_ui') {
            $shell.SendKeys(',')
            Start-Sleep -Seconds 1
        }
        $null = $captured.Add($key)
        Set-Content -LiteralPath $ack -Value $ackValue
        "acknowledged=$key" | Add-Content -LiteralPath $wrapperLog
    }
}

if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force
    throw "PIE QA timed out after $TimeoutSeconds seconds."
}
if (-not (Test-Path -LiteralPath $report)) { throw "PIE report missing: $report" }
$result = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
$screenshots = @(Get-ChildItem -LiteralPath $output -File -Filter '*.png')
[pscustomobject]@{
    Status = $result.status
    VisualBatches = $result.visual_batch_count
    VisualPickups = $result.visual_pickup_count
    Checks = @($result.checks.psobject.Properties).Count
    FailedChecks = @($result.checks.psobject.Properties | Where-Object { -not $_.Value }).Count
    Screenshots = $screenshots.Count
    Report = $report
    Output = $output
} | Format-List
if ($result.status -ne 'UE58_FOODKIT_ALCOHOL_PIE_PASS') { throw "PIE QA failed: $($result.error)" }
