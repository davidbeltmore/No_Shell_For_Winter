[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$MapPath = '/Game/_Game/Hub/HUB',
    [ValidateRange(300, 1200)]
    [int]$TimeoutSeconds = 900
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ProjectFile = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$LaunchWrapper = Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$ReceiptGuard = Join-Path $ProjectRoot 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$PythonScript = Join-Path $ProjectRoot 'Tools\ClothingMorphV2\Validate-EFClothingMorphV26SurfaceRuntimePIE58.py'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'

foreach ($requiredPath in @($ProjectFile, $LaunchWrapper, $ReceiptGuard, $PythonScript, $EditorExe)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

$PythonSource = Get-Content -LiteralPath $PythonScript -Raw
if ($PythonSource -match '(?i)\.set_skeletal_mesh(?:_asset)?\s*\(') {
    throw 'The V26 gameplay harness must never assign a Skeletal Mesh directly.'
}
if ($PythonSource -notmatch 'call\(STATE\.interaction,\s*"interact",\s*""\)') {
    throw 'The V26 gameplay harness no longer invokes the real ACF Interact route.'
}
foreach ($requiredToken in @(
        'get_garment_surface_runtime_state',
        'EFClothingSurfaceReadbackQALibrary',
        'begin_final_geometry_readback',
        'poll_final_geometry_readback',
        'release_final_geometry_readback',
        'set_global_clearance_offset_cm',
        'set_garment_clearance_offset_cm',
        'FULL_EQUIP_UNEQUIP_CYCLES = 25',
        'NOT_TESTED_NO_CLAIM',
        'PASS_LIVE_COMPONENT_SPACE_POSE_HASH_CHANGED'
    )) {
    if ($PythonSource.IndexOf($requiredToken, [System.StringComparison]::Ordinal) -lt 0) {
        throw "The V26 gameplay harness is missing required contract token: $requiredToken"
    }
}

$RunId = Get-Date -Format 'yyyyMMdd_HHmmss'
$RunDir = Join-Path $ProjectRoot "Saved\ClothingMorphV2QA\SurfaceRuntime_$RunId"
$RuntimeResultPath = Join-Path $RunDir 'RuntimeResult.json'
$ProgressLog = Join-Path $RunDir 'Progress.log'
$EditorLog = Join-Path $RunDir 'UnrealEditor_EFClothingMorphV26SurfaceRuntime.log'
$LauncherStdout = Join-Path $RunDir 'Launcher.stdout.log'
$LauncherStderr = Join-Path $RunDir 'Launcher.stderr.log'
$LauncherExitCodePath = Join-Path $RunDir 'Launcher.exitcode.txt'
$SummaryPath = Join-Path $RunDir 'Summary.json'
$ReportPath = Join-Path $RunDir 'Report.md'
$HashesBeforePath = Join-Path $RunDir 'ProtectedHashesBefore.json'
$HashesAfterPath = Join-Path $RunDir 'ProtectedHashesAfter.json'
$CompilerReceiptBindingPath = Join-Path $RunDir 'CompilerReceiptBinding.json'
New-Item -ItemType Directory -Path $RunDir -Force | Out-Null

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-SafeNestedValue {
    param(
        $InputObject,
        [Parameter(Mandatory = $true)][string[]]$Path,
        $Default = $null
    )
    $current = $InputObject
    foreach ($segment in $Path) {
        if ($null -eq $current) {
            return $Default
        }
        if ($current -is [System.Collections.IDictionary]) {
            if (-not $current.Contains($segment)) {
                return $Default
            }
            $current = $current[$segment]
            continue
        }
        $property = $current.PSObject.Properties[$segment]
        if ($null -eq $property) {
            return $Default
        }
        $current = $property.Value
    }
    if ($null -eq $current) {
        return $Default
    }
    return $current
}

function ConvertTo-SingleQuotedPowerShellLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Get-GitPorcelain {
    $lines = @(& git -C $ProjectRoot status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to capture target worktree state.'
    }
    return @($lines)
}

function Get-ProtectedAssetState {
    $relativePaths = @(
        'Content\FullSample\Player.uasset',
        'Content\DazToUnreal\Female\Female.uasset',
        'Content\DazToUnreal\Male\Male.uasset',
        'Content\DazToUnreal\Multiple\Multiple.uasset',
        'Content\DazToUnreal\Multiple\Multiple_Skeleton.uasset',
        'Content\DazToUnreal\UnderWearPanty\UnderWearPanty.uasset',
        'Content\DazToUnreal\UnderWearPanty\UnderWearPanty_Skeleton.uasset',
        'Content\FullSample\GASP\UEFN_Mannequin\Meshes\SK_UEFN_Mannequin.uasset',
        'Content\_Game\Data\EFClothingMorph\DA_EFClothingMorphDirector.uasset'
    )
    foreach ($root in @(
            (Join-Path $ProjectRoot 'Plugins\EFClothingMorph\Content\_Internal\Compiled\V26'),
            (Join-Path $ProjectRoot 'Plugins\EFClothingMorph\Content\Deformers')
        )) {
        if (Test-Path -LiteralPath $root -PathType Container) {
            $relativePaths += @(
                Get-ChildItem -LiteralPath $root -File -Filter '*.uasset' -Recurse |
                    Sort-Object FullName |
                    ForEach-Object {
                        $_.FullName.Substring($ProjectRoot.Length).TrimStart('\')
                    }
            )
        }
    }

    $rows = foreach ($relativePath in @($relativePaths | Sort-Object -Unique)) {
        $fullPath = Join-Path $ProjectRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Protected asset is missing: $fullPath"
        }
        $item = Get-Item -LiteralPath $fullPath
        [ordered]@{
            relative_path = $relativePath.Replace('\', '/')
            length = [long]$item.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $fullPath).Hash
        }
    }
    return @($rows)
}

function Get-ProjectEditorProcesses {
    return @(
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.Name -in @('UnrealEditor.exe', 'UnrealEditor-Cmd.exe') -and
                $null -ne $_.CommandLine -and
                $_.CommandLine.IndexOf($ProjectFile, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
            }
    )
}

function Stop-ExactProjectEditors {
    param([switch]$AllowForce)
    $forced = $false
    $editors = @(Get-ProjectEditorProcesses)
    foreach ($editor in $editors) {
        try {
            $process = Get-Process -Id $editor.ProcessId -ErrorAction Stop
            [void]$process.CloseMainWindow()
        }
        catch {}
    }
    $deadline = (Get-Date).AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 250
        $editors = @(Get-ProjectEditorProcesses)
    } while ($editors.Count -gt 0 -and (Get-Date) -lt $deadline)
    if ($editors.Count -gt 0 -and $AllowForce) {
        foreach ($editor in $editors) {
            Stop-Process -Id $editor.ProcessId -Force -ErrorAction SilentlyContinue
            $forced = $true
        }
    }
    return $forced
}

function Invoke-DazReceiptVerification {
    $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ReceiptGuard `
        -ProjectRoot $ProjectRoot `
        -TargetName 'NoShellForWinterEditor' `
        -Configuration 'Development' `
        -VerifyOnly 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Daz editor receipt verification failed: $($output -join ' ')"
    }
    return ($output -join "`n")
}

function Get-LatestCompilerReceiptBinding {
    $receiptRoot = Join-Path $ProjectRoot 'Saved\ClothingMorphV2QA'
    $receiptFile = Get-ChildItem -LiteralPath $receiptRoot -File -Filter 'compiler_receipt_FullCatalog_V26_*.json' |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $receiptFile) {
        throw "No V26 full-catalog compiler receipt exists under $receiptRoot"
    }
    $receipt = Get-Content -LiteralPath $receiptFile.FullName -Raw | ConvertFrom-Json
    if (
        [string]$receipt.status -ne 'UE58_EF_CLOTHING_MORPH_V26_CATALOG_COMPILE_PASS' -or
        -not [bool]$receipt.success -or
		[int]$receipt.schema_version -ne 7 -or
		[int]$receipt.compiler_version -ne 26 -or
		[string]$receipt.director -ne '/Game/_Game/Data/EFClothingMorph/DA_EFClothingMorphDirector' -or
		[string]$receipt.output_root -ne '/EFClothingMorph/_Internal/Compiled/V26' -or
		[string]$receipt.registry -ne '/EFClothingMorph/_Internal/Compiled/V26/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry' -or
        -not [bool]$receipt.catalog_equality_gate -or
        -not [bool]$receipt.protected_inputs_unchanged -or
        [int]$receipt.enabled_row_count -lt 1 -or
        [int]$receipt.enabled_row_count -ne [int]$receipt.compiled_row_count -or
        [int]$receipt.enabled_row_count -ne [int]$receipt.tested_row_count -or
        [int]$receipt.enabled_row_count -ne [int]$receipt.passed_row_count
    ) {
        throw "Latest V26 catalog compiler receipt is not a protected equality PASS: $($receiptFile.FullName)"
    }
    $binding = [ordered]@{
        path = $receiptFile.FullName
        relative_path = $receiptFile.FullName.Substring($ProjectRoot.Length).TrimStart('\').Replace('\', '/')
        length = [long]$receiptFile.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $receiptFile.FullName).Hash
        generated_utc = [string]$receipt.generated_utc
        status = [string]$receipt.status
        enabled_rows = [int]$receipt.enabled_row_count
        surface_wrap_rows = [int]$receipt.surface_wrap_row_count
        valid_profiles = [int]$receipt.valid_profile_count
        valid_bindings = [int]$receipt.valid_binding_count
        tested_rows = [int]$receipt.tested_row_count
        passed_rows = [int]$receipt.passed_row_count
        registry = [string]$receipt.registry
        rows = @($receipt.rows)
    }
    Write-JsonFile -Value $binding -Path $CompilerReceiptBindingPath
    return $binding
}

function Get-ImageMetadata {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Gameplay screenshot is missing: $Path"
    }
    $image = [System.Drawing.Image]::FromFile($Path)
    try {
        $width = $image.Width
        $height = $image.Height
    }
    finally {
        $image.Dispose()
    }
    $item = Get-Item -LiteralPath $Path
    if ($width -lt 1280 -or $height -lt 720 -or $item.Length -lt 4096) {
        throw "Gameplay screenshot is invalid: $Path (${width}x${height}, $($item.Length) bytes)"
    }
    return [ordered]@{
        path = $Path
        width = $width
        height = $height
        length = [long]$item.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
        visual_review = 'PENDING_HUMAN_REVIEW'
    }
}

Add-Type -AssemblyName System.Drawing

$compilerBinding = Get-LatestCompilerReceiptBinding
$summary = [ordered]@{
    schema_version = 1
    run_id = $RunId
    status = 'IN_PROGRESS'
    failure = $null
    project = $ProjectFile
    map = $MapPath
    renderer = 'D3D12_SM6_VISIBLE'
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    finished_utc = $null
    run_dir = $RunDir
    compiler_receipt = $compilerBinding
    runtime_result = $null
    screenshots = @()
    protected_assets_unchanged = $null
    git_state_unchanged = $null
    daz_receipt = $null
    launcher_pid = $null
    launcher_exit_code = $null
    cleanup = 'NOT_STARTED'
    critical_log_matches = @()
    visual_review = 'PENDING_HUMAN_REVIEW'
}

$gitBefore = Get-GitPorcelain
$hashesBefore = Get-ProtectedAssetState
Write-JsonFile -Value $hashesBefore -Path $HashesBeforePath

$environmentValues = [ordered]@{
    CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    CODEX_MIGRATION_PIE_SCRIPT = $PythonScript
    CODEX_EF_CLOTHING_V26_QA_DIR = $RunDir
    CODEX_EF_CLOTHING_V26_QA_RESULT = $RuntimeResultPath
    CODEX_EF_CLOTHING_V26_QA_MAP = $MapPath
    CODEX_EF_CLOTHING_V26_QA_TIMEOUT = [string]($TimeoutSeconds - 60)
}
$previousEnvironment = [ordered]@{}
$launcher = $null
$lastProgressLine = ''
$failureException = $null

try {
    $existingEditors = @(Get-ProjectEditorProcesses)
    if ($existingEditors.Count -gt 0) {
        throw "An Unreal Editor already targets this project: $($existingEditors.ProcessId -join ', ')"
    }
    $summary.daz_receipt = Invoke-DazReceiptVerification

    foreach ($entry in $environmentValues.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, [string]$entry.Value, 'Process')
    }

    $additionalArguments = '-NoSplash -NoP4 -Unattended -d3d12 -sm6 -stdout -FullStdOutLogOutput ' +
        ('-abslog="{0}"' -f $EditorLog)
    if ($additionalArguments -match '(?i)nullrhi') {
        throw 'The V26 runtime gameplay runner must never use NullRHI.'
    }
    $launchInvocation = '& ' + (ConvertTo-SingleQuotedPowerShellLiteral $LaunchWrapper) +
        ' -ProjectRoot ' + (ConvertTo-SingleQuotedPowerShellLiteral $ProjectRoot) +
        ' -EngineRoot ' + (ConvertTo-SingleQuotedPowerShellLiteral $EngineRoot) +
        ' -AdditionalArguments ' + (ConvertTo-SingleQuotedPowerShellLiteral $additionalArguments) +
        ' -Wait'
    $launchCommand = '$ErrorActionPreference = ''Stop''; $efExit = 1; try { ' +
        $launchInvocation +
        '; $efExit = 0 } catch { [Console]::Error.WriteLine($_.Exception.ToString()); $efExit = 1 } finally { ' +
        '[IO.File]::WriteAllText(' + (ConvertTo-SingleQuotedPowerShellLiteral $LauncherExitCodePath) +
        ', [string]$efExit) }; exit $efExit'
    $encodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($launchCommand))
    $launcher = Start-Process `
        -FilePath 'powershell.exe' `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $encodedCommand) `
        -RedirectStandardOutput $LauncherStdout `
        -RedirectStandardError $LauncherStderr `
        -PassThru `
        -WindowStyle Hidden
    $summary.launcher_pid = $launcher.Id

    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $launcher.Refresh()
        if (Test-Path -LiteralPath $ProgressLog -PathType Leaf) {
            $progressLines = @(Get-Content -LiteralPath $ProgressLog -ErrorAction SilentlyContinue)
            if ($progressLines.Count -gt 0) {
                $current = [string]$progressLines[-1]
                if ($current -ne $lastProgressLine) {
                    Write-Host $current
                    $lastProgressLine = $current
                }
            }
        }
        if ($launcher.HasExited) {
            break
        }
        Start-Sleep -Milliseconds 250
    }

    $launcher.Refresh()
    if (-not $launcher.HasExited) {
        [void](Stop-ExactProjectEditors -AllowForce)
        Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
        throw "V26 Surface Runtime PIE exceeded $TimeoutSeconds seconds."
    }
    $launcher.WaitForExit()
    if (-not (Test-Path -LiteralPath $LauncherExitCodePath -PathType Leaf)) {
        throw "Launch wrapper did not publish exit-code sidecar: $LauncherExitCodePath"
    }
    $launcherExitText = (Get-Content -LiteralPath $LauncherExitCodePath -Raw).Trim()
    if ($launcherExitText -notmatch '^-?\d+$') {
        throw "Launch wrapper published invalid exit code: $launcherExitText"
    }
    $summary.launcher_exit_code = [int]$launcherExitText
    if ([int]$summary.launcher_exit_code -ne 0) {
        $stderr = if (Test-Path -LiteralPath $LauncherStderr) { (Get-Content -LiteralPath $LauncherStderr -Raw).Trim() } else { '' }
        throw "Editor launch wrapper failed with exit code $($summary.launcher_exit_code): $stderr"
    }

    if (-not (Test-Path -LiteralPath $RuntimeResultPath -PathType Leaf)) {
        throw "Runtime result was not created: $RuntimeResultPath"
    }
    $runtime = Get-Content -LiteralPath $RuntimeResultPath -Raw | ConvertFrom-Json
    $summary.runtime_result = $runtime
    if ([string]$runtime.status -ne 'UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_PASS') {
        throw "V26 runtime validation failed: $($runtime.failure)"
    }
    if (
        [int]$runtime.expected_compiler_version -ne 26 -or
        -not [bool]$runtime.catalog_equality_gate.passed -or
        [string]$runtime.hub_character_creation_gate.status -ne 'PASS_ABSENT_FROM_ALL_OBSERVED_HUB_PIE_TICKS' -or
        [string]$runtime.surface_visibility_gate.status -ne 'PASS_ZERO_UNSAFE_RENDERABLE_POST_TICK_SAMPLES' -or
        [string]$runtime.geometric_gpu_readback.status -ne 'PASS_FINAL_OPTIMUS_GPU_BUFFERS_GEOMETRIC_THRESHOLDS' -or
        [string]$runtime.story_selection_auto_open.status -ne 'NOT_TESTED_NO_CLAIM'
    ) {
        throw 'V26 runtime result is missing a required fail-closed/HUB/no-Story-claim gate.'
    }
    if (
        [int]$runtime.catalog.enabled_row_count -ne [int]$compilerBinding.enabled_rows -or
        [int]$runtime.catalog.valid_profile_count -ne [int]$compilerBinding.valid_profiles -or
        [int]$runtime.catalog.valid_binding_count -ne [int]$compilerBinding.valid_bindings
    ) {
        throw 'Runtime catalog counts differ from the exact latest compiler receipt.'
    }
    foreach ($runtimeRow in @($runtime.rows_tested)) {
        if ([string]$runtimeRow.status -ne 'PASS') {
            throw "Runtime row did not pass: $($runtimeRow.row_name)"
        }
        $compilerRow = @($compilerBinding.rows | Where-Object { [string]$_.garment_id -eq [string]$runtimeRow.row_name })
        if ($compilerRow.Count -ne 1 -or -not [bool]$compilerRow[0].success -or -not [bool]$compilerRow[0].binding_valid) {
            throw "Runtime row is not bound to one successful compiler receipt row: $($runtimeRow.row_name)"
        }
        if (
            [int]$runtimeRow.equip_unequip_cycles.completed -ne 25 -and
            -not ([string]$runtimeRow.equip_unequip_cycles.status).StartsWith('SKIPPED_')
        ) {
            throw "ACF equip/unequip cycle gate is incomplete for $($runtimeRow.row_name)."
        }
        if (-not [bool]$runtimeRow.pose_evidence.crawl.animation_name_alone_not_used_as_evidence) {
            throw "Crawl pose hash evidence is incomplete for $($runtimeRow.row_name)."
        }
        if (@($runtimeRow.gpu_readbacks).Count -lt 6) {
            throw "Final GPU readback pose coverage is incomplete for $($runtimeRow.row_name)."
        }
        foreach ($readback in @($runtimeRow.gpu_readbacks)) {
            if (
                [string]$readback.state -ne 'Ready' -or
                -not [bool]$readback.thresholds.passed -or
                [int]$readback.triangle_intersections -ne 0 -or
                [int]$readback.invalid_or_non_finite_vertices -ne 0
            ) {
                throw "Final GPU geometry threshold failure for $($runtimeRow.row_name)/$($readback.pose)."
            }
        }
    }

    $screenRows = @()
    foreach ($property in $runtime.screenshots.PSObject.Properties) {
        $screenRows += Get-ImageMetadata -Path ([string]$property.Value.path)
    }
    $expectedScreenshotCount = 5 * [int]$runtime.catalog.enabled_row_count
    if ($screenRows.Count -ne $expectedScreenshotCount) {
        throw "Expected $expectedScreenshotCount catalog-driven gameplay screenshots; found $($screenRows.Count)."
    }
    $summary.screenshots = @($screenRows)

    $criticalPatterns = @(
        'Fatal error:',
        'EXCEPTION_ACCESS_VIOLATION',
        'Assertion failed:',
        'LogEFClothingMorphV2: Error:',
        'SurfaceWrapGPU dispatch failed',
        'NaN/Inf'
    )
    if (Test-Path -LiteralPath $EditorLog -PathType Leaf) {
        foreach ($pattern in $criticalPatterns) {
            $matches = @(Select-String -LiteralPath $EditorLog -SimpleMatch $pattern -ErrorAction SilentlyContinue)
            foreach ($match in $matches) {
                $summary.critical_log_matches += [ordered]@{
                    pattern = $pattern
                    line_number = $match.LineNumber
                    line = $match.Line.Trim()
                }
            }
        }
    }
    if (@($summary.critical_log_matches).Count -gt 0) {
        throw "Critical runtime log signatures were observed: $(@($summary.critical_log_matches).Count)"
    }

    $summary.cleanup = 'CLEAN_EDITOR_SELF_EXIT'
    $summary.status = 'UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_PASS'
}
catch {
    $failureException = $_
    $summary.status = 'UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_FAIL'
    $summary.failure = $_.Exception.Message
}
finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    $remainingEditors = @(Get-ProjectEditorProcesses)
    if ($remainingEditors.Count -gt 0) {
        $forced = Stop-ExactProjectEditors -AllowForce
        $summary.cleanup = if ($forced) { 'FORCED_EXACT_PROJECT_EDITOR_CLEANUP' } else { 'GRACEFUL_EXACT_PROJECT_EDITOR_CLEANUP' }
    }
    try {
        $summary.daz_receipt = Invoke-DazReceiptVerification
    }
    catch {
        if ($null -eq $failureException) {
            $failureException = $_
            $summary.status = 'UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_FAIL'
            $summary.failure = $_.Exception.Message
        }
    }
    try {
        $hashesAfter = Get-ProtectedAssetState
        Write-JsonFile -Value $hashesAfter -Path $HashesAfterPath
        $summary.protected_assets_unchanged = ((ConvertTo-Json $hashesBefore -Depth 10 -Compress) -ceq (ConvertTo-Json $hashesAfter -Depth 10 -Compress))
        $gitAfter = Get-GitPorcelain
        $summary.git_state_unchanged = ((ConvertTo-Json $gitBefore -Compress) -ceq (ConvertTo-Json $gitAfter -Compress))
        if (-not $summary.protected_assets_unchanged -or -not $summary.git_state_unchanged) {
            throw 'Worktree or protected Unreal assets changed during runtime QA.'
        }
    }
    catch {
        $summary.protected_assets_unchanged = $false
        if ($null -eq $failureException) {
            $failureException = $_
            $summary.status = 'UE58_EF_CLOTHING_MORPH_V26_SURFACE_RUNTIME_FAIL'
            $summary.failure = $_.Exception.Message
        }
    }
    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')

    if ($null -eq $summary.runtime_result -and (Test-Path -LiteralPath $RuntimeResultPath -PathType Leaf)) {
        try {
            $summary.runtime_result = Get-Content -LiteralPath $RuntimeResultPath -Raw | ConvertFrom-Json
        }
        catch {
            # Preserve the primary failure. A malformed partial result is reported
            # as unavailable instead of causing a second StrictMode exception.
        }
    }

    $runtimeForReport = $summary.runtime_result
    $catalogEqualityForReport = Get-SafeNestedValue `
        -InputObject $runtimeForReport `
        -Path @('catalog_equality_gate', 'passed') `
        -Default 'PENDING_OR_NOT_REACHED'
    $unsafeFramesForReport = @(
        Get-SafeNestedValue `
            -InputObject $runtimeForReport `
            -Path @('surface_visibility_gate', 'unsafe_renderable_frames') `
            -Default @()
    )
    $characterCreationForReport = Get-SafeNestedValue `
        -InputObject $runtimeForReport `
        -Path @('hub_character_creation_gate', 'status') `
        -Default 'PENDING_OR_NOT_REACHED'
    $storySelectionForReport = Get-SafeNestedValue `
        -InputObject $runtimeForReport `
        -Path @('story_selection_auto_open', 'status') `
        -Default 'NOT_TESTED_NO_CLAIM'
    $gpuReadbackForReport = Get-SafeNestedValue `
        -InputObject $runtimeForReport `
        -Path @('geometric_gpu_readback', 'status') `
        -Default 'PENDING_OR_NOT_REACHED'

    Write-JsonFile -Value $summary -Path $SummaryPath

    $report = @(
        '# EF Clothing Morph V26 — Surface Runtime PIE',
        '',
        "- Status: ``$($summary.status)``",
        "- Map: ``$MapPath``",
        '- Renderer: `D3D12 / SM6`, visible PIE',
        "- Compiler receipt: ``$($compilerBinding.relative_path)``",
        "- Runtime result: ``$RuntimeResultPath``",
        "- Catalog equality: ``$catalogEqualityForReport``",
        "- Unsafe renderable post-tick samples: ``$($unsafeFramesForReport.Count)``",
        "- HUB Character Creation: ``$characterCreationForReport``",
        "- Story Selection: ``$storySelectionForReport``",
        "- Protected assets unchanged: ``$($summary.protected_assets_unchanged)``",
        "- Worktree unchanged: ``$($summary.git_state_unchanged)``",
        "- GPU geometric readback: ``$gpuReadbackForReport``",
        "- Visual review: ``$($summary.visual_review)``",
        '',
        'The runtime harness does not substitute CPU geometry for final GPU-buffer intersection evidence.'
    )
    Set-Content -LiteralPath $ReportPath -Value ($report -join "`r`n") -Encoding UTF8
}

"RESULT_DIR=$RunDir"
"SUMMARY=$SummaryPath"
"REPORT=$ReportPath"

if ($null -ne $failureException) {
    throw $failureException
}
