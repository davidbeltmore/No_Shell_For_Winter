[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ExpectedTestsFile,
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 180,
    [ValidatePattern('^NoShellForWinter[.]CalystoDungeon[.][A-Za-z0-9_.]*$')]
    [string]$TestPrefix = 'NoShellForWinter.CalystoDungeon.',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$taskRoot = 'D:\Projects UE5\NoShellForWinter'
$taskProject = Join-Path $taskRoot 'NoShellForWinter.uproject'
$taskExpected = @(Get-Content -LiteralPath $ExpectedTestsFile | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($taskExpected.Count -eq 0 -or @($taskExpected | Sort-Object -Unique).Count -ne $taskExpected.Count) {
    throw 'Supply a reviewed nonempty exact test inventory, without duplicate names.'
}
foreach ($taskName in $taskExpected) {
    if (-not $taskName.StartsWith($TestPrefix, [StringComparison]::Ordinal) -or $taskName -notmatch '^NoShellForWinter[.]CalystoDungeon[.][A-Za-z0-9_.]+$') {
        throw "Unsupported test inventory name: $taskName"
    }
}
# UE 5.8 filters namespace prefixes by complete path tokens. Use its documented
# start/end anchors and OR separator to execute the reviewed exact inventory,
# including focused tests drawn from different subsystems in one process.
$taskSelection = ($taskExpected | ForEach-Object { '^' + $_ + '$' }) -join '+'
if ($taskSelection.Length -gt 16000) { throw 'Split this inventory into bounded native batches.' }
$taskExisting = @(Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor.exe' OR Name = 'UnrealEditor-Cmd.exe'")
if ($taskExisting.Count -ne 0) { throw 'Native automation requires editors closed with unsaved work preserved.' }
$taskOutput = Join-Path $taskRoot "Saved\Migration\CalystoDungeonDirectorV7\Native_$Stamp"
if (Test-Path -LiteralPath $taskOutput) { throw 'Refusing to reuse native test evidence.' }
[void](New-Item -ItemType Directory -Path $taskOutput)
$taskReport = Join-Path $taskOutput 'Report'
$taskLog = Join-Path $taskOutput 'Native.log'
$taskDriver = Join-Path $taskOutput 'Driver.ps1'
$taskArguments = @(
    '/Engine/Maps/Entry', '-unattended', '-nop4', '-nosplash', '-NullRHI', '-NoSound',
    '-DisablePlugins=BpGeneratorUltimate,MCPClientToolset,ModelContextProtocol',
    '-stdout', '-FullStdOutLogOutput',
    ('-ExecCmds="Automation RunTests ' + $taskSelection + ';Quit"'),
    '-TestExit="Automation Test Queue Empty"',
    ('-ReportExportPath="' + $taskReport + '"'), ('-ABSLOG="' + $taskLog + '"')
) -join ' '
# Store structured values as JSON and load them inside the child; no nested shell interpolation.
@{ arguments = $taskArguments; root = $taskRoot } | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $taskOutput 'launch.json') -Encoding UTF8
@'
$ErrorActionPreference = 'Stop'
$taskLaunch = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'launch.json') -Raw | ConvertFrom-Json
try {
    & (Join-Path $taskLaunch.root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $taskLaunch.root -AdditionalArguments $taskLaunch.arguments -Wait *> (Join-Path $PSScriptRoot 'driver.stdout.log')
    exit 0
} catch {
    ($_ | Out-String) | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'driver.stderr.log') -Encoding UTF8
    exit 1
}
'@ | Set-Content -LiteralPath $taskDriver -Encoding UTF8
# Python owns the child process and postprocessing under explicit bounds. The
# protected PowerShell launcher remains the only code that starts Unreal Editor.
$taskPython = (Get-Command python.exe -ErrorAction Stop).Source
& $taskPython (Join-Path $taskRoot 'Tools/Migration/calysto_native_supervisor.py') `
    --output $taskOutput --expected $ExpectedTestsFile --budget $TimeoutSeconds --test-prefix $TestPrefix
exit $LASTEXITCODE
