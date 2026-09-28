param([Parameter(Mandatory)][string]$StagedRoot,
      [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference = 'Stop'
$exe = Join-Path $StagedRoot 'NoShellForWinter\Binaries\Win64\NoShellForWinter.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Missing packaged executable: $exe" }
$out = Join-Path $ProjectRoot ('Saved\Analysis\TimeControl\PackagedQA_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
# CSV's frame commands keep running during a native game pause. This exercises
# the public time API in the rendered packaged game, without external UI input.
$commands = @('90:Project.Time.Hud', '100:Project.Time.Report', '120:Project.Time.Speed 2', '180:Project.Time.Report',
 '200:Shot SHOWUI filename=time_x2', '220:Project.Time.Speed 4', '280:Project.Time.Report',
 '300:Shot SHOWUI filename=time_x4', '320:Project.Time.Speed pause', '340:Project.Time.Report',
 '360:Shot SHOWUI filename=time_paused', '400:Project.Time.Report',
 '420:Project.Time.Speed pause', '480:Project.Time.Report', '500:Quit') -join ','
$log = Join-Path $out 'game.log'
# UE 5.8's CSV worker can outlive allocator teardown (captured in package_debugger.log).
# Process profiling synchronously for this short QA capture; gameplay uses the same clock.
$arguments = '/Game/_Game/Hub/HUB -windowed -ResX=1280 -ResY=900 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=520 -csvExecCmds="' + $commands + '" -UserDir="' + (Join-Path $out 'UserData') + '" -ABSLOG="' + $log + '"'
$process = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(300000)) {
 [void]$process.CloseMainWindow()
 throw "Packaged QA timed out; inspect $out"
}
$reports = @(Select-String -LiteralPath $log -Pattern '\[ProjectTime\] hours=([\d.]+) speed=([\d.]+) paused=([01]) allowed=([01]) reason=(.*)' | ForEach-Object {
 $m = $_.Matches[0]
 @{hours=[double]::Parse($m.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture); speed=[double]::Parse($m.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture); paused=[int]$m.Groups[3].Value; reason=$m.Groups[5].Value}
})
$ok = $reports.Count -eq 6
if ($ok) {
 $ok = $reports[0].speed -eq 1 -and $reports[1].speed -eq 2 -and $reports[2].speed -eq 4 -and
  $reports[3].paused -eq 1 -and $reports[4].paused -eq 1 -and $reports[3].hours -eq $reports[4].hours -and
  $reports[5].paused -eq 0 -and $reports[5].speed -eq 1 -and $reports[5].hours -gt $reports[4].hours
}
@{passed=$ok; exit_code=$process.ExitCode; reports=$reports; isolated_user_dir=(Join-Path $out 'UserData')} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'receipt.json') -Encoding UTF8
if (!$ok -or $process.ExitCode -ne 0) { throw "Packaged time validation failed: $out" }
Write-Host "Packaged runtime time controls PASS: $out. Screenshots still require visual inspection."
