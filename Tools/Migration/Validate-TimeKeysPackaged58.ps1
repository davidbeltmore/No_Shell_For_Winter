param([Parameter(Mandatory)][string]$StagedRoot,
      [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
$out=Join-Path $ProjectRoot ('Saved/Analysis/TimeKeys20261007/Packaged_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$exe=Join-Path $StagedRoot 'NoShellForWinter/Binaries/Win64/NoShellForWinter.exe'
$commands=@('90:Project.Time.Hud','100:Project.Time.Report','110:Project.Time.Key Equals','150:Project.Time.Report',
'160:Project.Time.Key Equals','200:Project.Time.Report','210:Project.Time.Key P','250:Project.Time.Report',
'260:Shot SHOWUI filename=keys_native_pause','270:Project.Time.Key Equals','280:Project.Time.Report',
'300:Project.Time.Key P','320:Project.Time.Key Hyphen','350:Project.Time.Report',
'380:Project.Time.Key Apostrophe','420:Project.Time.Report',
'430:Shot SHOWUI filename=keys_wait','450:Project.Time.Key Apostrophe','470:Project.Time.Report','480:Project.Time.Key Equals','490:Project.Time.Report','520:Quit') -join ','
$log=Join-Path $out 'game.log'
$launchArguments='/Game/_Game/Hub/HUB -windowed -ResX=1280 -ResY=900 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=540 -csvExecCmds="'+$commands+'" -UserDir="'+$out+'/UserData" -ABSLOG="'+$log+'"'
$process=Start-Process -FilePath $exe -ArgumentList $launchArguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(240000)) { [void]$process.CloseMainWindow(); throw "240-second packaged limit: $out" }
$reports=@(Select-String -LiteralPath $log -Pattern '\[ProjectTime\] hours=([\d.]+) speed=([\d.]+) paused=([01])' | ForEach-Object {
 $m=$_.Matches[0]; @{speed=[double]::Parse($m.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture);paused=[int]$m.Groups[3].Value}
})
$menus=@(Select-String -LiteralPath $log -Pattern '\[ProjectTimeMenus\] native=([01]) wait=([01]) waiting=([01])' | ForEach-Object {
 $m=$_.Matches[0]; @{native=[int]$m.Groups[1].Value;wait=[int]$m.Groups[2].Value}
})
$expected=@(@(1,0,0,0),@(2,0,0,0),@(4,0,0,0),@(1,1,1,0),@(1,1,1,0),@(1,1,0,0),@(1,1,0,1),@(1,1,0,0),@(1,0,0,0))
$ok=$process.ExitCode -eq 0 -and $reports.Count -eq 9 -and $menus.Count -eq 9
if ($ok) {
 for ($i=0;$i -lt 9;$i++) { $e=$expected[$i]; $ok=$ok -and $reports[$i].speed -eq $e[0] -and $reports[$i].paused -eq $e[1] -and $menus[$i].native -eq $e[2] -and $menus[$i].wait -eq $e[3] }
}
@{passed=$ok;exit_code=$process.ExitCode;reports=$reports;menus=$menus} | ConvertTo-Json -Depth 6 | Set-Content ($out+'/receipt.json') -Encoding UTF8
if (!$ok) { throw "Packaged key validation failed: $out" }
Write-Host "Packaged key validation PASS: $out; inspect screenshots."
