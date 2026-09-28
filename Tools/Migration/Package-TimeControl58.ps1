param([string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
      [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8')
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor sessions before cooking.' }
$out = Join-Path $ProjectRoot ('Saved\Analysis\TimeControl\Package_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$exitCode = 1
try {
 $ErrorActionPreference = 'Continue'
 & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
  "-project=$(Join-Path $ProjectRoot 'NoShellForWinter.uproject')" -noP4 -platform=Win64 `
  -clientconfig=Development -target=NoShellForWinter -nocompileeditor -skipbuild `
  '-map=/Game/_Game/Hub/HUB+/Game/_Game/Locations/StorySelection' `
  '-AdditionalCookerOptions=-PACKAGE=/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper' `
  "-CookOutputDir=$(Join-Path $out 'Cooked/Windows')" "-stagingdirectory=$(Join-Path $out 'Staged')" `
  -cook -stage -pak -iostore -package -utf8output 2>&1 | Tee-Object -FilePath (Join-Path $out 'uat.log')
 $exitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = 'Stop' }
if ($exitCode -ne 0) { throw "Time control package failed (${exitCode}): $out" }
Write-Host "Time control HUB/StorySelection package: $out"
