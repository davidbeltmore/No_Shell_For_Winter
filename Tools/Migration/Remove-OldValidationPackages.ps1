param([switch]$Apply, [string]$KeepPackagePath = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if ($root -ne 'D:\Projects UE5\NoShellForWinter') { throw 'Unexpected project root.' }
$prefix = $root + '\'
$evidence = Join-Path $root 'Saved\CleanupEvidence\20261008'
if (-not $KeepPackagePath) {
    $KeepPackagePath = (Get-Content -LiteralPath (Join-Path $root 'Saved\BodyPresetsQA\latest-package.txt') -Raw).Trim()
}
$keep = (Resolve-Path -LiteralPath $KeepPackagePath).Path
if (-not $keep.StartsWith((Join-Path $root 'Saved\BodyPresetsQA\Package_'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Retained package must be a target body-preset package.' }
$receipt = Get-Content -LiteralPath (Join-Path $keep 'package-result.json') -Raw | ConvertFrom-Json
if ($receipt.exit_code -ne 0) { throw 'Refusing cleanup while latest package has not cooked successfully.' }
$targets = @()
function Assert-NoReparsePath([string]$Path) {
    $cursor = Get-Item -LiteralPath $Path -Force
    while ($cursor.FullName -ne $root) {
        if ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse ancestor: $($cursor.FullName)" }
        $cursor = $cursor.Parent
        if ($null -eq $cursor) { throw 'Path ancestry escaped project.' }
    }
}
foreach ($parent in @('Saved\Analysis','Saved\Migration','Saved\BodyPresetsQA')) {
    $packages = Get-ChildItem -LiteralPath (Join-Path $root $parent) -Directory -Recurse |
        Where-Object { $_.Name -match '^Package_\d{8}_\d{6}$' }
    foreach ($package in $packages) {
        if ($package.FullName -eq $keep) { continue }
        foreach ($name in @('Cooked','Staged')) {
            $candidate = Join-Path $package.FullName $name
            if (-not (Test-Path -LiteralPath $candidate)) { continue }
            $resolved = (Resolve-Path -LiteralPath $candidate).Path
            if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Escaped root: $resolved" }
            Assert-NoReparsePath $resolved
            $items = @(Get-ChildItem -LiteralPath $resolved -Recurse -Force)
            if (@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw "Reparse point in $resolved" }
            if ((Get-Item -LiteralPath $resolved).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse root: $resolved" }
            $files = @($items | Where-Object { -not $_.PSIsContainer })
            $targets += [pscustomobject]@{Path=$resolved; Bytes=[long](($files | Measure-Object Length -Sum).Sum); Files=$files.Count}
        }
    }
}
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$planName = 'package-cleanup-plan_' + (Get-Date -Format 'yyyyMMdd_HHmmssfff') + '.json'
$targets | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence $planName) -Encoding UTF8
if ($Apply) {
    $active = @(Get-CimInstance Win32_Process | Where-Object {
        $_.Name -match '^(UnrealEditor-Cmd|AutomationTool|UnrealPak|NoShellForWinter)\.exe$' -or
        ($_.Name -eq 'dotnet.exe' -and $_.CommandLine -match 'AutomationTool')
    })
    if ($active.Count) { throw 'Packaging tools are active.' }
    $quarantine = Join-Path $root 'Saved\CleanupQuarantine\20261008_old_packages'
    if (Test-Path -LiteralPath $quarantine) { throw 'Quarantine already exists; inspect it first.' }
    New-Item -ItemType Directory -Path $quarantine -Force | Out-Null
    Assert-NoReparsePath $quarantine
    foreach ($target in $targets) {
        $relative = $target.Path.Substring($prefix.Length)
        $classification = & python 'A:/Skills Codex/project-safety-guard/scripts/assert_project_safe_op.py' move $relative --project-root $root
        if ($LASTEXITCODE -ne 0) { throw "Safety classifier rejected $relative : $classification" }
        Move-Item -LiteralPath $target.Path -Destination (Join-Path $quarantine ($relative -replace '[\\/:]', '__'))
    }
    $resolvedQuarantine = (Resolve-Path -LiteralPath $quarantine).Path
    if (-not $resolvedQuarantine.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Quarantine escaped root.' }
    Remove-Item -LiteralPath $resolvedQuarantine -Recurse -Force
}
[pscustomobject]@{Applied=[bool]$Apply; Targets=$targets.Count; Bytes=[long](($targets | Measure-Object Bytes -Sum).Sum); RetainedPackage=$keep} | ConvertTo-Json
