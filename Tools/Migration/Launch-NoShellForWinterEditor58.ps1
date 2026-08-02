param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8"
)

$ErrorActionPreference = "Stop"
$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"
$editorExe = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"

foreach ($requiredPath in @($projectPath, $editorExe, $receiptGuard)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required path not found: $requiredPath"
    }
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $ProjectRoot
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

$editorArguments = '"' + $projectPath + '" -EnablePlugin=DazToUnreal -EnablePlugin=EFCharacterCreationDazBridge'
Start-Process -FilePath $editorExe -ArgumentList $editorArguments
Write-Host "NoShellForWinter editor launch requested with Daz plugins enabled."
