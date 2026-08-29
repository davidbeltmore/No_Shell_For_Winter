[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$MapPath = '/Game/_Game/Hub/HUB',
    [ValidateRange(240, 1200)]
    [int]$TimeoutSeconds = 660
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$ProjectFile = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$LaunchWrapper = Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$ReceiptGuard = Join-Path $ProjectRoot 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$PythonScript = Join-Path $ProjectRoot 'Tools\ClothingMorphV4\Validate-EFClothingMorphV4RuntimeSmokePIE58.py'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'

foreach ($requiredPath in @($ProjectFile, $LaunchWrapper, $ReceiptGuard, $PythonScript, $EditorExe)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

# Static safety gates keep this V4 harness source-first and prevent accidental
# reintroduction of the destructive/generated-mesh V26 test contract.
$PythonSource = Get-Content -LiteralPath $PythonScript -Raw
if ($PythonSource -match '(?i)\.set_skeletal_mesh(?:_asset)?\s*\(') {
    throw 'The V4 smoke harness must never assign a Skeletal Mesh directly.'
}
if ($PythonSource -match '(?i)(compile_native_source_catalog_v4|compile_all_garments|compile_catalog)\s*\(') {
    throw 'The V4 runtime smoke harness must never invoke a clothing compiler.'
}
if ($PythonSource -match '(?i)(EditorAssetLibrary\.)?save_asset\s*\(') {
    throw 'The V4 runtime smoke harness must never save an Unreal asset.'
}
if ($PythonSource.IndexOf('call(STATE.interaction, "interact", "")', [System.StringComparison]::Ordinal) -lt 0) {
    throw 'The V4 smoke harness no longer invokes the real ACF Interact route.'
}
foreach ($forbiddenToken in @(
        'EFClothingSurfaceReadbackQALibrary',
        'begin_final_geometry_readback',
        'poll_final_geometry_readback',
        'get_garment_surface_runtime_state'
    )) {
    if ($PythonSource.IndexOf($forbiddenToken, [System.StringComparison]::Ordinal) -ge 0) {
        throw "The V4 smoke harness contains forbidden V26 readback/runtime token: $forbiddenToken"
    }
}
foreach ($requiredToken in @(
        'EFClothingMorphV3RuntimeComponent',
        'get_garment_runtime_state',
        'set_garment_clearance_offset_cm',
        'set_garment_inflate_cm',
        'clear_garment_clearance_offset_cm',
        'clear_garment_inflate_cm',
        'OFFSET_TEST_CM = 0.2',
        'SMOKE_EQUIP_UNEQUIP_CYCLES = 3',
        'MIN_SIMULTANEOUS_CLOTHES = 2',
        'retained_clothes',
        'body_visibility_contract_snapshot',
        'PASS_ALL_ENABLED_VALID_CLOTHES_SIMULTANEOUS_READY',
        'PASS_EXACT_SOURCE_GARMENT_READY_NO_FITTED_NO_EF_AUTOFIT',
        'FAIL_WITH_DEBUG_SUMMARY',
        'PASS_ABSENT_FROM_ALL_OBSERVED_HUB_PIE_TICKS',
        'take_high_res_screenshot(1600, 900',
        'equip_item_from_inventory_in_slot',
        'unequip_item_by_guid'
    )) {
    if ($PythonSource.IndexOf($requiredToken, [System.StringComparison]::Ordinal) -lt 0) {
        throw "The V4 smoke harness is missing required contract token: $requiredToken"
    }
}

$RunId = Get-Date -Format 'yyyyMMdd_HHmmss'
$RunDir = Join-Path $ProjectRoot "Saved\ClothingMorphV4QA\RuntimeSmoke_$RunId"
$RuntimeResultPath = Join-Path $RunDir 'RuntimeResult.json'
$ProgressLog = Join-Path $RunDir 'Progress.log'
$EditorLog = Join-Path $RunDir 'UnrealEditor_EFClothingMorphV4RuntimeSmoke.log'
$LauncherStdout = Join-Path $RunDir 'Launcher.stdout.log'
$LauncherStderr = Join-Path $RunDir 'Launcher.stderr.log'
$LauncherExitCodePath = Join-Path $RunDir 'Launcher.exitcode.txt'
$SummaryPath = Join-Path $RunDir 'Summary.json'
$HashesBeforePath = Join-Path $RunDir 'ProtectedHashesBefore.json'
$HashesAfterPath = Join-Path $RunDir 'ProtectedHashesAfter.json'
New-Item -ItemType Directory -Path $RunDir -Force | Out-Null

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding UTF8
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
        'Content\DazToUnreal\UnderWearBra\UnderWearBra.uasset',
        'Content\DazToUnreal\UnderWearBra\UnderWearBra_Skeleton.uasset',
        'Content\_Game\Data\EFClothingMorph\DA_EFClothingMorphDirector.uasset',
        'Plugins\EFClothingMorph\Content\Deformers\DG_EFGarmentSurfaceConstraint.uasset'
    )
    $v4Root = Join-Path $ProjectRoot 'Plugins\EFClothingMorph\Content\_Internal\Compiled\V4'
    if (-not (Test-Path -LiteralPath $v4Root -PathType Container)) {
        throw "V4 compiled root is missing: $v4Root"
    }
    $relativePaths += @(
        Get-ChildItem -LiteralPath $v4Root -File -Filter '*.uasset' -Recurse |
            Sort-Object FullName |
            ForEach-Object { $_.FullName.Substring($ProjectRoot.Length).TrimStart('\') }
    )

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

function Stop-OwnedProjectEditors {
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
    CODEX_EF_CLOTHING_V4_QA_DIR = $RunDir
    CODEX_EF_CLOTHING_V4_QA_RESULT = $RuntimeResultPath
    CODEX_EF_CLOTHING_V4_QA_MAP = $MapPath
    CODEX_EF_CLOTHING_V4_QA_TIMEOUT = [string]($TimeoutSeconds - 60)
}
$previousEnvironment = [ordered]@{}
$launcher = $null
$lastProgressLine = ''
$failureException = $null
$ownsEditorSession = $false

try {
    $existingEditors = @(Get-ProjectEditorProcesses)
    if ($existingEditors.Count -gt 0) {
        throw "An Unreal Editor already targets this project; close it before the isolated V4 smoke run. PID(s): $($existingEditors.ProcessId -join ', ')"
    }
    $summary.daz_receipt = Invoke-DazReceiptVerification

    foreach ($entry in $environmentValues.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, [string]$entry.Value, 'Process')
    }

    $additionalArguments = '-NoSplash -NoP4 -Unattended -d3d12 -sm6 -stdout -FullStdOutLogOutput ' +
        ('-abslog="{0}"' -f $EditorLog)
    if ($additionalArguments -match '(?i)nullrhi') {
        throw 'The V4 runtime smoke must never use NullRHI.'
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
    $ownsEditorSession = $true
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
        [void](Stop-OwnedProjectEditors -AllowForce)
        Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
        throw "V4 runtime smoke exceeded $TimeoutSeconds seconds."
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
        $stderr = if (Test-Path -LiteralPath $LauncherStderr) {
            (Get-Content -LiteralPath $LauncherStderr -Raw).Trim()
        }
        else { '' }
        throw "Editor launch wrapper failed with exit code $($summary.launcher_exit_code): $stderr"
    }

    if (-not (Test-Path -LiteralPath $RuntimeResultPath -PathType Leaf)) {
        throw "Runtime result was not created: $RuntimeResultPath"
    }
    $runtime = Get-Content -LiteralPath $RuntimeResultPath -Raw | ConvertFrom-Json
    $summary.runtime_result = $runtime
    if ([string]$runtime.status -ne 'UE58_EF_CLOTHING_MORPH_V4_RUNTIME_SMOKE_PASS') {
        throw "V4 runtime smoke failed: $($runtime.failure)"
    }
    if (
        [int]$runtime.expected_compiler_version -ne 28 -or
        [int]$runtime.expected_binding_schema -ne 8 -or
        [string]$runtime.hub_character_creation_gate.status -ne 'PASS_ABSENT_FROM_ALL_OBSERVED_HUB_PIE_TICKS' -or
        [string]$runtime.source_visibility_gate.status -ne 'PASS_EXACT_SOURCE_READY_NEVER_HIDDEN_WHILE_EXPECTED' -or
        @($runtime.source_visibility_gate.passthrough_observations).Count -ne 0 -or
        [string]$runtime.gpu_readback.status -ne 'NOT_USED_V4_SMOKE' -or
        [bool]$runtime.gpu_readback.v26_readback_dependency -or
        -not [bool]$runtime.no_assets_saved -or
        -not [bool]$runtime.cleanup_requested
    ) {
        throw 'V4 runtime result is missing a required source-first/HUB/no-readback gate.'
    }
    if (
        [int]$runtime.catalog.valid_row_count -lt 2 -or
        [int]$runtime.catalog.enabled_row_count -lt [int]$runtime.catalog.valid_row_count -or
        [int]$runtime.catalog.valid_row_count -ne [int]$runtime.catalog.native_binding_count -or
        [int]$runtime.catalog.generated_profile_count -ne 0 -or
        [int]$runtime.catalog.valid_row_count -ne @($runtime.rows_tested).Count
    ) {
        throw 'V4 runtime catalog does not equal its binding-only registry/test coverage.'
    }
    if (
        [string]$runtime.multi_clothing_gate.status -ne 'PASS_ALL_ENABLED_VALID_CLOTHES_SIMULTANEOUS_READY' -or
        [int]$runtime.multi_clothing_gate.expected_simultaneous_clothes -ne [int]$runtime.catalog.valid_row_count -or
        [int]$runtime.multi_clothing_gate.final_runtime_counts.managed -ne [int]$runtime.catalog.valid_row_count -or
        [int]$runtime.multi_clothing_gate.final_runtime_counts.ready -ne [int]$runtime.catalog.valid_row_count -or
        [int]$runtime.multi_clothing_gate.final_runtime_counts.warming -ne 0 -or
        [int]$runtime.multi_clothing_gate.final_runtime_counts.passthrough -ne 0 -or
        @($runtime.multi_clothing_gate.offset_isolation_checks).Count -lt 1 -or
        @($runtime.multi_clothing_gate.unequip_isolation_checks).Count -lt 1 -or
        @($runtime.multi_clothing_gate.combined_gameplay_screenshots).Count -lt 4
    ) {
        throw 'V4 simultaneous independent-clothing gate failed.'
    }
    if (
        @($runtime.cleanup.offset_components_cleared).Count -ne [int]$runtime.catalog.valid_row_count -or
        @($runtime.cleanup.acf_guids_unequipped).Count -ne [int]$runtime.catalog.valid_row_count
    ) {
        throw 'V4 runtime cleanup did not clear and unequip every tested clothing entry.'
    }

    foreach ($runtimeRow in @($runtime.rows_tested)) {
        if ([string]$runtimeRow.status -ne 'PASS') {
            throw "Runtime row did not pass: $($runtimeRow.row_name)"
        }
        if (
            [string]$runtimeRow.source_mesh_gate.status -ne 'PASS_EXACT_SOURCE_GARMENT_READY_NO_FITTED_NO_EF_AUTOFIT' -or
            -not [string]::IsNullOrWhiteSpace([string]$runtimeRow.source_mesh_gate.binding_fitted_mesh) -or
            [string]$runtimeRow.source_mesh_gate.source_mesh -ne [string]$runtimeRow.source
        ) {
            throw "V4 exact SourceGarment gate failed for $($runtimeRow.row_name)."
        }
        if (
            [string]$runtimeRow.acf_real_equip.status -ne 'PASS_REAL_WORLD_INTERACT_TO_ACF_EQUIPMENT' -or
            [bool]$runtimeRow.acf_real_equip.direct_mesh_assignment -or
            [bool]$runtimeRow.acf_real_equip.direct_equipment_shortcut_for_initial_acquisition -or
            [string]$runtimeRow.acf_real_equip.guid -notmatch '^[0-9A-F]{32}$'
        ) {
            throw "V4 real ACF acquisition/GUID gate failed for $($runtimeRow.row_name)."
        }
        if (
            [string]$runtimeRow.offset_runtime_sequence.status -ne 'PASS_CLEARANCE_AND_INFLATE_0_TO_0_2_TO_0_READY_NO_SWAP' -or
            [bool]$runtimeRow.offset_runtime_sequence.recompile_invoked -or
            [bool]$runtimeRow.offset_runtime_sequence.mesh_swap_invoked
        ) {
            throw "V4 live offset sequence failed for $($runtimeRow.row_name)."
        }
        if (
            [string]$runtimeRow.motion.idle.status -ne 'PASS' -or
            [string]$runtimeRow.motion.walk.status -ne 'PASS' -or
            [double]$runtimeRow.motion.walk.speed_cm_s -le 5.0
        ) {
            throw "V4 idle/locomotion smoke failed for $($runtimeRow.row_name)."
        }
        if (
            [int]$runtimeRow.equip_unequip_cycles.requested -ne 3 -or
            [int]$runtimeRow.equip_unequip_cycles.completed -ne 3 -or
            [string]$runtimeRow.equip_unequip_cycles.status -ne 'PASS_3_REAL_ACF_UNEQUIP_REEQUIP_READY_CYCLES'
        ) {
            throw "V4 three-cycle ACF smoke failed for $($runtimeRow.row_name)."
        }
        if (@($runtimeRow.screenshots).Count -ne 4) {
            throw "Expected four gameplay captures for $($runtimeRow.row_name)."
        }
        foreach ($checkpoint in @($runtimeRow.runtime_checks)) {
            if (
                [string]$checkpoint.mesh -ne [string]$runtimeRow.source -or
                -not [bool]$checkpoint.visible -or
                [string]$checkpoint.state -ne 'Ready' -or
                [string]$checkpoint.skin_weight_profile -eq 'EF_AutoFit'
            ) {
                throw "Certified V4 runtime checkpoint is not visible source-first Ready: $($runtimeRow.row_name)/$($checkpoint.checkpoint)"
            }
        }

        $bodyVisibilityChecks = @($runtimeRow.body_visibility_checks)
        if ($bodyVisibilityChecks.Count -eq 0 -or @($bodyVisibilityChecks | Where-Object { [string]$_.status -ne 'PASS' }).Count -gt 0) {
            throw "V4 body visibility contract did not pass for $($runtimeRow.row_name)."
        }
        foreach ($visibilityCheck in $bodyVisibilityChecks) {
            foreach ($slot in @($visibilityCheck.slots)) {
                $shownByEveryLod = @($slot.shown_by_lod | Where-Object { -not [bool]$_ }).Count -eq 0
                if ([bool]$slot.expected_hidden) {
                    if (@($slot.shown_by_lod | Where-Object { [bool]$_ }).Count -gt 0) {
                        throw "Requested gameplay-hidden body slot remained visible for $($runtimeRow.row_name): $($slot.slot_name)"
                    }
                }
                elseif (-not $shownByEveryLod) {
                    throw "Body slot was hidden without a gameplay-hiding clothing owner for $($runtimeRow.row_name): $($slot.slot_name)"
                }
            }
        }
        $geometryOnlySlots = @(
            $bodyVisibilityChecks |
                ForEach-Object { @($_.slots) } |
                Where-Object {
                    -not [bool]$_.expected_hidden -and
                    @($_.fit_exclusion_owners | Where-Object {
                        [string]$_.clothing_name -eq [string]$runtimeRow.row_name
                    }).Count -gt 0 -and
                    @($_.shown_by_lod | Where-Object { -not [bool]$_ }).Count -eq 0
                }
        )
        if (@($runtimeRow.body_sections_excluded_from_fit).Count -gt 0 -and
            @($runtimeRow.body_sections_to_hide_in_gameplay).Count -eq 0 -and
            $geometryOnlySlots.Count -eq 0) {
            throw "Geometry-only fit exclusions were not proven visible for $($runtimeRow.row_name)."
        }

        $initial = $runtimeRow.offset_runtime_sequence.initial_before_overrides
        if (
            $null -eq $initial -or
            [string]$initial.state -ne 'Ready' -or
            [Math]::Abs(
                [double]$initial.clearance_cm -
                [double]$runtimeRow.authored_additional_clearance_cm
            ) -gt 0.001 -or
            [Math]::Abs(
                [double]$initial.inflate_cm -
                [double]$runtimeRow.authored_shell_thickness_cm
            ) -gt 0.001
        ) {
            throw "V4 initial Director-authored clearance/inflate snapshot failed for $($runtimeRow.row_name)."
        }
    }

    $screenRows = @()
    foreach ($property in $runtime.screenshots.PSObject.Properties) {
        $screenRows += Get-ImageMetadata -Path ([string]$property.Value.path)
    }
    $expectedScreenshotCount = 4 * [int]$runtime.catalog.valid_row_count
    if ($screenRows.Count -ne $expectedScreenshotCount) {
        throw "Expected $expectedScreenshotCount V4 gameplay screenshots; found $($screenRows.Count)."
    }
    $summary.screenshots = @($screenRows)

    $criticalPatterns = @(
        'Fatal error:',
        'EXCEPTION_ACCESS_VIOLATION',
        'Assertion failed:',
        'LogEFClothingMorphV3: Error:',
        'LogEFClothingMorphRuntime: Error:',
        '[EFClothingMorphV4RuntimeSmoke] finished success=False'
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
        throw "Critical V4 runtime log signatures were observed: $(@($summary.critical_log_matches).Count)"
    }

    $summary.cleanup = 'CLEAN_EDITOR_SELF_EXIT'
    $summary.status = 'UE58_EF_CLOTHING_MORPH_V4_RUNTIME_SMOKE_PASS'
}
catch {
    $failureException = $_
    $summary.status = 'UE58_EF_CLOTHING_MORPH_V4_RUNTIME_SMOKE_FAIL'
    $summary.failure = $_.Exception.Message
}
finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    if ($ownsEditorSession) {
        $remainingEditors = @(Get-ProjectEditorProcesses)
        if ($remainingEditors.Count -gt 0) {
            $forced = Stop-OwnedProjectEditors -AllowForce
            $summary.cleanup = if ($forced) {
                'FORCED_OWNED_PROJECT_EDITOR_CLEANUP'
            }
            else {
                'GRACEFUL_OWNED_PROJECT_EDITOR_CLEANUP'
            }
        }
    }
    try {
        $summary.daz_receipt = Invoke-DazReceiptVerification
    }
    catch {
        if ($null -eq $failureException) {
            $failureException = $_
            $summary.status = 'UE58_EF_CLOTHING_MORPH_V4_RUNTIME_SMOKE_FAIL'
            $summary.failure = $_.Exception.Message
        }
    }
    try {
        $hashesAfter = Get-ProtectedAssetState
        Write-JsonFile -Value $hashesAfter -Path $HashesAfterPath
        $summary.protected_assets_unchanged = (
            (ConvertTo-Json $hashesBefore -Depth 10 -Compress) -ceq
            (ConvertTo-Json $hashesAfter -Depth 10 -Compress)
        )
        $gitAfter = Get-GitPorcelain
        $summary.git_state_unchanged = (
            (ConvertTo-Json $gitBefore -Compress) -ceq
            (ConvertTo-Json $gitAfter -Compress)
        )
        if (-not $summary.protected_assets_unchanged -or -not $summary.git_state_unchanged) {
            throw 'Worktree or protected Unreal assets changed during V4 runtime smoke.'
        }
    }
    catch {
        $summary.protected_assets_unchanged = $false
        $summary.git_state_unchanged = $false
        if ($null -eq $failureException) {
            $failureException = $_
            $summary.status = 'UE58_EF_CLOTHING_MORPH_V4_RUNTIME_SMOKE_FAIL'
            $summary.failure = $_.Exception.Message
        }
    }
    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
    Write-JsonFile -Value $summary -Path $SummaryPath
}

Write-Host "V4 runtime smoke summary: $SummaryPath"
if ($null -ne $failureException) {
    throw $failureException
}
