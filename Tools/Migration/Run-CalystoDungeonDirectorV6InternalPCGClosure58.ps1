[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$expectedRoot = [IO.Path]::GetFullPath('D:\Projects UE5\NoShellForWinter')
if (-not $root.Equals($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing Calysto V6 internal PCG creation outside the target: $root"
}

$launcher = Join-Path $root 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$creator = Join-Path $root `
    'Tools\Migration\Create-CalystoDungeonDirectorV6InternalPCGClosure.py'
$receipt = Join-Path $root `
    'Saved\Migration\CalystoDungeonDirectorV6\CreateInternalPCGClosureV6.json'
$expectedPackages = @(
    '/EFProcedural/Calysto/Internal/PCG/PCG_AddRampsCookedSafe',
    '/EFProcedural/Calysto/Internal/PCG/PCG_SetDungeonMeshCookedSafe'
)
foreach ($path in @($launcher, $creator)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required Calysto V6 internal PCG tool is missing: $path"
    }
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $launcher `
    -ProjectRoot $root `
    -EngineRoot $EngineRoot `
    -PythonScript $creator `
    -PythonTimeoutSeconds 300
if ($LASTEXITCODE -ne 0) {
    throw "Calysto V6 internal PCG creator exited with code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $receipt -PathType Leaf)) {
    throw "Calysto V6 internal PCG creator emitted no receipt: $receipt"
}
$data = Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json
if ([string]$data.status -cne 'PASS' -or
    @($data.vendor_assets_saved).Count -ne 0 -or
    @($data.assets.PSObject.Properties).Count -ne 2 -or
    @(Compare-Object -CaseSensitive -ReferenceObject $expectedPackages `
        -DifferenceObject @($data.assets.PSObject.Properties.Name)).Count -ne 0) {
    throw "Calysto V6 internal PCG receipt is not exact: $receipt"
}
if ([string]$data.mode -ceq 'VALIDATE_EXISTING_READ_ONLY') {
    if (@($data.saved).Count -ne 0 -or @($data.asset_mutations).Count -ne 0 -or
        -not [bool]$data.idempotent_read_only) {
        throw 'Existing internal PCG closure validation was not read-only.'
    }
}
elseif ([string]$data.mode -ceq 'CREATE_ONCE_FROM_FROZEN_VENDOR_GRAPHS') {
    if (@($data.saved).Count -ne 2 -or @($data.asset_mutations).Count -ne 2) {
        throw 'First internal PCG closure creation did not save exactly two assets.'
    }
}
else {
    throw "Unexpected internal PCG creator mode: $($data.mode)"
}

Write-Output (
    "Calysto V6 internal PCG closure: PASS mode=$($data.mode) " +
    "assets=$(@($data.assets.PSObject.Properties).Count) vendorAssetsSaved=0")
