[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$BaselinePath = 'D:\Projects UE5\NoShellForWinter\Docs\Migration\Evidence\Phase0_Source_Git_State_Before.json',
    [string]$OutputPath = 'D:\Projects UE5\NoShellForWinter\Saved\Migration\Evidence\SourceReadOnlyVerification.json'
)

$ErrorActionPreference = 'Stop'
$resolvedProject = (Resolve-Path -LiteralPath $ProjectRoot).Path
$resolvedSource = (Resolve-Path -LiteralPath $SourceRoot).Path
$resolvedBaseline = (Resolve-Path -LiteralPath $BaselinePath).Path
if ($resolvedSource -eq $resolvedProject -or $resolvedSource.StartsWith($resolvedProject + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Source invariant points inside target workspace: $resolvedSource"
}

$baseline = Get-Content -Raw -LiteralPath $resolvedBaseline | ConvertFrom-Json
$head = (git -C $resolvedSource rev-parse HEAD).Trim()
$status = @(git -C $resolvedSource status --porcelain=v2 --branch)
$expectedStatus = @($baseline.status_porcelain_v2 | ForEach-Object { [string]$_ })
$statusMatch = ($status.Count -eq $expectedStatus.Count) -and (-not (Compare-Object -ReferenceObject $expectedStatus -DifferenceObject $status -SyncWindow 0))

$hashResults = @()
foreach ($entry in $baseline.modified_file_hashes) {
    $path = Join-Path $resolvedSource ([string]$entry.path)
    $exists = Test-Path -LiteralPath $path -PathType Leaf
    $actualLength = if ($exists) { [int64](Get-Item -LiteralPath $path).Length } else { $null }
    $actualHash = if ($exists) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash } else { $null }
    $match = $exists -and $actualLength -eq [int64]$entry.length -and $actualHash -eq [string]$entry.sha256
    $hashResults += [pscustomobject]@{
        Path = [string]$entry.path
        ExpectedLength = [int64]$entry.length
        ActualLength = $actualLength
        ExpectedSHA256 = [string]$entry.sha256
        ActualSHA256 = $actualHash
        Result = if ($match) { 'PASS' } else { 'FAIL' }
    }
}
$modifiedHashesMatch = @($hashResults | Where-Object Result -ne 'PASS').Count -eq 0

$expectedLfsPath = [string]$baseline.lfs_manifest
$expectedLfs = @(Get-Content -LiteralPath $expectedLfsPath)
$currentLfs = @(git -C $resolvedSource lfs ls-files -l)
$lfsMatch = ($currentLfs.Count -eq $expectedLfs.Count) -and (-not (Compare-Object -ReferenceObject $expectedLfs -DifferenceObject $currentLfs -SyncWindow 0))

$pass = $head -eq [string]$baseline.head -and $statusMatch -and $modifiedHashesMatch -and $lfsMatch
$payload = [ordered]@{
    schema_version = 1
    verified_utc = [DateTime]::UtcNow.ToString('o')
    source_root = $resolvedSource
    baseline = $resolvedBaseline
    head_expected = [string]$baseline.head
    head_actual = $head
    head_match = $head -eq [string]$baseline.head
    status_match = $statusMatch
    modified_file_hashes_match = $modifiedHashesMatch
    modified_file_hashes = $hashResults
    lfs_entry_count_expected = [int]$baseline.lfs_entry_count
    lfs_entry_count_actual = $currentLfs.Count
    lfs_manifest_match = $lfsMatch
    pass = $pass
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$payload | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host ("Source read-only verification: {0}" -f $(if ($pass) { 'PASS' } else { 'FAIL' }))
Write-Host "Evidence: $OutputPath"
if (-not $pass) { exit 1 }
