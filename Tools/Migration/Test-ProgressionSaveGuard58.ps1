$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Progression-SaveGuard.ps1')
$testRoot = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path ('Saved\Analysis\ProgressionPilot\SaveGuardTest_' + (Get-Date -Format yyyyMMdd_HHmmss))
$save = Join-Path $testRoot 'Saved\SaveGames'
[void][IO.Directory]::CreateDirectory($save)
[IO.File]::WriteAllText((Join-Path $save 'Existing.sav'), 'original')
Invoke-ProgressionSaveGuard -ProjectRoot $testRoot -EvidenceDirectory (Join-Path $testRoot 'Evidence') -Action {
 [IO.File]::WriteAllText((Join-Path $save 'Existing.sav'), 'modified')
 [IO.File]::WriteAllText((Join-Path $save 'Generated.sav'), 'generated')
}
if ([IO.File]::ReadAllText((Join-Path $save 'Existing.sav')) -ne 'original') { throw 'Original not restored.' }
if (Test-Path -LiteralPath (Join-Path $save 'Generated.sav')) { throw 'Generated save not isolated.' }
if ([IO.File]::ReadAllText((Join-Path $testRoot 'Evidence\AfterSaveGames\Generated.sav')) -ne 'generated') { throw 'Generated output lost.' }
Write-Host "Save guard restore/isolation: PASS $testRoot"
