[CmdletBinding()]
param([ValidateRange(60,1800)][int]$TimeoutSeconds = 600)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (@(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) {
    throw 'Close Unreal Editor before upgrading generated clothing data.'
}
& (Join-Path $root 'Tools\Migration\Repair-DazPluginReceipt58.ps1') -ProjectRoot $root -VerifyOnly
if (!$?) { throw 'Daz receipt validation failed.' }
$out = Join-Path $root 'Saved\ClothingUnisexQA'
[void][IO.Directory]::CreateDirectory($out)
$started = Get-Date
$arguments = @(
    ('"{0}"' -f (Join-Path $root 'NoShellForWinter.uproject')),
    '-run=pythonscript', ('-script="{0}"' -f (Join-Path $PSScriptRoot 'Upgrade-UnisexCatalog58.py')),
    '-unattended', '-nop4', '-nosplash', '-NullRHI', '-NoSound',
    ('-ABSLOG="{0}"' -f (Join-Path $out 'upgrade.log'))
)
$process = Start-Process -FilePath 'D:\Unreal Engine 5\Library\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
    -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds*1000)) { throw 'Unisex upgrade exceeded its time budget; inspect the commandlet.' }
$path = Join-Path $out 'upgrade.json'
if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTime -lt $started) { throw 'No fresh upgrade receipt.' }
$report = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
if ($report.status -ne 'PASS') { throw $report.error }
Write-Host "Unisex catalog upgrade: PASS. $path"
