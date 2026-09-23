$ErrorActionPreference = 'Stop'
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) { throw 'Use the current editor; this launcher requires no existing editor.' }
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$keys = @('CODEX_RUN_MIGRATION_BASELINE_PIE','CODEX_MIGRATION_PIE_SCRIPT')
$prior = @{}
foreach ($key in $keys) { $prior[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
try {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $PSScriptRoot 'Open-ProceduralChestDetails.py'
    & (Join-Path $root 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -NoAssetRegistryCache `
        -AdditionalArguments '-NoSplash -NoP4 -LOG=ProceduralChestReopen.log'
} finally {
    foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $prior[$key], 'Process') }
}
