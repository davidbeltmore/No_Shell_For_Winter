[CmdletBinding()]
param([ValidateRange(30,120)][int]$TimeoutSeconds = 120)
$ErrorActionPreference = 'Stop'
$runner = Join-Path $PSScriptRoot 'Run-EFClothingMorphV5NativeAutomation58.ps1'
foreach ($test in @('EF.ClothingMorph.Unisex.BodyVariants', 'EF.ClothingMorph.V51.Contracts')) {
    & $runner -TestPath $test -TimeoutSeconds $TimeoutSeconds
    if (!$?) { throw "Native clothing contract failed: $test" }
}
