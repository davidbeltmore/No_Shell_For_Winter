param([string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
      [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
      [string]$ReuseCookedPackage = '')
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue).Count) { throw 'Close editor sessions before cooking.' }
$out = Join-Path $ProjectRoot ('Saved\Migration\InnerState20261005\Package_' + (Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
$cookDirectory = Join-Path $out 'Cooked/Windows'
$cookArguments = @('-cook')
if ($ReuseCookedPackage) {
    # Only native-code-only revisions can reuse previously validated cooked content.
    $cookDirectory = Join-Path ([IO.Path]::GetFullPath($ReuseCookedPackage)) 'Cooked/Windows'
    if (!(Test-Path -LiteralPath $cookDirectory -PathType Container)) { throw "Missing previous cook: $cookDirectory" }
    $cookArguments += '-skipcook'
    Write-Host "Reusing unchanged cooked content: $cookDirectory"
}
$ErrorActionPreference = 'Continue'
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun `
 "-project=$(Join-Path $ProjectRoot 'NoShellForWinter.uproject')" -noP4 -platform=Win64 `
 -clientconfig=Development -target=NoShellForWinter -nocompileeditor -skipbuild `
 '-map=/Game/_Game/Hub/HUB' `
 '-AdditionalCookerOptions=-PACKAGE=/Game/DirtyPawnSystem/Materials/DAZ/M_DirtyPawn_DAZSkinWrapper' `
 "-CookOutputDir=$cookDirectory" "-stagingdirectory=$(Join-Path $out 'Staged')" `
 @cookArguments -stage -pak -iostore -package -utf8output 2>&1 | Tee-Object -FilePath (Join-Path $out 'uat.log')
$result = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($result -ne 0) { throw "Inner State HUB package failed ($result): $out" }
Write-Host "Inner State HUB package: $out"
