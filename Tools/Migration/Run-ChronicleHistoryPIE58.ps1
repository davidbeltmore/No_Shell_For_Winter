[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    throw 'Close the existing editor before running the isolated Chronicle input test.'
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $projectRoot 'Saved/Migration/ChronicleHistory'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
$env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $PSScriptRoot 'Validate-ChronicleWidgets58.py'
$env:CODEX_CHRONICLE_WBP_REPORT = Join-Path $output 'BlueprintCompile.json'
$extra = '-ExecCmds="Automation RunTests NoShellForWinter.ProjectSystems.UI.Chronicle" -TestExit="Automation Test Queue Empty" -abslog="' + (Join-Path $output 'Automation.log') + '"'
& (Join-Path $PSScriptRoot 'Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $projectRoot -AdditionalArguments $extra
