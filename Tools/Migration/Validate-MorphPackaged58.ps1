param([Parameter(Mandatory)][string]$StagedRoot,[string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
$exe=Join-Path $StagedRoot 'NoShellForWinter/Binaries/Win64/NoShellForWinter.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Missing executable: $exe" }
$out=Join-Path $ProjectRoot ('Saved/Analysis/CompatibilityClosure/MorphPackaged_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$commands=@('90:Project.MorphPhysics.Report','110:Shot SHOWUI filename=morph_female',
 '130:Project.MorphPhysics.Gender Male','190:Project.MorphPhysics.Report','210:Shot SHOWUI filename=morph_male',
 '230:Project.Practice.Key W down','280:Project.Practice.Key W up','300:Project.MorphPhysics.Report',
 '320:Project.Time.Speed pause','340:Project.MorphPhysics.Report','342:Project.Time.Report','360:Project.Time.Speed pause','365:Project.Time.Report',
 '380:Project.MorphPhysics.Gender Female','450:Project.MorphPhysics.Report','470:Shot SHOWUI filename=morph_return','500:Quit') -join ','
$log=Join-Path $out 'game.log'
$arguments='/Game/_Game/Hub/HUB -windowed -ResX=1280 -ResY=900 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=520 -csvExecCmds="'+$commands+'" -UserDir="'+(Join-Path $out 'UserData')+'" -ABSLOG="'+$log+'"'
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(300000)) { [void]$process.CloseMainWindow(); throw "Packaged morph QA timeout: $out" }
$profiles=@(Select-String -LiteralPath $log -Pattern '\[MorphPhysicsQA\] mesh=(\S+) profile=(\S+)' | ForEach-Object {
 @{mesh=$_.Matches[0].Groups[1].Value;profile=$_.Matches[0].Groups[2].Value}
})
$bodies=@(Select-String -LiteralPath $log -Pattern '\[MorphPhysicsQA\] bone=(\S+) sim=(\d) valid=(\d) limit=([\d.-]+)' | ForEach-Object {
 $m=$_.Matches[0];@{bone=$m.Groups[1].Value;sim=[int]$m.Groups[2].Value;valid=[int]$m.Groups[3].Value;limit=[double]::Parse($m.Groups[4].Value,[Globalization.CultureInfo]::InvariantCulture)}
})
$pauseStates=@(Select-String -LiteralPath $log -Pattern '\[ProjectTime\] hours=[\d.]+ speed=[\d.]+ paused=([01])' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
$ok=$process.ExitCode -eq 0 -and $profiles.Count -eq 5 -and $bodies.Count -eq 35 -and ($pauseStates -join ',') -eq '1,0'
if ($ok) {
 $ok=($profiles.mesh -join ',') -eq 'Female,Male,Male,Male,Female' -and
 @($profiles | Where-Object {$_.profile -ne ('PA_'+$_.mesh+'_Morph')}).Count -eq 0 -and
 @($bodies | Where-Object {$_.sim -ne 1 -or $_.valid -ne 1 -or $_.limit -lt 1 -or $_.limit -gt 7}).Count -eq 0
}
@{passed=$ok;exit_code=$process.ExitCode;profiles=$profiles;bodies=$bodies;pause_states=$pauseStates;scope='Live cooked profiles across gender selection, movement input and pause; screenshots require visual review'} | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath ($out+'/receipt.json')
if (!$ok) { throw "Packaged morph validation failed: $out" }
Write-Host "Packaged morph runtime PASS: $out. Visual inspection remains required."
