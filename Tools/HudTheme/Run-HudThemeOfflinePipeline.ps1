[CmdletBinding()]
param(
    [Parameter()]
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,

    [Parameter()]
    [string]$PythonExe = 'python'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectFile = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw "NoShellForWinter target project not found: $projectFile"
}
if ((Split-Path -Leaf $ProjectRoot) -ieq 'LustAsDeadlySin') {
    throw 'Refusing to operate in the read-only source project.'
}

$generator = Join-Path $PSScriptRoot 'Generate-HudThemeVariants.py'
$validator = Join-Path $PSScriptRoot 'Validate-HudThemeVariants.py'

& $PythonExe $generator --self-test
if ($LASTEXITCODE -ne 0) {
    throw "HUD theme transform self-test failed with exit code $LASTEXITCODE"
}

& $PythonExe $generator --project-root $ProjectRoot
if ($LASTEXITCODE -ne 0) {
    throw "HUD theme generation failed with exit code $LASTEXITCODE"
}

& $PythonExe $validator --project-root $ProjectRoot
if ($LASTEXITCODE -ne 0) {
    throw "HUD theme validation failed with exit code $LASTEXITCODE"
}

Write-Host 'HUD theme offline generation and validation: PASS'
Write-Host 'No Unreal assets were imported by this wrapper.'
