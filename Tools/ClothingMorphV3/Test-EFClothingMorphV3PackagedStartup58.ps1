[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArchiveRoot,
    [string]$ProjectRoot = '',
    [string]$MapPath = '/Game/_Game/Hub/HUB',
    [ValidateRange(5, 60)]
    [int]$ObservationSeconds = 10,
    [string]$Stamp = (Get-Date -Format 'yyyyMMdd_HHmmss')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ArchiveRoot = (Resolve-Path -LiteralPath $ArchiveRoot).Path
$expectedArchiveParent = [IO.Path]::GetFullPath(
    (Join-Path $ProjectRoot 'Saved\Migration\CalystoDungeonDirectorV4\Packages')
)
if (-not ($ArchiveRoot + '\').StartsWith($expectedArchiveParent + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Archive escaped the validated project package root: $ArchiveRoot"
}

$executable = Join-Path $ArchiveRoot 'Windows\NoShellForWinter\Binaries\Win64\NoShellForWinter.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Packaged executable is missing: $executable"
}
$existing = @(Get-CimInstance Win32_Process -Filter "Name='NoShellForWinter.exe'")
if ($existing.Count -ne 0) {
    throw "Close existing NoShellForWinter process(es) before isolated startup QA: $($existing.ProcessId -join ', ')"
}

$runDirectory = Join-Path $ProjectRoot "Saved\ClothingMorphV3QA\PackagedStartup_$Stamp"
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
$logPath = Join-Path $runDirectory 'NoShellForWinter.log'
$summaryPath = Join-Path $runDirectory 'Summary.json'
$arguments = @(
    'NoShellForWinter',
    $MapPath,
    '-d3d12',
    '-sm6',
    '-windowed',
    '-ResX=1280',
    '-ResY=720',
    '-nosplash',
    '-NoSound',
    '-log',
    ('-abslog="{0}"' -f $logPath)
)

$process = $null
$summary = [ordered]@{
    schema_version = 1
    status = 'UE58_EF_CLOTHING_MORPH_V3_PACKAGED_STARTUP_FAIL'
    project = (Join-Path $ProjectRoot 'NoShellForWinter.uproject')
    archive = $ArchiveRoot
    executable = $executable
    map = $MapPath
    renderer = 'D3D12_SM6'
    observation_seconds = $ObservationSeconds
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    finished_utc = $null
    pid = $null
    remained_alive = $false
    map_loaded = $false
    map_load_seconds = $null
    critical_log_matches = @()
    cleanup = 'NOT_STARTED'
    log = $logPath
}

try {
    $process = Start-Process -FilePath $executable -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $executable) -WindowStyle Hidden -PassThru
    $summary.pid = $process.Id
    $deadline = (Get-Date).AddSeconds($ObservationSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    $summary.remained_alive = -not $process.HasExited
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        $logText = Get-Content -Raw -LiteralPath $logPath
        $mapMatch = [regex]::Match(
            $logText,
            'LoadMap:\s*/Game/_Game/Hub/HUB(?:\s+in\s+([0-9.]+)\s+seconds)?',
            [Text.RegularExpressions.RegexOptions]::IgnoreCase
        )
        $summary.map_loaded = $mapMatch.Success
        if ($mapMatch.Success -and $mapMatch.Groups[1].Success) {
            $summary.map_load_seconds = [double]::Parse(
                $mapMatch.Groups[1].Value,
                [Globalization.CultureInfo]::InvariantCulture
            )
        }
        $criticalPatterns = @(
            'Fatal error:',
            'Assertion failed:',
            'GPU Crashed',
            'Failed to load package /Game/_Game/Hub/HUB',
            'Failed to enter /Game/_Game/Hub/HUB'
        )
        $summary.critical_log_matches = @(
            foreach ($pattern in $criticalPatterns) {
                if ($logText.IndexOf($pattern, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    $pattern
                }
            }
        )
    }
    if (-not $summary.remained_alive) {
        throw 'Packaged game exited before the observation window completed.'
    }
    if (-not $summary.map_loaded) {
        throw 'Packaged game did not report loading HUB.'
    }
    if ($summary.critical_log_matches.Count -ne 0) {
        throw "Packaged game logged critical startup failures: $($summary.critical_log_matches -join ', ')"
    }
    $summary.status = 'UE58_EF_CLOTHING_MORPH_V3_PACKAGED_STARTUP_PASS'
}
finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        $process.WaitForExit(5000) | Out-Null
    }
    $summary.cleanup = if ($null -eq $process -or $process.HasExited) {
        'OWNED_PROCESS_EXITED'
    }
    else {
        'OWNED_PROCESS_STILL_RUNNING'
    }
    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
    $summary | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
}

if ($summary.status -ne 'UE58_EF_CLOTHING_MORPH_V3_PACKAGED_STARTUP_PASS') {
    throw "Packaged startup QA failed. Summary: $summaryPath"
}
Write-Output "Packaged startup QA: PASS"
Write-Output $summaryPath
