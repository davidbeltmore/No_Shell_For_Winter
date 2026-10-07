param([ValidateSet('Automation','Visual','Reload')][string]$Mode = 'Automation',
      [switch]$CompileBlueprints,
      [switch]$LegacySave,
      [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor sessions before isolated time validation.' }
$report = Join-Path $ProjectRoot ('Saved\Analysis\TimeControl\' + $Mode + '_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($report)
$runStartedUtc = [DateTime]::UtcNow
if ($Mode -eq 'Automation') {
 $arguments = '/Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -NoSound -ExecCmds="Automation RunTests NoShellForWinter.TimeControl+EFProjectSystems.DayCycle" -TestExit="Automation Test Queue Empty" -ReportExportPath="' + $report + '" -ABSLOG="' + $report + '\editor.log"'
} else {
 $script = Join-Path $ProjectRoot 'Tools\Migration\Validate-TimeControlPIE58.py'
 if ($Mode -eq 'Reload') { $script = Join-Path $ProjectRoot 'Tools\Migration\Validate-TimeReloadPIE58.py' }
 $arguments = '-ExecutePythonScript="' + $script + '" -NoSound -ABSLOG="' + $report + '\editor.log"'
 if ($CompileBlueprints) { $arguments += ' -TimeCompileBlueprints' }
 if ($LegacySave) { $arguments += ' -TimeLegacySave' }
}
# Avoid the engine CSV processing-thread/allocator shutdown race in short QA runs.
$arguments += ' -csvNoProcessingThread'
. (Join-Path $ProjectRoot 'Tools\Migration\Progression-SaveGuard.ps1')
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory ($report + '\SaveGuard') -Action {
 if ($Mode -eq 'Reload') {
  if ($LegacySave) {
   $fixture = Get-Item -LiteralPath (Join-Path $ProjectRoot 'Saved\Analysis\TimeControl\Visual_20260928_022101\SaveGuard\AfterSaveGames\TimeControl_QA_World.sav')
  } else {
   $fixture = Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Saved\Analysis\TimeControl') -Filter 'TimeControl_QA_World.sav' -Recurse -File |
   Where-Object { $_.FullName -match 'Visual_[^\\]+\\SaveGuard\\AfterSaveGames' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  }
  if (!$fixture) { throw 'Run the visual save test first.' }
  Copy-Item -LiteralPath $fixture.FullName -Destination (Join-Path $ProjectRoot 'Saved\SaveGames\TimeControl_QA_World.sav') -Force
 }
 & (Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
if ($Mode -eq 'Automation') {
 $result = Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw | ConvertFrom-Json
 if ($result.Failed -or $result.NotRun -or $result.SucceededWithWarnings -or $result.Succeeded -lt 5) { throw "Time automation failed: $report" }
} else {
 $receipt = if ($Mode -eq 'Reload') { 'Saved\Analysis\TimeControl\reload.json' } else { 'Saved\Analysis\TimeControl\Visual\receipt.json' }
 $receiptPath = Join-Path $ProjectRoot $receipt
 if (!(Test-Path -LiteralPath $receiptPath) -or (Get-Item -LiteralPath $receiptPath).LastWriteTimeUtc -lt $runStartedUtc) { throw "Test interrupted before receipt was written: $report" }
 Copy-Item -LiteralPath $receiptPath -Destination (Join-Path $report 'receipt.json')
 $result = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
 if ($result.error) { throw $result.error }
}
Write-Host "Time control ${Mode}: $report"
