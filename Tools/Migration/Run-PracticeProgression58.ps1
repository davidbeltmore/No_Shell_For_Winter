param([ValidateSet('Automation','Visual','Reload','Respawn','Activities')][string]$Mode='Automation', [string]$ProjectRoot='D:\Projects UE5\NoShellForWinter', [switch]$InViewport)
$ErrorActionPreference='Stop'
if (@(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor sessions before isolated validation.' }
$report=Join-Path $ProjectRoot ('Saved/Analysis/PracticeProgression/' + $Mode + '_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($report)
if ($Mode -eq 'Automation') {
 $arguments='/Engine/Maps/Entry -unattended -nop4 -nosplash -NullRHI -NoSound -ini:Game:[/Script/EFProjectSystemsGameplay.ProjectPracticeSettings]:bEnabled=False -ExecCmds="Automation RunTests NoShellForWinter.Practice+NoShellForWinter.ProgressionPilot+NoShellForWinter.InnerDoctrine" -TestExit="Automation Test Queue Empty" -ReportExportPath="' + $report + '"'
} else {
 $script=Join-Path $ProjectRoot 'Tools/Migration/Validate-PracticeProgression58.py'
 $arguments='-ExecutePythonScript="' + $script + '" -NoSound'
 if ($Mode -eq 'Reload') { $arguments+=' -PracticeReload' }
 if ($Mode -eq 'Respawn') { $arguments+=' -PracticeRespawn' }
 if ($Mode -eq 'Activities') { $arguments+=' -PracticeActivities -unattended' }
}
$arguments+=' -csvNoProcessingThread -ABSLOG="' + $report + '/editor.log"'
if ($InViewport) { $arguments+=' -PracticeViewport' }
$started=[DateTime]::UtcNow
. (Join-Path $ProjectRoot 'Tools/Migration/Progression-SaveGuard.ps1')
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory ($report+'/SaveGuard') -Action {
 if ($Mode -in @('Reload','Respawn')) {
  $fixture=Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Saved/Analysis/PracticeProgression') -Filter 'PracticeV2_QA.sav' -File -Recurse |
   Where-Object { $_.FullName -match 'Visual_[^\\]+\\SaveGuard\\AfterSaveGames' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (!$fixture) { throw 'Run Visual validation first.' }
  Copy-Item -LiteralPath $fixture.FullName -Destination (Join-Path $ProjectRoot 'Saved/SaveGames/PracticeV2_QA.sav') -Force
 }
 & (Join-Path $ProjectRoot 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
if ($Mode -eq 'Automation') {
 $receipt=Get-Content -LiteralPath ($report+'/index.json') -Raw | ConvertFrom-Json
 if ($receipt.Failed -or $receipt.NotRun -or $receipt.SucceededWithWarnings -or $receipt.Succeeded -lt 22) { throw 'Practice automation failed.' }
} else {
 $path=Join-Path $ProjectRoot ('Saved/Analysis/PracticeProgression/' + $Mode + '/receipt.json')
 if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started) { throw 'Practice test interrupted.' }
 Copy-Item -LiteralPath $path -Destination ($report+'/receipt.json')
 $receipt=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
 if ($receipt.error) { throw $receipt.error }
}
Write-Host "Practice ${Mode}: $report"
