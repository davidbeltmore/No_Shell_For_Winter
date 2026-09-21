param(
    [Parameter(Mandatory)][ValidateSet('BuildEditor','InspectUI','AuthorUI','AuthorWorld','NativeTests','PIE')][string]$Action,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)
$ErrorActionPreference = 'Stop'
$equipmentRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not (Test-Path -LiteralPath (Join-Path $equipmentRoot 'NoShellForWinter.uproject'))) {
    throw 'This workbench must remain inside the NoShellForWinter target project.'
}
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count) {
    throw 'Close the target editor safely before this action; preserve any unsaved user work first.'
}
$runDirectory = Join-Path $equipmentRoot "Saved/EquipmentQA/$Stamp"
if (Test-Path -LiteralPath $runDirectory) { throw 'Use a fresh Stamp to avoid accepting stale evidence.' }
New-Item -ItemType Directory -Path $runDirectory | Out-Null
Push-Location $equipmentRoot
try {
    switch ($Action) {
        'BuildEditor' {
            & 'Tools/Migration/Build-NoShellForWinterEditor58.ps1' *> (Join-Path $runDirectory 'build.log')
        }
        'InspectUI' {
            $actionStarted = Get-Date
            & 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1' -PythonScript (Join-Path $PSScriptRoot 'Inspect-InventoryUI.py') *> (Join-Path $runDirectory 'inspect.log')
            if ((Get-Item -LiteralPath 'Saved/EquipmentQA/inventory_ui.txt').LastWriteTime -lt $actionStarted) { throw 'Inspector did not publish fresh evidence.' }
            Copy-Item -LiteralPath 'Saved/EquipmentQA/inventory_ui.txt' -Destination $runDirectory
            Copy-Item -LiteralPath 'Saved/Logs/NoShellForWinter.log' -Destination (Join-Path $runDirectory 'UnrealEditor.log')
        }
        'AuthorUI' {
            $actionStarted = Get-Date
            & 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1' -PythonScript (Join-Path $PSScriptRoot 'Author-InventoryUI.py') *> (Join-Path $runDirectory 'author.log')
            if ((Get-Item -LiteralPath 'Saved/EquipmentQA/author_inventory_ui.json').LastWriteTime -lt $actionStarted) { throw 'Author did not publish a fresh receipt.' }
            $receipt = Get-Content -Raw -LiteralPath 'Saved/EquipmentQA/author_inventory_ui.json' | ConvertFrom-Json
            Copy-Item -LiteralPath 'Saved/EquipmentQA/author_inventory_ui.json' -Destination $runDirectory
            Copy-Item -LiteralPath 'Saved/Logs/NoShellForWinter.log' -Destination (Join-Path $runDirectory 'UnrealEditor.log')
            if ($receipt.result -notlike 'PASS:*') { throw "UI authoring failed: $($receipt.result)" }
        }
        'NativeTests' {
            & (Join-Path $PSScriptRoot 'Run-EquipmentNativeTests.ps1') -Stamp $Stamp -TimeoutSeconds 120 *> (Join-Path $runDirectory 'automation.log')
        }
        'AuthorWorld' {
            $actionStarted = Get-Date
            & 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1' -PythonScript (Join-Path $PSScriptRoot 'Author-WorldActors.py') *> (Join-Path $runDirectory 'author.log')
            if ((Get-Item -LiteralPath 'Saved/EquipmentQA/author_world_actors.json').LastWriteTime -lt $actionStarted) { throw 'World authoring did not publish a fresh receipt.' }
            $receipt = Get-Content -Raw -LiteralPath 'Saved/EquipmentQA/author_world_actors.json' | ConvertFrom-Json
            Copy-Item -LiteralPath 'Saved/EquipmentQA/author_world_actors.json' -Destination $runDirectory
            Copy-Item -LiteralPath 'Saved/Logs/NoShellForWinter.log' -Destination (Join-Path $runDirectory 'UnrealEditor.log')
            if ($receipt.result -notlike 'PASS:*') { throw "World authoring failed: $($receipt.result)" }
        }
        'PIE' {
            & (Join-Path $PSScriptRoot 'Launch-InventoryPIE.ps1') -RunName $Stamp
            Write-Host "PENDING: inspect $runDirectory/result.json and capture the real menu before writing visual_done.txt."
        }
    }
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "Equipment action failed with exit code $LASTEXITCODE" }
    Write-Host "Equipment $Action artifacts: $runDirectory"
} finally { Pop-Location }
