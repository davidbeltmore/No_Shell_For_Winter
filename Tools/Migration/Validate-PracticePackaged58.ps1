param([Parameter(Mandatory)][string]$StagedRoot, [string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
$exe=Join-Path $StagedRoot 'NoShellForWinter/Binaries/Win64/NoShellForWinter.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Missing executable: $exe" }
$packageRoot=Split-Path (Split-Path $StagedRoot -Parent) -Parent
$registry=Join-Path $packageRoot 'Cooked/Windows/NoShellForWinter/AssetRegistry.bin'
if (!(Test-Path -LiteralPath $registry)) { throw 'Missing cooked registry evidence.' }
$registryNames=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($registry))
$legacyCuePresent=$registryNames.Contains('ACF_DefaultReaction_GC') -and $registryNames.Contains('ACF_DefaultHit_GC')
if (!$legacyCuePresent) { throw 'Cook omitted a public compatibility cue asset.' }
$out=Join-Path $ProjectRoot ('Saved/Analysis/PracticeProgression/PackagedQA_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$commands=@('90:Project.Time.Hud','100:Project.Practice.Start ReadBook','120:Project.Practice.Report','121:Project.Time.Report',
 '160:Project.Time.Speed 2','200:Project.Practice.Report','201:Project.Time.Report','220:Shot SHOWUI filename=practice_x2',
 '240:Project.Time.Speed 4','300:Project.Practice.Report','301:Project.Time.Report','320:Shot SHOWUI filename=practice_x4',
 '340:Project.Time.Speed pause','360:Project.Practice.Report','361:Project.Time.Report','380:Shot SHOWUI filename=practice_paused',
 '410:Project.Practice.Report','411:Project.Time.Report','430:Project.Time.Speed pause','480:Project.Practice.Report','481:Project.Time.Report','500:Quit') -join ','
$log=Join-Path $out 'game.log'
$arguments='/Game/_Game/Hub/HUB -windowed -ResX=1280 -ResY=900 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=520 -csvExecCmds="'+$commands+'" -UserDir="'+(Join-Path $out 'UserData')+'" -ABSLOG="'+$log+'"'
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(300000)) { [void]$process.CloseMainWindow(); throw "Packaged QA timeout: $out" }
$reports=@(Select-String -LiteralPath $log -Pattern '\[ProjectPractice\] active=([01]) activity=(\S+) hours=([\d.]+) book=([\d.]+) intelligence=([\d.]+) practice=([\d.]+) efficiency=([\d.]+) level=(\d+) xp=(\d+) perks=(\d+) skills=(\d+) paused=([01])' | ForEach-Object {
 $m=$_.Matches[0]
 @{active=[int]$m.Groups[1].Value; activity=$m.Groups[2].Value;
   hours=[double]::Parse($m.Groups[3].Value,[Globalization.CultureInfo]::InvariantCulture);
   practice=[double]::Parse($m.Groups[6].Value,[Globalization.CultureInfo]::InvariantCulture);
   perks=[int]$m.Groups[10].Value; paused=[int]$m.Groups[12].Value}
})
$clocks=@(Select-String -LiteralPath $log -Pattern '\[ProjectTime\] hours=([\d.]+) speed=([\d.]+) paused=([01])' | ForEach-Object {
 $m=$_.Matches[0]
 @{hours=[double]::Parse($m.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture);
   speed=[double]::Parse($m.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture);paused=[int]$m.Groups[3].Value}
})
$ok=$reports.Count -eq 6 -and $clocks.Count -eq 6
if ($ok) {
 $ok=@($reports | Where-Object {$_.active -ne 1 -or $_.activity -ne 'ReadBook' -or $_.perks -ne 0}).Count -eq 0 -and
  $reports[1].hours -gt $reports[0].hours -and $reports[2].hours -gt $reports[1].hours -and
  $reports[3].paused -eq 1 -and $reports[4].paused -eq 1 -and $reports[3].hours -eq $reports[4].hours -and
  $reports[3].practice -eq $reports[4].practice -and $reports[5].hours -gt $reports[4].hours -and
  $clocks[0].speed -eq 1 -and $clocks[1].speed -eq 2 -and $clocks[2].speed -eq 4 -and
  $clocks[3].paused -eq 1 -and $clocks[4].paused -eq 1 -and $clocks[3].hours -eq $clocks[4].hours
}
$questErrors=@(Select-String -LiteralPath $log -SimpleMatch "Can't Start the quest")
$cueConflicts=@(Select-String -LiteralPath $log -SimpleMatch 'Multiple gameplay cues are triggered')
$ok=$ok -and $process.ExitCode -eq 0 -and $questErrors.Count -eq 0 -and $cueConflicts.Count -eq 0
@{passed=$ok;exit_code=$process.ExitCode;reports=$reports;clocks=$clocks;quest_errors=$questErrors.Count;cue_conflicts=$cueConflicts.Count;legacy_cue_in_cooked_registry=$legacyCuePresent} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath ($out+'/receipt.json')
if (!$ok -or $process.ExitCode -ne 0) { throw "Packaged practice validation failed: $out" }
Write-Host "Packaged practice runtime PASS: $out. Visual inspection remains required."
