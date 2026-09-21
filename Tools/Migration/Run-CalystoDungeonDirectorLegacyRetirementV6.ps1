[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*$')]
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss'),
    [switch]$Execute,
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$ExpectedV6GameplayHash,
    [string]$AcceptanceReceipt
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$expectedRoot = [IO.Path]::GetFullPath('D:\Projects UE5\NoShellForWinter')
if (-not $root.Equals($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to run outside the writable NoShellForWinter target: $root"
}
$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$script = Join-Path $root 'Tools\Migration\Retire-CalystoDungeonDirectorLegacyAssetsV6.py'
foreach ($required in @($launcher, $script, (Join-Path $root 'NoShellForWinter.uproject'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required V6 retirement path is missing: $required"
    }
}

if ($Execute) {
    if ([string]::IsNullOrWhiteSpace($ExpectedV6GameplayHash)) {
        throw '-Execute requires -ExpectedV6GameplayHash <64 hex>.'
    }
    if ([string]::IsNullOrWhiteSpace($AcceptanceReceipt)) {
        throw '-Execute requires -AcceptanceReceipt.'
    }
    $acceptance = (Resolve-Path -LiteralPath $AcceptanceReceipt).Path
    $savedMigration = [IO.Path]::GetFullPath((Join-Path $root 'Saved\Migration')).TrimEnd('\') + '\'
    if (-not $acceptance.StartsWith($savedMigration, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Acceptance receipt must be below Saved/Migration: $acceptance"
    }
} else {
    if ($PSBoundParameters.ContainsKey('ExpectedV6GameplayHash') -or
        $PSBoundParameters.ContainsKey('AcceptanceReceipt')) {
        throw 'Hash/acceptance arguments are valid only with -Execute.'
    }
    $acceptance = ''
}

$evidenceRoot = Join-Path $root 'Saved\Migration\CalystoDungeonDirectorV6'
[void][IO.Directory]::CreateDirectory($evidenceRoot)
$mode = if ($Execute) { 'Apply' } else { 'Audit' }
$evidence = Join-Path $evidenceRoot "LegacyRetirement${mode}_$Stamp.json"
if (Test-Path -LiteralPath $evidence) {
    throw "Refusing to overwrite V6 retirement evidence: $evidence"
}

$names = @(
    'CODEX_APPLY_CALYSTO_V6_LEGACY_RETIREMENT',
    'CODEX_EXPECTED_CALYSTO_V6_GAMEPLAY_HASH',
    'CODEX_CALYSTO_V6_ACCEPTANCE_RECEIPT',
    'CODEX_CALYSTO_V6_RETIREMENT_EVIDENCE'
)
$original = @{}
foreach ($name in $names) {
    $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    [Environment]::SetEnvironmentVariable(
        'CODEX_APPLY_CALYSTO_V6_LEGACY_RETIREMENT',
        $(if ($Execute) { '1' } else { '0' }),
        'Process')
    [Environment]::SetEnvironmentVariable(
        'CODEX_EXPECTED_CALYSTO_V6_GAMEPLAY_HASH',
        $(if ($Execute) { $ExpectedV6GameplayHash.ToUpperInvariant() } else { '' }),
        'Process')
    [Environment]::SetEnvironmentVariable(
        'CODEX_CALYSTO_V6_ACCEPTANCE_RECEIPT', $acceptance, 'Process')
    [Environment]::SetEnvironmentVariable(
        'CODEX_CALYSTO_V6_RETIREMENT_EVIDENCE', $evidence, 'Process')

    & powershell -NoProfile -ExecutionPolicy Bypass -File $launcher `
        -ProjectRoot $root `
        -PythonScript $script `
        -PythonTimeoutSeconds 300
    if ($LASTEXITCODE -ne 0) {
        throw "Protected V6 retirement launcher failed with exit code $LASTEXITCODE."
    }
}
finally {
    foreach ($name in $names) {
        [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process')
    }
}

if (-not (Test-Path -LiteralPath $evidence -PathType Leaf)) {
    throw "V6 retirement evidence was not produced: $evidence"
}
$receipt = Get-Content -Raw -LiteralPath $evidence | ConvertFrom-Json
$expectedStatus = if ($Execute) { 'PASS' } else { 'AUDIT_PASS_NO_DELETION' }
if ([string]$receipt.status -cne $expectedStatus) {
    throw "V6 retirement did not pass: status=$($receipt.status) evidence=$evidence"
}
Write-Host "Calysto V6 legacy retirement $mode`: $expectedStatus"
Write-Output $evidence
