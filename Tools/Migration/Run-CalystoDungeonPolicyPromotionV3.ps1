[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MatrixReceipt,
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$OutputPath = "",
    [string]$LogPath = ""
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$savedRoot = (Resolve-Path -LiteralPath (Join-Path $ProjectRoot 'Saved')).Path
$MatrixReceipt = (Resolve-Path -LiteralPath $MatrixReceipt).Path
$launcher = Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$pythonScript = Join-Path $ProjectRoot 'Tools\Migration\Promote-CalystoDungeonValidatedSizesV3.py'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $savedRoot "Migration\CalystoDungeonDirectorV3\PolicyPromotion_$stamp.json"
}
if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogPath = Join-Path $savedRoot "Migration\Logs\CalystoDungeonDirectorV3PolicyPromotion_$stamp.log"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$LogPath = [System.IO.Path]::GetFullPath($LogPath)
foreach ($path in @($launcher, $pythonScript, $MatrixReceipt)) {
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required V3 policy-promotion path is missing: $path"
    }
}
foreach ($path in @($MatrixReceipt, $OutputPath, $LogPath)) {
    if (!$path.StartsWith($savedRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "V3 promotion evidence must stay under Saved: $path"
    }
}

$oldReceipt = $env:CODEX_CALYSTO_POLICY_PROMOTION_RECEIPT
$oldOutput = $env:CODEX_CALYSTO_POLICY_PROMOTION_OUTPUT
$oldAutoClose = $env:CODEX_CALYSTO_POLICY_PROMOTION_AUTOCLOSE
$oldRunMigration = $env:CODEX_RUN_MIGRATION_BASELINE_PIE
$oldMigrationScript = $env:CODEX_MIGRATION_PIE_SCRIPT
try {
    $env:CODEX_CALYSTO_POLICY_PROMOTION_RECEIPT = $MatrixReceipt
    $env:CODEX_CALYSTO_POLICY_PROMOTION_OUTPUT = $OutputPath
    $env:CODEX_CALYSTO_POLICY_PROMOTION_AUTOCLOSE = '1'
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    $env:CODEX_MIGRATION_PIE_SCRIPT = $pythonScript
    $arguments = '-unattended -nop4 -nosplash -NullRHI -NoSound -stdout ' +
        '-FullStdOutLogOutput -ABSLOG="' + $LogPath + '"'
    & $launcher -ProjectRoot $ProjectRoot -AdditionalArguments $arguments -Wait
}
finally {
    $env:CODEX_CALYSTO_POLICY_PROMOTION_RECEIPT = $oldReceipt
    $env:CODEX_CALYSTO_POLICY_PROMOTION_OUTPUT = $oldOutput
    $env:CODEX_CALYSTO_POLICY_PROMOTION_AUTOCLOSE = $oldAutoClose
    $env:CODEX_RUN_MIGRATION_BASELINE_PIE = $oldRunMigration
    $env:CODEX_MIGRATION_PIE_SCRIPT = $oldMigrationScript
}

if (!(Test-Path -LiteralPath $OutputPath -PathType Leaf)) {
    throw "V3 policy promotion did not produce its receipt: $OutputPath"
}
$result = Get-Content -LiteralPath $OutputPath -Raw | ConvertFrom-Json
if ([string]$result.status -ne 'PASS') {
    throw "V3 policy promotion failed: $([string]$result.error)"
}
$mutations = @($result.asset_mutations)
if ($mutations.Count -ne 1 -or
    [string]$mutations[0] -ne '/Game/_Game/Data/CalystoDungeon/V3/DA_CalystoDungeonDirectorPolicy') {
    throw "V3 policy promotion reported an invalid Content delta: $($mutations -join ', ')"
}
if (@($result.calysto_asset_mutations).Count -ne 0 -or
    [string]$result.after.policy_hash -ne [string]$result.candidate.policy_hash -or
    (@($result.after.validated_dungeon_sizes) -join ',') -ne
        (@($result.candidate.validated_dungeon_sizes) -join ',')) {
    throw 'V3 policy promotion receipt failed its post-save identity/hash contract.'
}

Write-Host 'Calysto Dungeon Director V3 policy promotion: PASS'
Write-Host "Validated sizes: $(@($result.after.validated_dungeon_sizes) -join ', ')"
Write-Host "PolicyHash: $([string]$result.after.policy_hash)"
Write-Host "Evidence: $OutputPath"
Write-Host "Log: $LogPath"
