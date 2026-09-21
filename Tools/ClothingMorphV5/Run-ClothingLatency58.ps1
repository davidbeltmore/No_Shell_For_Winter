[CmdletBinding()]
param(
    [ValidateSet('baseline','notify','watchdog','armor_event')][string]$Mode = 'baseline',
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$RunName = '',
    [ValidateRange(1,5)][int]$Repeats = 3,
    [switch]$Transitions,
    [switch]$EquipmentQA,
    [ValidateSet(0,30,60)][int]$FpsCap = 0,
    [ValidateRange(60,660)][int]$TimeoutSeconds = 620
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $root 'NoShellForWinter.uproject'
if (@(Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'UnrealEditor.exe' -and $_.CommandLine -like "*$project*" }).Count) {
    throw 'Close the target editor before this isolated latency experiment.'
}
if (!$RunName) { $RunName = $Mode }
$output = Join-Path $root "Saved\ClothingLatencyQA\$RunName"
[void][IO.Directory]::CreateDirectory($output)
$names = @('CODEX_RUN_MIGRATION_BASELINE_PIE','CODEX_MIGRATION_PIE_SCRIPT','EF_CLOTHING_LATENCY_MODE','EF_CLOTHING_LATENCY_RUN','EF_CLOTHING_LATENCY_REPEATS','EF_CLOTHING_LATENCY_FPS')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $PSScriptRoot $(if ($EquipmentQA) { 'Validate-ClothingEquipment58.py' } elseif ($Transitions) { 'Measure-ClothingTransitions58.py' } else { 'Measure-ClothingLatency58.py' })
    $env:EF_CLOTHING_LATENCY_MODE = $Mode
    $env:EF_CLOTHING_LATENCY_RUN = $RunName
    $env:EF_CLOTHING_LATENCY_REPEATS = "$Repeats"
    $env:EF_CLOTHING_LATENCY_FPS = "$FpsCap"
    $started = Get-Date
    $log = Join-Path $output 'editor.log'
    & (Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -AdditionalArguments ('-NoSplash -NoP4 -Unattended -d3d12 -sm6 -ResX=1400 -ResY=1000 -abslog="{0}"' -f $log)
    $process = Get-Process UnrealEditor | Where-Object { $_.StartTime -ge $started.AddSeconds(-1) } | Select-Object -First 1
    if (!$process) { throw 'Editor did not start.' }
    if (!$process.WaitForExit($TimeoutSeconds*1000)) {
        [void]$process.CloseMainWindow()
        throw 'Latency experiment timed out; requested normal editor close.'
    }
    $receipt = Join-Path $output 'result.json'
    if (!(Test-Path -LiteralPath $receipt) -or (Get-Item -LiteralPath $receipt).LastWriteTime -lt $started) { throw 'Missing current latency receipt.' }
    $report = Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
    if ($report.status -ne 'PASS') { throw $report.error }
    Write-Host "Latency $Mode PASS: $(@($report.trials).Count) samples. $receipt"
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') }
}
