param([string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference = 'Stop'
if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) { throw 'Close editor before isolated test.' }
$report = Join-Path $ProjectRoot ('Saved/Analysis/TimeKeys20261007/PIE_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($report)
$started = [DateTime]::UtcNow
. (Join-Path $ProjectRoot 'Tools/Migration/Progression-SaveGuard.ps1')
try {
 Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory ($report + '/SaveGuard') -Action {
  & (Join-Path $ProjectRoot 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -AdditionalArguments ('-ExecutePythonScript="' + $ProjectRoot + '/Tools/Migration/Validate-TimeKeysPIE58.py" -NoSound -csvNoProcessingThread -ABSLOG="' + $report + '/editor.log"') -Wait
 }
} finally {
 $receipt = $ProjectRoot+'/Saved/Analysis/TimeKeys20261007/pie.json'
 if ((Test-Path $receipt) -and (Get-Item -LiteralPath $receipt).LastWriteTimeUtc -ge $started) { Copy-Item -LiteralPath $receipt -Destination ($report+'/receipt.json') }
}
$result = Get-Content ($report+'/receipt.json') -Raw | ConvertFrom-Json
if ($result.error) { throw $result.error }
Write-Host "Time keys PASS: $report"
