[CmdletBinding()]
param([ValidateRange(60, 480)][int]$TimeoutSeconds = 440, [switch]$ExtendedOnly, [switch]$LatencyRegression)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$project = Join-Path $root 'NoShellForWinter.uproject'
$existing = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'UnrealEditor.exe' -and $_.CommandLine -like "*$project*"
})
if ($existing.Count) { throw 'Close the target editor before the isolated clothing PIE test.' }
$output = Join-Path $root $(if ($LatencyRegression) { 'Saved\ClothingLatencyQA\UnisexRegression' } else { 'Saved\ClothingUnisexQA' })
[void][IO.Directory]::CreateDirectory($output)
$oldGate = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$oldExtended = $env:EF_UNISEX_EXTENDED_ONLY
$oldOutput = $env:EF_UNISEX_OUTPUT_DIR
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:EF_UNISEX_OUTPUT_DIR = Join-Path $output 'PIE'
    $env:EF_UNISEX_EXTENDED_ONLY = $(if ($ExtendedOnly) { '1' } else { '0' })
    $env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $PSScriptRoot 'Validate-UnisexPIE58.py'
    $log = Join-Path $output 'pie.log'
    $started = Get-Date
    & (Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') `
        -AdditionalArguments ('-NoSplash -NoP4 -Unattended -d3d12 -sm6 -ResX=1400 -ResY=1000 -abslog="{0}"' -f $log)
    $process = Get-Process UnrealEditor | Where-Object { $_.StartTime -ge $started.AddSeconds(-1) } | Select-Object -First 1
    if (!$process) { throw 'Target editor did not start.' }
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
        [void]$process.CloseMainWindow()
        throw "Unisex PIE exceeded $TimeoutSeconds seconds; requested editor close. See $log"
    }
    $reportPath = Join-Path $output 'PIE\result.json'
    if (!(Test-Path -LiteralPath $reportPath) -or (Get-Item -LiteralPath $reportPath).LastWriteTime -lt $started) {
        throw 'No current unisex PIE receipt was written.'
    }
    $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
    if ($report.status -ne 'PASS') { throw $report.error }
    Write-Host "Unisex PIE: PASS ($(@($report.checks).Count) checks). $reportPath"
} finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldGate
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $env:EF_UNISEX_EXTENDED_ONLY = $oldExtended
    $env:EF_UNISEX_OUTPUT_DIR = $oldOutput
}
