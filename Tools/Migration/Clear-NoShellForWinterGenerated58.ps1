<#
.SYNOPSIS
    Audits, and only with -Apply removes, a closed set of regenerable UE 5.8 outputs.

.DESCRIPTION
    The default mode is an audit: it inventories the approved generated directories and
    writes an evidence manifest without removing generated output.  -Apply is deliberately
    required for deletion.  This script never uses destructive Git cleanup commands and never targets .git,
    Content, Config, Source, Tools, or plugin source/content.

    Every destructive target is resolved beneath ProjectRoot, checked against the internal
    allowlist, checked for Git-tracked/local-change collisions, and rejected if it or a
    descendant is a reparse point.  Directory traversal/removal uses extended-length paths
    so deeply nested cook and package artifacts remain removable on Windows.

.EXAMPLE
    .\Tools\Migration\Clear-NoShellForWinterGenerated58.ps1

.EXAMPLE
    .\Tools\Migration\Clear-NoShellForWinterGenerated58.ps1 -Apply
#>

[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

function Get-NormalizedFullPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath.Length -gt 3) {
        $fullPath = $fullPath.TrimEnd([char[]]@('\', '/'))
    }
    return $fullPath
}

function ConvertTo-ExtendedLengthPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $fullPath = Get-NormalizedFullPath -Path $Path
    if ($fullPath.StartsWith('\\?\', [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath
    }
    if ($fullPath.StartsWith('\\', [System.StringComparison]::OrdinalIgnoreCase)) {
        return '\\?\UNC\' + $fullPath.Substring(2)
    }
    return '\\?\' + $fullPath
}

function ConvertFrom-ExtendedLengthPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    if ($Path.StartsWith('\\?\UNC\', [System.StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $Path.Substring(8)
    }
    if ($Path.StartsWith('\\?\', [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring(4)
    }
    return $Path
}

function Test-IsSameOrDescendantPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Root
    )

    $candidate = Get-NormalizedFullPath -Path $Path
    $normalizedRoot = Get-NormalizedFullPath -Path $Root
    if ($candidate.Equals($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    return $candidate.StartsWith(
        $normalizedRoot + '\',
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-DirectoryExistsLongPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    return [System.IO.Directory]::Exists((ConvertTo-ExtendedLengthPath -Path $Path))
}

function Get-DirectoryMetricsLongPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $metrics = [ordered]@{
        directory_count = [int64]0
        file_count = [int64]0
        byte_count = [int64]0
        reparse_point_count = [int64]0
        asset_like_file_count = [int64]0
    }
    $stack = New-Object 'System.Collections.Generic.Stack[string]'
    $stack.Push((ConvertTo-ExtendedLengthPath -Path $Path))

    while ($stack.Count -gt 0) {
        $directory = $stack.Pop()
        $directoryAttributes = [System.IO.File]::GetAttributes($directory)
        if (($directoryAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            $metrics.reparse_point_count++
            continue
        }

        $metrics.directory_count++
        try {
            foreach ($entry in [System.IO.Directory]::EnumerateFileSystemEntries($directory)) {
                $attributes = [System.IO.File]::GetAttributes($entry)
                if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                    $metrics.reparse_point_count++
                    continue
                }

                if (($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
                    $stack.Push($entry)
                    continue
                }

                $fileInfo = [System.IO.FileInfo]::new($entry)
                $metrics.file_count++
                $metrics.byte_count += [int64]$fileInfo.Length
                $extension = [System.IO.Path]::GetExtension($entry)
                if ($extension -ieq '.uasset' -or $extension -ieq '.umap') {
                    $metrics.asset_like_file_count++
                }
            }
        }
        catch {
            throw "Could not inventory generated directory '$([string](ConvertFrom-ExtendedLengthPath -Path $directory))': $($_.Exception.Message)"
        }
    }

    return [pscustomobject]$metrics
}

function Get-GitState {
    param(
        [Parameter(Mandatory)]
        [string]$Root
    )

    $gitCommand = Get-Command git -ErrorAction SilentlyContinue
    if ($null -eq $gitCommand) {
        return [pscustomobject]@{
            available = $false
            repository = $false
            reason = 'git executable is unavailable'
            executable = $null
            head = $null
            status_entry_count = $null
            status_porcelain = @()
        }
    }

    $insideRepository = @(& $gitCommand.Source -C $Root rev-parse --is-inside-work-tree 2>$null)
    $insideExitCode = $LASTEXITCODE
    if ($insideExitCode -ne 0 -or $insideRepository.Count -ne 1 -or $insideRepository[0] -ne 'true') {
        return [pscustomobject]@{
            available = $true
            repository = $false
            reason = 'ProjectRoot is not a usable Git worktree'
            executable = $gitCommand.Source
            head = $null
            status_entry_count = $null
            status_porcelain = @()
        }
    }

    $head = @(& $gitCommand.Source -C $Root rev-parse HEAD 2>$null)
    $headExitCode = $LASTEXITCODE
    $status = @(& $gitCommand.Source -C $Root status --porcelain=v1 --untracked-files=all 2>$null)
    $statusExitCode = $LASTEXITCODE
    if ($statusExitCode -ne 0) {
        return [pscustomobject]@{
            available = $true
            repository = $true
            reason = 'git status failed'
            executable = $gitCommand.Source
            head = if ($headExitCode -eq 0) { [string]$head[0] } else { $null }
            status_entry_count = $null
            status_porcelain = @()
        }
    }

    return [pscustomobject]@{
        available = $true
        repository = $true
        reason = if ($headExitCode -eq 0) { 'PASS' } else { 'git HEAD lookup failed; status remains usable' }
        executable = $gitCommand.Source
        head = if ($headExitCode -eq 0) { [string]$head[0] } else { $null }
        status_entry_count = $status.Count
        status_porcelain = @($status)
    }
}

function Get-GitPathState {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$GitState,
        [Parameter(Mandatory)]
        [string]$Root,
        [Parameter(Mandatory)]
        [string]$RelativePath
    )

    if (-not $GitState.available -or -not $GitState.repository) {
        return [pscustomobject]@{
            result = 'PENDING_GIT_UNAVAILABLE'
            tracked_file_count = $null
            local_change_count = $null
            local_changes = @()
        }
    }

    $gitPath = $RelativePath.Replace('\', '/')
    $tracked = @(& $GitState.executable -C $Root ls-files -- $gitPath 2>$null)
    $trackedExitCode = $LASTEXITCODE
    $changes = @(& $GitState.executable -C $Root status --porcelain=v1 --untracked-files=all -- $gitPath 2>$null)
    $changesExitCode = $LASTEXITCODE
    if ($trackedExitCode -ne 0 -or $changesExitCode -ne 0) {
        return [pscustomobject]@{
            result = 'PENDING_GIT_QUERY_FAILED'
            tracked_file_count = $null
            local_change_count = $null
            local_changes = @()
        }
    }

    $result = 'PASS'
    if ($tracked.Count -gt 0) {
        $result = 'BLOCKED_TRACKED_FILES'
    }
    elseif ($changes.Count -gt 0) {
        $result = 'BLOCKED_LOCAL_CHANGES'
    }

    return [pscustomobject]@{
        result = $result
        tracked_file_count = $tracked.Count
        local_change_count = $changes.Count
        local_changes = @($changes)
    }
}

function Get-RecordedProtectedInvariantState {
    param(
        [Parameter(Mandatory)]
        [string]$Root
    )

    # This reads the existing verification only.  It deliberately does not rerun the
    # invariant test because that test writes evidence under Saved/Migration/Evidence.
    $verificationPath = Join-Path $Root 'Saved\Migration\Evidence\ProtectedInvariantVerification.json'
    if (-not [System.IO.File]::Exists((ConvertTo-ExtendedLengthPath -Path $verificationPath))) {
        return [pscustomobject]@{
            source_path = $verificationPath
            status = 'PENDING_NO_RECORDED_VERIFICATION'
            mismatch_count = $null
            generated_utc = $null
            classification = 'NOT_A_PURGE_CANDIDATE'
        }
    }

    try {
        $record = Get-Content -LiteralPath $verificationPath -Raw | ConvertFrom-Json
        return [pscustomobject]@{
            source_path = $verificationPath
            status = [string]$record.result
            mismatch_count = if ($null -ne $record.mismatch_count) { [int64]$record.mismatch_count } else { $null }
            generated_utc = [string]$record.generated_utc
            classification = 'PRE_EXISTING_DRIFT_NOT_A_PURGE_CANDIDATE'
        }
    }
    catch {
        return [pscustomobject]@{
            source_path = $verificationPath
            status = 'PENDING_UNREADABLE_RECORDED_VERIFICATION'
            mismatch_count = $null
            generated_utc = $null
            classification = 'NOT_A_PURGE_CANDIDATE'
        }
    }
}

function Add-CleanupSpec {
    param(
        [Parameter(Mandatory)]
        [object]$Specs,
        [Parameter(Mandatory)]
        [string]$Root,
        [Parameter(Mandatory)]
        [string]$RelativePath,
        [Parameter(Mandatory)]
        [string]$Rule
    )

    $fullPath = Get-NormalizedFullPath -Path (Join-Path $Root $RelativePath)
    if (-not (Test-IsSameOrDescendantPath -Path $fullPath -Root $Root) -or
        $fullPath.Equals($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup allowlist entry escapes ProjectRoot: $RelativePath"
    }

    [void]$Specs.Add([pscustomobject]@{
            relative_path = $fullPath.Substring($Root.Length).TrimStart('\')
            full_path = $fullPath
            rule = $Rule
        })
}

function Test-IsPreservedMigrationPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$MigrationRoot
    )

    if (-not (Test-IsSameOrDescendantPath -Path $Path -Root $MigrationRoot)) {
        return $false
    }

    $relative = (Get-NormalizedFullPath -Path $Path).Substring($MigrationRoot.Length).TrimStart('\')
    if ([string]::IsNullOrWhiteSpace($relative)) {
        return $false
    }
    $firstSegment = @($relative -split '\\')[0]
    if ($firstSegment -in @('Reports', 'Evidence', 'Logs')) {
        return $true
    }
    return $firstSegment -match '^Phase\d+($|_)'
}

function Add-MigrationStageAndArchiveSpecs {
    param(
        [Parameter(Mandatory)]
        [object]$Specs,
        [Parameter(Mandatory)]
        [string]$Root,
        [Parameter(Mandatory)]
        [string]$MigrationRoot
    )

    if (-not (Test-DirectoryExistsLongPath -Path $MigrationRoot)) {
        return
    }

    $stack = New-Object 'System.Collections.Generic.Stack[string]'
    $stack.Push((ConvertTo-ExtendedLengthPath -Path $MigrationRoot))
    while ($stack.Count -gt 0) {
        $directory = $stack.Pop()
        $attributes = [System.IO.File]::GetAttributes($directory)
        if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            continue
        }

        try {
            foreach ($entry in [System.IO.Directory]::EnumerateDirectories($directory)) {
                $entryAttributes = [System.IO.File]::GetAttributes($entry)
                if (($entryAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                    continue
                }

                $normalEntry = Get-NormalizedFullPath -Path (ConvertFrom-ExtendedLengthPath -Path $entry)
                if (Test-IsPreservedMigrationPath -Path $normalEntry -MigrationRoot $MigrationRoot) {
                    continue
                }

                $name = [System.IO.Path]::GetFileName($normalEntry)
                if ($name -in @('Stage', 'Archive')) {
                    Add-CleanupSpec -Specs $Specs -Root $Root `
                        -RelativePath $normalEntry.Substring($Root.Length).TrimStart('\') `
                        -Rule 'Saved/Migration recursively named Stage or Archive outside preserved evidence roots'
                    continue
                }

                $stack.Push($entry)
            }
        }
        catch {
            throw "Could not enumerate Saved/Migration for Stage/Archive artifacts: $($_.Exception.Message)"
        }
    }
}

function Get-EffectiveCleanupSpecs {
    param(
        [Parameter(Mandatory)]
        [object]$Specs
    )

    $uniqueByPath = @{}
    foreach ($spec in $Specs) {
        if (-not $uniqueByPath.ContainsKey($spec.full_path)) {
            $uniqueByPath[$spec.full_path] = $spec
        }
    }

    # Avoid PowerShell's enumerable binder here: Windows PowerShell can throw "Argument
    # types do not match" when Sort-Object receives Hashtable.Values of PSCustomObjects.
    # This deterministic, small in-memory ordering puts parents before descendants.
    $orderedSpecs = New-Object System.Collections.ArrayList
    foreach ($spec in $uniqueByPath.Values) {
        [void]$orderedSpecs.Add($spec)
    }
    for ($leftIndex = 0; $leftIndex -lt $orderedSpecs.Count; $leftIndex++) {
        for ($rightIndex = $leftIndex + 1; $rightIndex -lt $orderedSpecs.Count; $rightIndex++) {
            $left = $orderedSpecs[$leftIndex]
            $right = $orderedSpecs[$rightIndex]
            $shouldSwap = $right.full_path.Length -lt $left.full_path.Length
            if ($right.full_path.Length -eq $left.full_path.Length) {
                $shouldSwap = [string]::Compare(
                    [string]$right.full_path,
                    [string]$left.full_path,
                    [System.StringComparison]::OrdinalIgnoreCase) -lt 0
            }
            if ($shouldSwap) {
                $orderedSpecs[$leftIndex] = $right
                $orderedSpecs[$rightIndex] = $left
            }
        }
    }

    $effective = New-Object System.Collections.ArrayList
    foreach ($spec in $orderedSpecs) {
        $covered = $false
        foreach ($existing in $effective) {
            if (Test-IsSameOrDescendantPath -Path $spec.full_path -Root $existing.full_path) {
                $covered = $true
                break
            }
        }
        if (-not $covered) {
            [void]$effective.Add($spec)
        }
    }
    return $effective.ToArray()
}

function Get-CleanupCandidateRecord {
    param(
        [Parameter(Mandatory)]
        [pscustomobject]$Spec,
        [Parameter(Mandatory)]
        [string]$Root,
        [Parameter(Mandatory)]
        [pscustomobject]$GitState
    )

    $exists = Test-DirectoryExistsLongPath -Path $Spec.full_path
    $validation = New-Object 'System.Collections.Generic.List[string]'
    $metrics = $null
    $gitPathState = Get-GitPathState -GitState $GitState -Root $Root -RelativePath $Spec.relative_path

    $segments = @($Spec.relative_path -split '\\')
    if (-not (Test-IsSameOrDescendantPath -Path $Spec.full_path -Root $Root) -or
        $Spec.full_path.Equals($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        [void]$validation.Add('BLOCKED_PATH_ESCAPES_PROJECT_ROOT')
    }
    if (@($segments | Where-Object { $_ -ieq '.git' }).Count -gt 0) {
        [void]$validation.Add('BLOCKED_GIT_METADATA')
    }
    if (@($segments | Where-Object { $_ -in @('Content', 'Config', 'Source', 'Tools') }).Count -gt 0) {
        [void]$validation.Add('BLOCKED_PROTECTED_PROJECT_ROOT')
    }

    if ($segments.Count -gt 0 -and $segments[0] -ieq 'Plugins') {
        $isProjectOwnedPluginOutput = $segments.Count -ge 3 -and
            $segments[2] -in @('Binaries', 'Intermediate')
        if (-not $isProjectOwnedPluginOutput) {
            [void]$validation.Add('BLOCKED_PLUGIN_CONTENT_OR_METADATA')
        }
        else {
            $pluginRoot = Join-Path $Root (Join-Path 'Plugins' $segments[1])
            $hasSource = Test-DirectoryExistsLongPath -Path (Join-Path $pluginRoot 'Source')
            $hasDescriptor = @(Get-ChildItem -LiteralPath $pluginRoot -Filter '*.uplugin' -File -ErrorAction SilentlyContinue).Count -gt 0
            if (-not $hasSource -or -not $hasDescriptor) {
                [void]$validation.Add('BLOCKED_PLUGIN_IS_NOT_PROJECT_OWNED_SOURCE_PLUGIN')
            }
        }
    }

    if ($segments.Count -ge 2 -and $segments[0] -ieq 'Saved' -and
        $segments[1] -in @('SaveGames', 'Config', 'Screenshots', 'Autosaves', 'Quarantine', 'CodexUser')) {
        [void]$validation.Add('BLOCKED_PRESERVED_SAVED_DATA')
    }
    if ($segments.Count -ge 3 -and $segments[0] -ieq 'Saved' -and $segments[1] -ieq 'Migration' -and
        ($segments[2] -in @('Reports', 'Evidence', 'Logs') -or $segments[2] -match '^Phase\d+($|_)')) {
        [void]$validation.Add('BLOCKED_PRESERVED_MIGRATION_EVIDENCE')
    }

    if ($gitPathState.result -ne 'PASS') {
        [void]$validation.Add([string]$gitPathState.result)
    }

    if ($exists) {
        try {
            $metrics = Get-DirectoryMetricsLongPath -Path $Spec.full_path
            if ($metrics.reparse_point_count -gt 0) {
                [void]$validation.Add('BLOCKED_REPARSE_POINT')
            }
        }
        catch {
            [void]$validation.Add("BLOCKED_METRICS_ERROR: $($_.Exception.Message)")
        }
    }

    $validationResult = if ($validation.Count -eq 0) { 'PASS' } else { 'BLOCKED' }
    $initialResult = if (-not $exists) {
        'MISSING'
    }
    elseif ($validationResult -eq 'PASS') {
        'READY_TO_PURGE'
    }
    else {
        'BLOCKED'
    }

    return [pscustomobject]@{
        relative_path = $Spec.relative_path
        full_path = $Spec.full_path
        allowlist_rule = $Spec.rule
        exists = $exists
        metrics = $metrics
        git = $gitPathState
        validation = @($validation)
        validation_result = $validationResult
        result = $initialResult
        error = $null
    }
}

function Clear-ReadOnlyAttribute {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $attributes = [System.IO.File]::GetAttributes($Path)
    if (($attributes -band [System.IO.FileAttributes]::ReadOnly) -ne 0) {
        $updatedAttributes = $attributes -band (-bnot [System.IO.FileAttributes]::ReadOnly)
        [System.IO.File]::SetAttributes($Path, [System.IO.FileAttributes]$updatedAttributes)
    }
}

function Remove-DirectoryTreeLongPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $longPath = ConvertTo-ExtendedLengthPath -Path $Path
    if (-not [System.IO.Directory]::Exists($longPath)) {
        return
    }

    $directories = New-Object 'System.Collections.Generic.List[string]'
    $stack = New-Object 'System.Collections.Generic.Stack[string]'
    $stack.Push($longPath)

    while ($stack.Count -gt 0) {
        $directory = $stack.Pop()
        $directoryAttributes = [System.IO.File]::GetAttributes($directory)
        if (($directoryAttributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to remove reparse point: $(ConvertFrom-ExtendedLengthPath -Path $directory)"
        }
        [void]$directories.Add($directory)

        foreach ($entry in [System.IO.Directory]::EnumerateFileSystemEntries($directory)) {
            $attributes = [System.IO.File]::GetAttributes($entry)
            if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing to traverse reparse point: $(ConvertFrom-ExtendedLengthPath -Path $entry)"
            }

            if (($attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
                $stack.Push($entry)
                continue
            }

            Clear-ReadOnlyAttribute -Path $entry
            [System.IO.File]::Delete($entry)
        }
    }

    foreach ($directory in @($directories | Sort-Object { $_.Length } -Descending)) {
        Clear-ReadOnlyAttribute -Path $directory
        [System.IO.Directory]::Delete($directory, $false)
    }
}

function Write-CleanupManifest {
    param(
        [Parameter(Mandatory)]
        [hashtable]$Manifest,
        [Parameter(Mandatory)]
        [string]$Path
    )

    $json = $Manifest | ConvertTo-Json -Depth 12
    $utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, $utf8WithoutBom)
}

$projectRootFull = Get-NormalizedFullPath -Path $ProjectRoot
if (-not (Test-DirectoryExistsLongPath -Path $projectRootFull)) {
    throw "ProjectRoot does not exist: $projectRootFull"
}
$projectDescriptor = Join-Path $projectRootFull 'NoShellForWinter.uproject'
if (-not [System.IO.File]::Exists((ConvertTo-ExtendedLengthPath -Path $projectDescriptor))) {
    throw "NoShellForWinter project descriptor was not found: $projectDescriptor"
}

$runningProcesses = @(
    Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessName -in @('UnrealEditor', 'UnrealEditor-Cmd', 'NoShellForWinter') -or
        $_.ProcessName -like 'NoShellForWinter-*'
    }
)
if ($runningProcesses.Count -gt 0) {
    $processSummary = @($runningProcesses | ForEach-Object { "$($_.ProcessName) (PID $($_.Id))" }) -join ', '
    throw "Cleanup is blocked while UnrealEditor, UnrealEditor-Cmd, or NoShellForWinter is running: $processSummary"
}

$specs = New-Object 'System.Collections.Generic.List[object]'

# Closed root and Saved allowlist.  These entries intentionally do not include Saved\SaveGames,
# Saved\Config, Saved\Screenshots, Saved\Autosaves, Saved\Quarantine, or Saved\CodexUser.
$staticSpecs = @(
    @{ path = 'Binaries'; rule = 'root generated build output' },
    @{ path = 'Intermediate'; rule = 'root generated build/intermediate output, including PipInstall/torch' },
    @{ path = 'DerivedDataCache'; rule = 'root derived data cache' },
    @{ path = 'Build\Windows'; rule = 'Windows-only generated build output' },
    @{ path = '__pycache__'; rule = 'root Python bytecode cache' },
    @{ path = 'Saved\Automation'; rule = 'Unreal automation output' },
    @{ path = 'Saved\StagedBuilds'; rule = 'staged package output' },
    @{ path = 'Saved\Cooked'; rule = 'cooked output cache' },
    @{ path = 'Saved\Shaders'; rule = 'shader cache output' },
    @{ path = 'Saved\ShaderDebugInfo'; rule = 'shader debug output' },
    @{ path = 'Saved\Crashes'; rule = 'crash dump output' },
    @{ path = 'Saved\Logs'; rule = 'temporary runtime/build logs' },
    @{ path = 'Saved\Temp'; rule = 'temporary Saved output' },
    @{ path = 'Saved\QA'; rule = 'temporary QA output' },
    @{ path = 'Saved\CharacterCreationQA'; rule = 'temporary character-creation QA output' },
    @{ path = 'Saved\ClothingMorphV2QA'; rule = 'temporary Clothing Morph V2 QA output' },
    @{ path = 'Saved\ClothingMorphV3QA'; rule = 'temporary Clothing Morph V3 QA output' },
    @{ path = 'Saved\ClothingMorphV4QA'; rule = 'temporary Clothing Morph V4 QA output' },
    @{ path = 'Saved\TattooShopQA'; rule = 'temporary TattooShop QA output' },
    @{ path = 'Saved\Artifacts\PendingValidation'; rule = 'unvalidated temporary package artifacts' },
    @{ path = 'Saved\Migration\Package'; rule = 'historical migration package archive' },
    @{ path = 'Saved\Migration\Packages'; rule = 'historical migration package archive' },
    @{ path = 'Saved\Migration\FailedCookDerived'; rule = 'historical failed cook output' },
    @{ path = 'Saved\Migration\PackageEnabledGuard'; rule = 'historical package-enabled guard output' }
)
foreach ($spec in $staticSpecs) {
    Add-CleanupSpec -Specs $specs -Root $projectRootFull -RelativePath $spec.path -Rule $spec.rule
}

# Only direct, project-owned source plugins qualify.  The script never removes a plugin root,
# descriptor, Content, or Source directory.
$pluginsRoot = Join-Path $projectRootFull 'Plugins'
if (Test-DirectoryExistsLongPath -Path $pluginsRoot) {
    foreach ($pluginDirectory in Get-ChildItem -LiteralPath $pluginsRoot -Directory -Force) {
        $sourceDirectory = Join-Path $pluginDirectory.FullName 'Source'
        $descriptorCount = @(Get-ChildItem -LiteralPath $pluginDirectory.FullName -Filter '*.uplugin' -File -ErrorAction SilentlyContinue).Count
        if ((Test-DirectoryExistsLongPath -Path $sourceDirectory) -and $descriptorCount -gt 0) {
            foreach ($generatedDirectory in @('Binaries', 'Intermediate')) {
                Add-CleanupSpec -Specs $specs -Root $projectRootFull `
                    -RelativePath (Join-Path (Join-Path 'Plugins' $pluginDirectory.Name) $generatedDirectory) `
                    -Rule "project-owned source plugin generated $generatedDirectory"
            }
        }
    }
}

$migrationRoot = Join-Path $projectRootFull 'Saved\Migration'
if (Test-DirectoryExistsLongPath -Path $migrationRoot) {
    # Explicit package variants generated by historical migration/package scripts.
    foreach ($directory in Get-ChildItem -LiteralPath $migrationRoot -Directory -Force) {
        if ($directory.Name -match '_Package_' -or
            $directory.Name -match '^PackageEnabledGuard') {
            Add-CleanupSpec -Specs $specs -Root $projectRootFull `
                -RelativePath $directory.FullName.Substring($projectRootFull.Length).TrimStart('\') `
                -Rule 'historical migration package or package-enabled guard output'
        }
    }

    $foodKitRoot = Join-Path $migrationRoot 'FoodKitAlcohol'
    if (Test-DirectoryExistsLongPath -Path $foodKitRoot) {
        foreach ($directory in Get-ChildItem -LiteralPath $foodKitRoot -Directory -Force) {
            if ($directory.Name -match '^FoodKit.*Harness' -or
                $directory.Name -match '^PIE_' -or
                $directory.Name -match '^Automation_' -or
                $directory.Name -in @('Stage', 'Archive')) {
                Add-CleanupSpec -Specs $specs -Root $projectRootFull `
                    -RelativePath $directory.FullName.Substring($projectRootFull.Length).TrimStart('\') `
                    -Rule 'FoodKit generated harness or temporary test output'
            }
        }
    }

    # Do not walk Reports, Evidence, Logs, or Phase* evidence roots.  Stage/Archive folders
    # elsewhere are only collected when their own containing output is not already allowlisted.
    Add-MigrationStageAndArchiveSpecs -Specs $specs -Root $projectRootFull -MigrationRoot $migrationRoot
}

$effectiveSpecs = Get-EffectiveCleanupSpecs -Specs $specs
$gitState = Get-GitState -Root $projectRootFull
$candidateRecords = @(
    foreach ($spec in $effectiveSpecs) {
        Get-CleanupCandidateRecord -Spec $spec -Root $projectRootFull -GitState $gitState
    }
)

$eligibleRecords = @($candidateRecords | Where-Object { $_.result -eq 'READY_TO_PURGE' })
$blockedRecords = @($candidateRecords | Where-Object { $_.result -eq 'BLOCKED' })
$potentialBytes = [int64]0
foreach ($record in $eligibleRecords) {
    if ($null -ne $record.metrics) {
        $potentialBytes += [int64]$record.metrics.byte_count
    }
}

$evidenceRoot = Join-Path $projectRootFull 'Docs\Migration\Evidence'
if (-not (Test-IsSameOrDescendantPath -Path $evidenceRoot -Root $projectRootFull)) {
    throw "Evidence root escapes ProjectRoot: $evidenceRoot"
}
[System.IO.Directory]::CreateDirectory((ConvertTo-ExtendedLengthPath -Path $evidenceRoot)) | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$manifestPath = Join-Path $evidenceRoot "GeneratedCleanup58_$stamp.json"
$suffix = 1
while ([System.IO.File]::Exists((ConvertTo-ExtendedLengthPath -Path $manifestPath))) {
    $manifestPath = Join-Path $evidenceRoot "GeneratedCleanup58_${stamp}_$suffix.json"
    $suffix++
}

$manifest = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    mode = if ($Apply) { 'APPLY' } else { 'AUDIT' }
    project_root = $projectRootFull
    project_descriptor = $projectDescriptor
    manifest_path = $manifestPath
    unreal_process_gate = 'PASS_NO_UNREAL_OR_GAME_PROCESS'
    git_status_before_manifest = $gitState
    protected_boundaries = @(
        'Content (entire tree)',
        'Config (entire tree)',
        'Source (entire tree)',
        'Tools (entire tree)',
        '.git (entire tree, never queried for deletion)',
        'Plugins except Binaries/Intermediate under direct plugins with Source and a .uplugin',
        'Saved/SaveGames',
        'Saved/Config',
        'Saved/Screenshots',
        'Saved/Autosaves',
        'Saved/Quarantine',
        'Saved/CodexUser',
        'Saved/Migration/Reports',
        'Saved/Migration/Evidence',
        'Saved/Migration/Logs',
        'Saved/Migration/Phase*'
    )
    recorded_protected_invariant_state = Get-RecordedProtectedInvariantState -Root $projectRootFull
    pre_purge_validation = if ($blockedRecords.Count -eq 0) { 'PASS' } else { 'BLOCKED' }
    candidate_count = $candidateRecords.Count
    ready_candidate_count = $eligibleRecords.Count
    blocked_candidate_count = $blockedRecords.Count
    potential_reclaim_bytes = $potentialBytes
    potential_reclaim_gib = [Math]::Round(([double]$potentialBytes / 1GB), 3)
    candidates = @($candidateRecords)
    result = if ($Apply) { 'PENDING_APPLY' } elseif ($blockedRecords.Count -eq 0) { 'AUDIT_PASS_NO_DELETION' } else { 'AUDIT_BLOCKED_NO_DELETION' }
}

# The evidence is durable before any deletion attempt.  Per-directory updates below make an
# interrupted -Apply run auditable without relying on a console transcript.
Write-CleanupManifest -Manifest $manifest -Path $manifestPath

if (-not $Apply) {
    Write-Host "Generated cleanup audit complete; no generated output was removed."
    Write-Host "Potential reclaim: $($manifest.potential_reclaim_gib) GiB."
    Write-Host "Manifest: $manifestPath"
    if ($blockedRecords.Count -gt 0) {
        Write-Warning "Audit found $($blockedRecords.Count) blocked candidate(s); inspect the manifest before using -Apply."
    }
    Write-Output $manifestPath
    return
}

if ($blockedRecords.Count -gt 0) {
    $manifest.result = 'APPLY_BLOCKED_NO_DELETION'
    Write-CleanupManifest -Manifest $manifest -Path $manifestPath
    throw "Refusing -Apply because $($blockedRecords.Count) cleanup candidate(s) failed safety validation. See: $manifestPath"
}

$removedBytes = [int64]0
foreach ($record in $eligibleRecords) {
    $record.result = 'PURGE_STARTED'
    Write-CleanupManifest -Manifest $manifest -Path $manifestPath
    try {
        Remove-DirectoryTreeLongPath -Path $record.full_path
        if (Test-DirectoryExistsLongPath -Path $record.full_path) {
            throw "Directory still exists after removal attempt: $($record.full_path)"
        }
        if ($null -ne $record.metrics) {
            $removedBytes += [int64]$record.metrics.byte_count
        }
        $record.result = 'PURGE_PASS'
    }
    catch {
        $record.result = 'PURGE_FAILED'
        $record.error = $_.Exception.Message
        $manifest.result = 'APPLY_FAILED_PARTIAL_PURGE'
        $manifest.removed_bytes = $removedBytes
        $manifest.removed_gib = [Math]::Round(([double]$removedBytes / 1GB), 3)
        Write-CleanupManifest -Manifest $manifest -Path $manifestPath
        throw "Generated cleanup stopped at '$($record.relative_path)': $($_.Exception.Message). See: $manifestPath"
    }
}

$manifest.result = 'APPLY_PASS'
$manifest.removed_bytes = $removedBytes
$manifest.removed_gib = [Math]::Round(([double]$removedBytes / 1GB), 3)
$manifest.completed_utc = [DateTime]::UtcNow.ToString('o')
Write-CleanupManifest -Manifest $manifest -Path $manifestPath

Write-Host "Generated cleanup completed. Reclaimed $($manifest.removed_gib) GiB."
Write-Host "Manifest: $manifestPath"
Write-Output $manifestPath
