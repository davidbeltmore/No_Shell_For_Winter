[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$Filter = 'NoShellForWinter.InnerState',
    [int]$MinimumExpectedTests = 4
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss')
$output = Join-Path $root "Saved\Migration\InnerState20261005\Automation_$stamp"
$null = New-Item -ItemType Directory -Path $output -Force
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1') -ProjectRoot $root
if ($LASTEXITCODE -ne 0) { throw 'Daz receipt repair failed.' }
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$arguments = @((Join-Path $root 'NoShellForWinter.uproject'), '/Engine/Maps/Entry', '-unattended', '-nop4', '-nosplash', '-NullRHI', '-NoSound',
    '-NoLoadStartupPackages', '-stdout', '-FullStdOutLogOutput', "-ExecCmds=Automation RunTests $Filter;Quit",
    '-TestExit=Automation Test Queue Empty', "-ReportExportPath=$output", "-ABSLOG=$output\engine.log")
& $editor @arguments *> (Join-Path $output 'stdout.log')
$code = $LASTEXITCODE
$index = Join-Path $output 'index.json'
if (!(Test-Path -LiteralPath $index)) { throw "Automation report missing ($code): $output" }
$result = Get-Content -Raw -LiteralPath $index | ConvertFrom-Json
$summary = [pscustomobject]@{ ExitCode=$code; Passed=$result.Succeeded; Warnings=$result.SucceededWithWarnings;
    Failed=$result.Failed; NotRun=$result.NotRun; Report=$index }
$summary | ConvertTo-Json | Tee-Object -FilePath (Join-Path $output 'summary.json')
if ($code -ne 0 -or $result.Succeeded -lt $MinimumExpectedTests -or $result.Failed -ne 0 -or $result.NotRun -ne 0 -or $result.SucceededWithWarnings -ne 0) {
    throw "Inner State automation failed: $index"
}
