param(
    [Parameter(Mandatory)][string]$StagedRoot,
    [string]$ProjectRoot='D:\Projects UE5\NoShellForWinter',
    [ValidatePattern('^[A-Za-z0-9_.]+$')][string]$Body='Male.01',
    [ValidatePattern('^[A-Za-z0-9_]+$')][string]$Archetype='SkillGuy',
    [ValidateRange(1,25)][int]$Count=3,
    [int]$Seed=731,
    [ValidatePattern('^/Game/[A-Za-z0-9_/]+$')][string]$Map='/Game/_Game/Hub/HUB',
    [switch]$RequireColdItems
)
$ErrorActionPreference='Stop'
$exe=Join-Path $StagedRoot 'NoShellForWinter\Binaries\Win64\NoShellForWinter.exe'
if (!(Test-Path -LiteralPath $exe)) { $exe=Join-Path $StagedRoot 'Windows\NoShellForWinter\Binaries\Win64\NoShellForWinter.exe' }
if (!(Test-Path -LiteralPath $exe)) { throw "Missing package executable: $exe" }
$name='outfit_'+($Body -replace '\.','_')+'_'+(Get-Date -Format yyyyMMdd_HHmmssfff)
$out=Join-Path $ProjectRoot ('Saved\NPCOutfits\Packaged\'+$name)
[void][IO.Directory]::CreateDirectory($out)
$userDir=Join-Path $out 'UserData'
$command="Project.NPCOutfits.QA count=$Count body=$Body archetype=$Archetype seed=$Seed capture=3 functional=1 timeout=120 loading=fresh_process name=$name gc quit"
$arguments=$Map+' -windowed -ResX=1600 -ResY=1000 -nosound -unattended -csvNoProcessingThread -csvCaptureFrames=16000 -ExecCmds="ef.ClothingMorph.ProfileInstall 1" -csvExecCmds="90:'+$command+'" -UserDir="'+$userDir+'" -ABSLOG="'+(Join-Path $out 'game.log')+'"'
if ($RequireColdItems) { $arguments += ' -trace=loadtime -tracefile="'+(Join-Path $out 'loading.utrace')+'"' }
$started=Get-Date
$result=@{status='PENDING';reason='';command=$command;body=$Body;archetype=$Archetype;count=$Count;limit_seconds=180;scope='Fresh packaged process, real equip/unequip, reinitialization, explicit GC and normal shutdown. HUB may preload shared clothes; not an isolated cold-resource benchmark.'}
$result.map=$Map
$result.require_cold_items=[bool]$RequireColdItems
if ($RequireColdItems) { $result.scope='Isolated map initial NPC: definition and three garment classes must be nonresident at request. OS disk caches are not controlled. Native QA group remains a separate functional check.' }
$process=$null
try {
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $deadline=(Get-Date).AddSeconds(180)
    while (!$process.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; $process.Refresh() }
    if (!$process.HasExited) {
        [void]$process.CloseMainWindow()
        if (!$process.WaitForExit(10000)) { Stop-Process -Id $process.Id -Force }
        throw 'Packaged fixture exceeded 180 seconds; its isolated process was closed.'
    }
    $result.exit_code=$process.ExitCode
    $receipts=@(Get-ChildItem -LiteralPath $out -Recurse -Filter runtime.json -File | Where-Object { $_.FullName -like ('*\NPCOutfits\Native\'+$name+'\runtime.json') })
    if ($receipts.Count -ne 1) { throw "Expected one fresh outfit receipt, found $($receipts.Count)" }
    $runtime=Get-Content -LiteralPath $receipts[0].FullName -Raw | ConvertFrom-Json
    $result.runtime_receipt=$receipts[0].FullName
    $result.runtime=$runtime
    $logPath=Join-Path $out 'game.log'
    $errors=@(Select-String -LiteralPath $logPath -Pattern 'Fatal error[!:]|=== Critical error: ===|Handled ensure:|Blueprint Runtime Error|NPCOutfits FAIL|Clothing immutable buffers unavailable|Failed to (load|find object).*(DA_NPCRoster|DA_FullRag|EFClothing|Characters/(Male|Female|Data|Framework))')
    $allErrors=@(Select-String -LiteralPath $logPath -Pattern ': Error:')
    $allErrors.Line | Set-Content -LiteralPath (Join-Path $out 'startup-errors.txt')
    $result.feature_errors=@($errors.Line)
    $result.all_error_count=$allErrors.Count
    $result.prepared_reuses=@(Select-String -LiteralPath $logPath -Pattern 'ClothingInstall PreparedBindingReuse').Count
    $image=Join-Path $receipts[0].DirectoryName 'outfits.png'
    $result.screenshot=$image
    if ($process.ExitCode -ne 0 -or $runtime.status -ne 'PASS' -or !$runtime.garbage_collection_completed -or $runtime.pending_after_destroy -ne 0 -or $errors.Count -or $allErrors.Count -or !(Test-Path -LiteralPath $image)) {
        throw 'Runtime, GC, teardown, process exit, feature logs or screenshot gate failed.'
    }
    if ($Count -gt 1 -and $result.prepared_reuses -eq 0) { throw 'No prepared binding reuse observed in the multi-NPC package fixture.' }
    if ($RequireColdItems) {
        $initial=@($runtime.initial_world_outfits)
        if ($initial.Count -ne 1 -or $initial[0].state -ne 'EProjectNPCOutfitState::Ready' -or $initial[0].definition_resident_at_request -or $initial[0].item_classes_requested -ne 3 -or $initial[0].item_classes_resident_at_request -ne 0) {
            throw 'Isolated initial NPC did not prove Ready with a nonresident definition and three nonresident garment classes.'
        }
    }
    $result.status='PASS'
} catch {
    $result.status='FAIL';$result.reason=$_.Exception.Message
} finally {
    $result.seconds=((Get-Date)-$started).TotalSeconds
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $out 'process.json') -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $ProjectRoot 'Saved\NPCOutfits\latest-packaged.txt') -Value $out
}
Write-Host "Packaged outfit $($result.status): $out"
if ($result.status -ne 'PASS') { throw $result.reason }
