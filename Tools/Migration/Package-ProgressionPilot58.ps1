param([string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
      [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8')
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor/commandlet sessions before packaging.' }
$receipt = Join-Path $ProjectRoot 'Binaries\Win64\NoShellForWinter.target'
if (!(Test-Path -LiteralPath $receipt)) { throw 'Run Build-NoShellForWinterGame58.ps1 first.' }
$out = Join-Path $ProjectRoot ('Saved\Analysis\ProgressionPilot\Package_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$cooker = '-NoGameAlwaysCook -NoDefaultMaps -PACKAGE=/Game/_Game/Data/Progression/GE_ProjectAttDependencies_GE+/Game/_Game/Data/Progression/GE_ProjectStatInit_GE'
$uatExitCode = 1
try {
 $ErrorActionPreference = 'Continue'
 & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
 "-project=$(Join-Path $ProjectRoot 'NoShellForWinter.uproject')" -noP4 -platform=Win64 `
 -clientconfig=Development -target=NoShellForWinter -nocompileeditor -skipbuild `
 -map=/Engine/Maps/Entry "-AdditionalCookerOptions=$cooker" `
 "-CookOutputDir=$(Join-Path $out 'Cooked/Windows')" "-stagingdirectory=$(Join-Path $out 'Staged')" `
 -cook -stage -pak -iostore -package -archive "-archivedirectory=$(Join-Path $out 'Archive')" `
 -utf8output 2>&1 | Tee-Object -FilePath (Join-Path $out 'uat.log')
 $uatExitCode = $LASTEXITCODE
} finally { $ErrorActionPreference = 'Stop' }
if ($uatExitCode -ne 0) { throw "Pilot packaging failed with exit code ${uatExitCode}: $out" }
Write-Host "Pilot package only; full HUB and migrated save validation remain separate: $out"
