param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8"
)

$ErrorActionPreference = "Stop"
$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"
$editorCmd = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$logPath = Join-Path $ProjectRoot "Saved\Logs\TattooOpaqueAutomation_20260802.log"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"

foreach ($requiredPath in @($projectPath, $editorCmd, $receiptGuard)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required path not found: $requiredPath"
    }
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $ProjectRoot
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

& $editorCmd $projectPath `
    -unattended -nop4 -nosplash -NullRHI `
    -EnablePlugin=DazToUnreal `
    -EnablePlugin=EFCharacterCreationDazBridge `
    '-ExecCmds=Automation RunTests Project.TattooShop.RuntimeTextures.OpaqueCanvas;Quit' `
    "-abslog=$logPath"

if ($LASTEXITCODE -ne 0) {
    throw "Tattoo opaque automation failed with exit code $LASTEXITCODE"
}

if (Select-String -LiteralPath $logPath -Pattern "DazToUnreal.*not mounted|BasePBRSkinMaterial.*not available|BaseAlphaMaterial.*not available" -Quiet) {
    throw "Tattoo automation detected missing Daz material parents. See $logPath"
}

Write-Host "Tattoo opaque automation with Daz materials mounted: PASS"
