[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArchiveRoot,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Stamp,
    [ValidateSet('Visual','NativeEquipment','NativeController','NativeChests')][string]$Mode = 'Visual'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'EquipmentAutomationReport.ps1')
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$archive = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$exe = Join-Path $archive 'Windows/NoShellForWinter/Binaries/Win64/NoShellForWinter.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Packaged Development executable is missing: $exe" }
$out = Join-Path $root "Saved/EquipmentQA/Packaged_$Stamp"
if (Test-Path -LiteralPath $out) { throw 'Use a fresh Stamp; packaged QA must have no existing saves or receipts.' }
[void][IO.Directory]::CreateDirectory($out)
$user = Join-Path $out 'User'
$log = Join-Path $out 'game.log'
$report = Join-Path $out 'Report'
$arguments = @('-NoP4', '-NoSplash', '-unattended', ('-UserDir="{0}"' -f $user), ('-ABSLOG="{0}"' -f $log))
$expected = @()
if ($Mode -eq 'Visual') {
    $arguments = @('/Game/_Game/Hub/HUB') + $arguments + @('-EFEquipmentQA','-windowed','-ResX=1920','-ResY=1080','-d3d12','-sm6')
} else {
    $expected = @(if ($Mode -eq 'NativeChests') { 'EF.Chests.Authored.MapInstances'; 'EF.Chests.Lifecycle.NativeStorage'; 'EF.Chests.Selection.100000Trials' } elseif ($Mode -eq 'NativeEquipment') { 'EF.Equipment.Native.Vertical'; 'EF.Equipment.Policy.Contracts' } else { 'NoShellForWinter.CalystoDungeon.Director.DormantController.AuditedNpcProfilePreflight' })
    $filter = (@($expected | ForEach-Object { '^' + $_ + '$' }) -join '+')
    $map = if ($Mode -eq 'NativeChests') { '/Game/_Game/Tests/ChestProcedural' } else { '/Engine/Maps/Entry' }
    $arguments = @($map) + $arguments + @('-NullRHI','-NoSound', ('-ExecCmds="Automation RunTests {0};Quit"' -f $filter), '-TestExit="Automation Test Queue Empty"', ('-ReportExportPath="{0}"' -f $report))
}
$receipt = [ordered]@{ status='PENDING'; mode=$Mode; archive=$archive; executable_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash; user_dir=$user; log=$log; expected_tests=$expected }
$receiptPath = Join-Path $out 'launch.json'
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
$process = Start-Process -FilePath $exe -ArgumentList ($arguments -join ' ') -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
$null = $process.Handle # Retain the native handle so ExitCode survives process exit.
$deadline = (Get-Date).AddSeconds(300)
while (-not $process.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500; $process.Refresh() }
if (-not $process.HasExited) {
    [void]$process.CloseMainWindow()
    if (-not $process.WaitForExit(5000)) { Stop-Process -Id $process.Id -Force }
    $receipt.status = 'FAIL'; $receipt.error = 'Packaged QA deadline exceeded.'
} elseif ($process.ExitCode -ne 0) {
    $receipt.status = 'FAIL'; $receipt.error = "Packaged process exited with code $($process.ExitCode)."
} elseif ($Mode -eq 'Visual') {
    $result = Join-Path $user 'Saved/EquipmentQA/Packaged/result.json'
    if ((Test-Path -LiteralPath $result) -and (Get-Content -Raw -LiteralPath $result | ConvertFrom-Json).status -eq 'PASS') { $receipt.status = 'PASS' }
    else { $receipt.status = 'FAIL'; $receipt.error = "Packaged visual/logic result did not pass: $result" }
} else {
    $index = Join-Path $report 'index.json'
    if (Test-Path -LiteralPath $index) {
        $results = Get-Content -Raw -LiteralPath $index | ConvertFrom-Json
        if (Test-ExactEquipmentAutomationReport -Report $results -Expected $expected) { $receipt.status = 'PASS' }
    }
    if ($receipt.status -ne 'PASS') { $receipt.status = 'FAIL'; $receipt.error = "Exact packaged Automation gate failed: $index" }
}
$receipt.exit_code = $process.ExitCode
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
if ($receipt.status -ne 'PASS') { throw $receipt.error }
Write-Host "Packaged equipment $Mode QA: PASS ($receiptPath)"
