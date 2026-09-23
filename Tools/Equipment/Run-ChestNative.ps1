param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Stamp = 'Native01')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root "Saved/EquipmentQA/ProceduralChests/$Stamp"
if (Test-Path -LiteralPath $out) { throw 'Use fresh evidence.' }
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) { throw 'Preserve and close the editor first.' }
New-Item -ItemType Directory -Path $out | Out-Null
$report = Join-Path $out 'Report'
$log = Join-Path $out 'editor.log'
$arguments = '/Game/_Game/Tests/ChestProcedural -unattended -nop4 -nosplash -NullRHI -NoSound -DisablePlugins=BpGeneratorUltimate,MCPClientToolset,ModelContextProtocol -ExecCmds="MAP LOAD FILE=/Game/_Game/Tests/ChestProcedural,Automation RunTests EF.Chests;Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath="{0}" -ABSLOG="{1}"' -f $report,$log
& (Join-Path $root 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -Wait -AdditionalArguments $arguments
if (!(Test-Path -LiteralPath (Join-Path $report 'index.json'))) { throw 'Missing automation report.' }
$result = Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw | ConvertFrom-Json
. (Join-Path $PSScriptRoot 'EquipmentAutomationReport.ps1')
if (!(Test-ExactEquipmentAutomationReport -Report $result -Expected @('EF.Chests.Authored.MapInstances','EF.Chests.Lifecycle.NativeStorage','EF.Chests.Selection.100000Trials'))) { throw 'Chest native tests failed or exact inventory changed.' }
Write-Host 'PASS: three exact chest native suites completed.'
