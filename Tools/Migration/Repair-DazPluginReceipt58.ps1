param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$TargetName = "NoShellForWinterEditor",
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development",
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
$receiptFileName = if ($TargetName -eq "NoShellForWinter" -and $Configuration -eq "Shipping") {
    "NoShellForWinter-Win64-Shipping.target"
}
else {
    "${TargetName}.target"
}
$receiptPath = Join-Path $ProjectRoot ("Binaries\Win64\{0}" -f $receiptFileName)
$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"

foreach ($requiredPath in @($receiptPath, $projectPath)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Daz receipt guard input not found: $requiredPath"
    }
}

$content = [System.IO.File]::ReadAllText($receiptPath)
$updated = $content
$pluginNames = @("DazToUnreal", "EFCharacterCreationDazBridge")
$requiredProjectPluginNames = $pluginNames + @("NeuralMorphModel")
$projectDescriptor = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json

# The project descriptor is the persistent authority. Both integrations must
# remain enabled even though the synchronous Game UBT invocation temporarily
# excludes their editor-only modules.
foreach ($pluginName in $requiredProjectPluginNames) {
    $projectPlugin = @($projectDescriptor.Plugins | Where-Object { $_.Name -eq $pluginName })
    if ($projectPlugin.Count -ne 1 -or ![bool]$projectPlugin[0].Enabled) {
        throw "$pluginName is not explicitly enabled in $projectPath"
    }
}

if (
    !$VerifyOnly -and
    $TargetName -eq "NoShellForWinter" -and
    ![Regex]::IsMatch($updated, '(?m)^\s*"Plugins"\s*:')
) {
    $buildPluginsPattern = '(?m)^(?<indent>\s*)"BuildPlugins"\s*:\s*\['
    $buildPluginsMatch = [Regex]::Match($updated, $buildPluginsPattern)
    if (!$buildPluginsMatch.Success) {
        throw "Game receipt has neither Plugins nor BuildPlugins insertion point: $receiptPath"
    }
    $indent = $buildPluginsMatch.Groups['indent'].Value
    $pluginBlock = $indent + '"Plugins": [' + [Environment]::NewLine +
        $indent + "`t{" + [Environment]::NewLine +
        $indent + "`t`t`"Name`": `"DazToUnreal`"," + [Environment]::NewLine +
        $indent + "`t`t`"Enabled`": true" + [Environment]::NewLine +
        $indent + "`t}," + [Environment]::NewLine +
        $indent + "`t{" + [Environment]::NewLine +
        $indent + "`t`t`"Name`": `"EFCharacterCreationDazBridge`"," + [Environment]::NewLine +
        $indent + "`t`t`"Enabled`": true" + [Environment]::NewLine +
        $indent + "`t}" + [Environment]::NewLine +
        $indent + '],' + [Environment]::NewLine +
        $indent + '"BuildPlugins": ['
    $updated = [Regex]::Replace($updated, $buildPluginsPattern, $pluginBlock, 1)
}

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

# A Game target has no Daz editor modules to link, so UBT does not emit either
# descriptor while the temporary exclusion is active. They are still required
# at packaged startup: DazToUnreal owns cooked content mount points and the
# project bridge declares the dependency. Stage only the descriptors as UFS;
# no vendor source, binary or descriptor is modified.
$gameDescriptorDependencies = [ordered]@{
    "DazToUnreal" = '$(EngineDir)/Plugins/DazToUnreal/DazToUnreal.uplugin'
    "EFCharacterCreationDazBridge" = '$(ProjectDir)/Plugins/EFCharacterCreationDazBridge/EFCharacterCreationDazBridge.uplugin'
}

if ($TargetName -eq "NoShellForWinter") {
    foreach ($entry in $gameDescriptorDependencies.GetEnumerator()) {
        $dependencyPattern = '(?s)"Path"\s*:\s*"' + [Regex]::Escape($entry.Value) + '"\s*,\s*"Type"\s*:\s*"UFS"'
        if (![Regex]::IsMatch($updated, $dependencyPattern)) {
            if ($VerifyOnly) {
                throw "$($entry.Key) descriptor is not staged as a Game UFS dependency in $receiptPath"
            }

            $arrayPattern = '(?m)^(?<indent>\s*)"RuntimeDependencies"\s*:\s*\['
            $arrayMatch = [Regex]::Match($updated, $arrayPattern)
            if (!$arrayMatch.Success) {
                throw "Game receipt has no RuntimeDependencies insertion point: $receiptPath"
            }

            $indent = $arrayMatch.Groups['indent'].Value
            $itemIndent = $indent + "`t"
            $fieldIndent = $itemIndent + "`t"
            $replacement = $indent + '"RuntimeDependencies": [' + [Environment]::NewLine +
                $itemIndent + '{' + [Environment]::NewLine +
                $fieldIndent + '"Path": "' + $entry.Value + '",' + [Environment]::NewLine +
                $fieldIndent + '"Type": "UFS"' + [Environment]::NewLine +
                $itemIndent + '},'
            $updated = [Regex]::Replace($updated, $arrayPattern, $replacement, 1)
        }
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


if ($TargetName -eq "NoShellForWinter") {
    foreach ($entry in $gameDescriptorDependencies.GetEnumerator()) {
        $dependencyPattern = '(?s)"Path"\s*:\s*"' + [Regex]::Escape($entry.Value) + '"\s*,\s*"Type"\s*:\s*"UFS"'
        if (![Regex]::IsMatch($updated, $dependencyPattern)) {
            throw "Failed to stage $($entry.Key) descriptor in $receiptPath"
        }
    }
}

Write-Host "Daz receipt guard for ${TargetName} ${Configuration}: PASS"
