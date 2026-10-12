param([string]$ProjectRoot='D:\Projects UE5\NoShellForWinter',
      [string]$EngineRoot='D:\Unreal Engine 5\Library\UE_5.8',
      [ValidatePattern('^$|^/Game/[A-Za-z0-9_/]+$')][string]$AdditionalMap='')
$ErrorActionPreference='Stop'
if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor sessions before cooking.' }
$out=Join-Path $ProjectRoot ('Saved\BodyPresetsQA\Package_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$start=Get-Date
$maps='/Game/_Game/Hub/HUB'
if ($AdditionalMap) { $maps += '+' + $AdditionalMap }
Set-Content -LiteralPath (Join-Path $ProjectRoot 'Saved\BodyPresetsQA\latest-package.txt') -Value $out
$ErrorActionPreference='Continue'
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
 "-project=$(Join-Path $ProjectRoot 'NoShellForWinter.uproject')" -noP4 -platform=Win64 `
 -clientconfig=Development -target=NoShellForWinter -nocompileeditor -skipbuild `
 "-map=$maps" `
 '-AdditionalCookerOptions=-PACKAGE=/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper -COOKDIR=/Game/_Game/Data/CharacterCreation -COOKDIR=/EFClothingMorph/_Internal/Compiled/V5' `
 "-CookOutputDir=$(Join-Path $out 'Cooked/Windows')" "-stagingdirectory=$(Join-Path $out 'Staged')" `
 -cook -stage -pak -iostore -package -utf8output 2>&1 | Tee-Object -FilePath (Join-Path $out 'uat.log')
$result=$LASTEXITCODE
$ErrorActionPreference='Stop'
@{exit_code=$result; elapsed_seconds=((Get-Date)-$start).TotalSeconds; scope='HUB body presets and clothing'; reused_cook=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'package-result.json')
if ($result -ne 0) { throw "Body preset HUB package failed ($result): $out" }
Write-Host "Body preset HUB package: $out"
