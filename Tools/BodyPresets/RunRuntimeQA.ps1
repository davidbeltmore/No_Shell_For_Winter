param([string]$ProjectRoot='D:\Projects UE5\NoShellForWinter')
$ErrorActionPreference='Stop'
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) { throw 'Runtime QA owns a fresh editor process; close the previous fixture first.' }
. (Join-Path $ProjectRoot 'Tools\Migration\Progression-SaveGuard.ps1')
$evidence=Join-Path $ProjectRoot ('Saved\BodyPresetsQA\RuntimeSaveGuard_'+(Get-Date -Format yyyyMMdd_HHmmss))
$started=Get-Date
Invoke-ProgressionSaveGuard -ProjectRoot $ProjectRoot -EvidenceDirectory $evidence -Action {
 $arguments='-nosound -ExecutePythonScript="'+(Join-Path $ProjectRoot 'Tools\BodyPresets\RuntimeQA.py')+'"'
 & (Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -AdditionalArguments $arguments -Wait
}
$result=Get-Item -LiteralPath (Join-Path $ProjectRoot 'Saved\BodyPresetsQA\Runtime\runtime.txt')
if ($result.LastWriteTime -lt $started -or !(Select-String -LiteralPath $result.FullName -Pattern 'status=PASS' -Quiet)) { throw 'Runtime QA did not produce a fresh PASS receipt.' }
Get-Content -LiteralPath $result.FullName
