[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$BaselinePath = 'D:\Projects UE5\NoShellForWinter\Docs\Migration\Evidence\Phase0_Target_Invariant_Hashes.json',
    [string]$OutputPath = 'D:\Projects UE5\NoShellForWinter\Saved\Migration\Evidence\ProtectedInvariantVerification.json'
)

$ErrorActionPreference = 'Stop'

$resolvedProject = (Resolve-Path -LiteralPath $ProjectRoot).Path
$resolvedBaseline = (Resolve-Path -LiteralPath $BaselinePath).Path
if (-not $resolvedBaseline.StartsWith($resolvedProject + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Baseline manifest must be inside the target workspace: $resolvedBaseline"
}

$baseline = Get-Content -Raw -LiteralPath $resolvedBaseline | ConvertFrom-Json
$setResults = @()
$allMismatches = [System.Collections.Generic.List[object]]::new()

foreach ($set in $baseline.sets) {
    $root = (Resolve-Path -LiteralPath ([string]$set.root)).Path.TrimEnd('\')
    $manifestPath = (Resolve-Path -LiteralPath ([string]$set.manifest)).Path
    $manifestHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $manifestPath).Hash
    if ($manifestHash -ne [string]$set.manifest_sha256) {
        $allMismatches.Add([pscustomobject]@{
            Set = [string]$set.name
            RelativePath = '<baseline-manifest>'
            Kind = 'BASELINE_MANIFEST_HASH_CHANGED'
            Expected = [string]$set.manifest_sha256
            Actual = $manifestHash
        })
    }

    $expectedRows = @(Import-Csv -LiteralPath $manifestPath)
    $expectedPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $setMismatchStart = $allMismatches.Count

    foreach ($row in $expectedRows) {
        $relative = [string]$row.RelativePath
        [void]$expectedPaths.Add($relative)
        $path = Join-Path $root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $allMismatches.Add([pscustomobject]@{
                Set = [string]$set.name
                RelativePath = $relative
                Kind = 'MISSING'
                Expected = [string]$row.SHA256
                Actual = $null
            })
            continue
        }

        $item = Get-Item -LiteralPath $path
        if ([int64]$item.Length -ne [int64]$row.Length) {
            $allMismatches.Add([pscustomobject]@{
                Set = [string]$set.name
                RelativePath = $relative
                Kind = 'LENGTH_CHANGED'
                Expected = [int64]$row.Length
                Actual = [int64]$item.Length
            })
            continue
        }

        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
        if ($actualHash -ne [string]$row.SHA256) {
            $allMismatches.Add([pscustomobject]@{
                Set = [string]$set.name
                RelativePath = $relative
                Kind = 'HASH_CHANGED'
                Expected = [string]$row.SHA256
                Actual = $actualHash
            })
        }
    }

    $currentFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force)
    foreach ($file in $currentFiles) {
        $relative = $file.FullName.Substring($root.Length).TrimStart('\')
        if (-not $expectedPaths.Contains($relative)) {
            $allMismatches.Add([pscustomobject]@{
                Set = [string]$set.name
                RelativePath = $relative
                Kind = 'UNEXPECTED_FILE'
                Expected = $null
                Actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
            })
        }
    }

    $setResults += [pscustomobject]@{
        Name = [string]$set.name
        Root = $root
        ExpectedFileCount = [int]$set.file_count
        CurrentFileCount = $currentFiles.Count
        MismatchCount = $allMismatches.Count - $setMismatchStart
        Result = if (($allMismatches.Count - $setMismatchStart) -eq 0) { 'PASS' } else { 'FAIL' }
    }
    Write-Host ("{0}: {1} files, {2}" -f $set.name, $currentFiles.Count, $setResults[-1].Result)
}

$assetResults = @()
foreach ($asset in $baseline.authoritative_assets) {
    $path = [string]$asset.path
    $exists = Test-Path -LiteralPath $path -PathType Leaf
    $actualLength = $null
    $actualHash = $null
    if ($exists) {
        $item = Get-Item -LiteralPath $path
        $actualLength = [int64]$item.Length
        $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    }
    $matches = $exists -and $actualLength -eq [int64]$asset.length -and $actualHash -eq [string]$asset.sha256
    if (-not $matches) {
        $allMismatches.Add([pscustomobject]@{
            Set = 'authoritative_assets'
            RelativePath = $path
            Kind = 'AUTHORITATIVE_ASSET_CHANGED'
            Expected = [string]$asset.sha256
            Actual = $actualHash
        })
    }
    $assetResults += [pscustomobject]@{
        Path = $path
        ExpectedLength = [int64]$asset.length
        ActualLength = $actualLength
        ExpectedSHA256 = [string]$asset.sha256
        ActualSHA256 = $actualHash
        Result = if ($matches) { 'PASS' } else { 'FAIL' }
    }
}

$payload = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    baseline = $resolvedBaseline
    result = if ($allMismatches.Count -eq 0) { 'PASS' } else { 'FAIL' }
    sets = $setResults
    authoritative_assets = $assetResults
    mismatch_count = $allMismatches.Count
    mismatches = @($allMismatches)
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Evidence: $OutputPath"

if ($allMismatches.Count -ne 0) {
    exit 1
}
