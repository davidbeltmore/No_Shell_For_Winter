param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8",
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development"
)

$ErrorActionPreference = "Stop"
$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"
$buildBat = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"
$targetName = "NoShellForWinter"
$receiptFileName = if ($Configuration -eq "Shipping") {
    "NoShellForWinter-Win64-Shipping.target"
}
else {
    "${targetName}.target"
}
$receiptPath = Join-Path $ProjectRoot "Binaries\Win64\$receiptFileName"

foreach ($requiredPath in @($projectPath, $buildBat, $receiptGuard)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required path not found: $requiredPath"
    }
}

# DazToUnreal and the project bridge are editor-only/precompiled. Installed
# Engine Game targets cannot use -DisablePlugin without an unsupported unique
# build environment, so the descriptor is changed only for the synchronous UBT
# invocation and restored byte-exact in finally. No Editor can run in this
# window. Cook/runtime always observe the restored, enabled descriptor.
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -ne 0) {
    throw "Close Unreal Editor before the temporary UBT-only Daz exclusion."
}
$originalProjectBytes = [System.IO.File]::ReadAllBytes($projectPath)
$originalProjectHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $projectPath).Hash
$projectText = [System.IO.File]::ReadAllText($projectPath)
$temporaryProjectText = $projectText
foreach ($pluginName in @("DazToUnreal", "EFCharacterCreationDazBridge")) {
    $pattern = '(?s)("Name"\s*:\s*"' + [Regex]::Escape($pluginName) + '"\s*,\s*"Enabled"\s*:\s*)true'
    if (![Regex]::IsMatch($temporaryProjectText, $pattern)) {
        throw "Project descriptor does not explicitly enable $pluginName."
    }
    $temporaryProjectText = [Regex]::Replace($temporaryProjectText, $pattern, '${1}false', 1)
}

$buildExitCode = 1
try {
    [System.IO.File]::WriteAllText(
        $projectPath,
        $temporaryProjectText,
        [System.Text.UTF8Encoding]::new($false))
    & $buildBat $targetName Win64 $Configuration $projectPath
    $buildExitCode = $LASTEXITCODE
}
finally {
    [System.IO.File]::WriteAllBytes($projectPath, $originalProjectBytes)
}

$restoredProjectHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $projectPath).Hash
if ($restoredProjectHash -ne $originalProjectHash) {
    throw "NoShellForWinter.uproject was not restored byte-exact after UBT."
}

if ($buildExitCode -eq 0 -and (Test-Path -LiteralPath $receiptPath)) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
        -ProjectRoot $ProjectRoot `
        -TargetName $targetName `
        -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Daz Game receipt guard failed with exit code $LASTEXITCODE"
    }
}

if ($buildExitCode -ne 0) {
    throw "${targetName} ${Configuration} build failed with exit code $buildExitCode"
}

Write-Host "${targetName} ${Configuration} build and Daz receipt repair: PASS"
