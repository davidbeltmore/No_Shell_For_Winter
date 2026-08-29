param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8"
)

$ErrorActionPreference = "Stop"
$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"
$buildBat = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"

foreach ($requiredPath in @($projectPath, $buildBat, $receiptGuard)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required path not found: $requiredPath"
    }
}

# DazToUnreal is installed as a precompiled root Engine plugin. UBT 5.8 cannot
# resolve its ModuleRules from UE5Rules, so source builds temporarily exclude it.
# The exclusion must never leak into the receipt used by the next editor launch.
& $buildBat NoShellForWinterEditor Win64 Development $projectPath `
    "-DisablePlugin=DazToUnreal+EFCharacterCreationDazBridge"
$buildExitCode = $LASTEXITCODE

if (Test-Path -LiteralPath (Join-Path $ProjectRoot "Binaries\Win64\NoShellForWinterEditor.target")) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $ProjectRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Daz receipt guard failed with exit code $LASTEXITCODE"
    }
}

if ($buildExitCode -ne 0) {
    throw "NoShellForWinterEditor build failed with exit code $buildExitCode"
}

Write-Host "NoShellForWinterEditor build and Daz receipt repair: PASS"
