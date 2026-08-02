param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
$receiptPath = Join-Path $ProjectRoot "Binaries\Win64\NoShellForWinterEditor.target"

if (!(Test-Path -LiteralPath $receiptPath)) {
    throw "Editor target receipt not found: $receiptPath"
}

$content = [System.IO.File]::ReadAllText($receiptPath)
$updated = $content
$pluginNames = @("DazToUnreal", "EFCharacterCreationDazBridge")

foreach ($pluginName in $pluginNames) {
    $pattern = '(?s)("Name"\s*:\s*"' + [Regex]::Escape($pluginName) + '"\s*,\s*"Enabled"\s*:\s*)(true|false)'
    $match = [Regex]::Match($updated, $pattern)
    if (!$match.Success) {
        throw "Receipt does not contain an explicit $pluginName state: $receiptPath"
    }

    if ($match.Groups[2].Value -ne "true") {
        if ($VerifyOnly) {
            throw "$pluginName is disabled in $receiptPath"
        }
        $updated = [Regex]::Replace($updated, $pattern, '${1}true', 1)
    }
}

if (!$VerifyOnly -and $updated -ne $content) {
    [System.IO.File]::WriteAllText(
        $receiptPath,
        $updated,
        [System.Text.UTF8Encoding]::new($false))
    Write-Host "Restored Daz plugin states in $receiptPath"
}

foreach ($pluginName in $pluginNames) {
    $enabledPattern = '(?s)"Name"\s*:\s*"' + [Regex]::Escape($pluginName) + '"\s*,\s*"Enabled"\s*:\s*true'
    if (![Regex]::IsMatch($updated, $enabledPattern)) {
        throw "Failed to enable $pluginName in $receiptPath"
    }
}

Write-Host "Daz editor receipt guard: PASS"
