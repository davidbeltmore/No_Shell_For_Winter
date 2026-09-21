[CmdletBinding()]
param([ValidatePattern('^[A-Za-z0-9_.-]+$')][string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'))

$ErrorActionPreference = 'Stop'
$root = 'D:\Projects UE5\NoShellForWinter'
$projectFile = Join-Path $root 'NoShellForWinter.uproject'
if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Close Unreal Editor before this read-only authoring gate.'
}
$runRoot = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV6\InlineAuthoring_$Stamp"
if (Test-Path -LiteralPath $runRoot) { throw "Evidence already exists: $runRoot" }
[void](New-Item -ItemType Directory -Path $runRoot)
$logPath = Join-Path $runRoot 'Editor.log'
$policyFile = Join-Path $root 'Content\_Game\Data\CalystoDungeon\V6\DA_CalystoDungeonDirectorPolicy.uasset'
$beforeHash = (Get-FileHash -LiteralPath $policyFile -Algorithm SHA256).Hash
$oldEnabled = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldScript = $env:CODEX_MIGRATION_PIE_SCRIPT
$result = [ordered]@{ status = 'FAIL'; project = $projectFile; policy_sha256 = $beforeHash; log = $logPath; error = '' }
try {
    $started = [DateTime]::UtcNow
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = Join-Path $root 'Tools\Migration\Validate-CalystoV6InlineAuthoring58.py'
    & (Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1') -Wait -AdditionalArguments (
        '-unattended -nop4 -nosplash -NoSound -NullRHI -NoAutoSave ' +
        '-DisablePlugins=BpGeneratorUltimate,MCPClientToolset,ModelContextProtocol ' +
        '-ABSLOG="' + $logPath + '"')
    if ((Get-FileHash -LiteralPath $policyFile -Algorithm SHA256).Hash -cne $beforeHash) {
        throw 'Read-only authoring validation changed the policy bytes.'
    }
    foreach ($name in @('CreateDirectorPolicyV6.json', 'ValidateCookClosureV6.json')) {
        $receiptPath = Join-Path $root "Saved\Migration\CalystoDungeonDirectorV6\$name"
        if ((Get-Item -LiteralPath $receiptPath).LastWriteTimeUtc -lt $started) { throw "Stale receipt: $name" }
        $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        if ($receipt.status -cne 'PASS') { throw "Failed receipt: $name" }
    }
    if (Select-String -LiteralPath $logPath -Pattern 'Fatal error!|Critical error:|Traceback \(most recent|EXCEPTION_ACCESS_VIOLATION' -Quiet) {
        throw 'The authoring Editor log contains a fatal error or Python exception.'
    }
    $result.status = 'PASS'
}
catch { $result.error = $_.Exception.Message }
finally {
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldEnabled
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldScript
    $result.generated_utc = [DateTime]::UtcNow.ToString('o')
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runRoot 'StrictSummary.json') -Encoding UTF8
}
if ($result.status -cne 'PASS') { throw $result.error }
Write-Host 'Calysto V6 inline authoring and clean Editor exit: PASS'
