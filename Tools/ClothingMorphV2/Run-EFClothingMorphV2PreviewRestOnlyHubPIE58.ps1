[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$MapPath = '/Game/_Game/Hub/HUB',
    [ValidateRange(180, 900)]
    [int]$TimeoutSeconds = 390
)

throw 'The public-root V25 preview harness is retired. Use Run-EFClothingMorphV26SurfaceRuntimePIE58.ps1 with the single Clothing Director.'

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ProjectFile = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$LaunchWrapper = Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$ReceiptGuard = Join-Path $ProjectRoot 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$PythonScript = Join-Path $ProjectRoot 'Tools\ClothingMorphV2\Validate-EFClothingMorphV2PreviewRestOnlyHubPIE58.py'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'

foreach ($requiredPath in @(
        $ProjectFile,
        $LaunchWrapper,
        $ReceiptGuard,
        $PythonScript,
        $EditorExe
    )) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file not found: $requiredPath"
    }
}

$PythonSource = Get-Content -LiteralPath $PythonScript -Raw
if ($PythonSource -match '(?i)\.set_skeletal_mesh(?:_asset)?\s*\(') {
    throw 'The PreviewRestOnly gameplay harness must not assign a garment mesh directly'
}
if ($PythonSource -notmatch 'call\(STATE\.interaction,\s*"interact",\s*""\)') {
    throw 'The PreviewRestOnly harness no longer invokes the real ACF Interact route'
}
if ($PythonSource -match '(?i)set_morph_target\s*\(') {
    throw 'PreviewRestOnly gameplay proof must not mutate body morphs'
}
if (
    $PythonSource -notmatch 'is_material_section_shown' -or
    $PythonSource -notmatch 'Genesis9_GP_Torso' -or
    $PythonSource -notmatch 'CoverageSections' -or
    $PythonSource -notmatch 'CoverageRefs'
) {
    throw 'PreviewRestOnly gameplay proof no longer validates exact GP material coverage'
}

$RunId = Get-Date -Format 'yyyyMMdd_HHmmss'
$RunDir = Join-Path $ProjectRoot "Saved\ClothingMorphV2QA\PreviewRestOnlyHubPIE_$RunId"
$RuntimeResult = Join-Path $RunDir 'RuntimeResult.json'
$ProgressLog = Join-Path $RunDir 'Progress.log'
$EditorLog = Join-Path $RunDir 'UnrealEditor_EFClothingMorphV2PreviewRestHubPIE58.log'
$LauncherStdout = Join-Path $RunDir 'Launcher.stdout.log'
$LauncherStderr = Join-Path $RunDir 'Launcher.stderr.log'
$LauncherExitCodePath = Join-Path $RunDir 'Launcher.exitcode.txt'
$SummaryPath = Join-Path $RunDir 'Summary.json'
$ReportPath = Join-Path $RunDir 'Report.md'
$BeforeHashesPath = Join-Path $RunDir 'ProtectedHashesBefore.json'
$AfterHashesPath = Join-Path $RunDir 'ProtectedHashesAfter.json'
$CompilerBindingPath = Join-Path $RunDir 'CompilerPassBinding.json'
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

$ExpectedScreenshots = @(
    '01_acf_pickup_selected.png',
    '02_front_idle_ready.png',
    '03_back_idle_ready.png',
    '04_side_walk_motion.png',
    '05_back_walk_motion.png',
    '06_side_crawl_motion.png',
    '07_side_crawl_closeup.png'
)
$ExpectedSurfaceSlots = @('Genesis9_GP_Torso')
$ExpectedBoneBranches = @('anus_01', 'pelvis2', 'rectum_01')
$ExpectedMorphPrefixes = @()

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $Value | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function ConvertTo-NormalizedGuid {
    param([Parameter(Mandatory = $true)][string]$Value)
    $normalized = ($Value -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{32}$') {
        throw "Malformed compiler build GUID: $Value"
    }
    return $normalized
}

function Test-ExactStringArray {
    param(
        [AllowNull()]$Actual,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Expected
    )
    $actualValues = @($Actual | ForEach-Object { [string]$_ })
    if ($actualValues.Count -ne $Expected.Count) {
        return $false
    }
    if ($actualValues.Count -eq 0) {
        return $true
    }
    return @(
        Compare-Object -ReferenceObject $Expected -DifferenceObject $actualValues -SyncWindow 0
    ).Count -eq 0
}

function Resolve-GameObjectPathFile {
    param([Parameter(Mandatory = $true)][string]$ObjectPath)
    $packagePath = ($ObjectPath -split '\.', 2)[0]
    if (-not $packagePath.StartsWith('/Game/', [System.StringComparison]::Ordinal)) {
        throw "Compiler output is not a /Game asset: $ObjectPath"
    }
    $relativePath = $packagePath.Substring('/Game/'.Length).Replace('/', '\') + '.uasset'
    $fullPath = [System.IO.Path]::GetFullPath(
        (Join-Path (Join-Path $ProjectRoot 'Content') $relativePath)
    )
    $contentRoot = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot 'Content'))
    if (-not $fullPath.StartsWith($contentRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Resolved compiler asset escapes Content: $ObjectPath -> $fullPath"
    }
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Compiler-bound asset is missing: $fullPath"
    }
    return $fullPath
}

function New-FileBindingRow {
    param(
        [Parameter(Mandatory = $true)][string]$FullPath,
        [string]$ObjectPath = ''
    )
    $resolved = (Resolve-Path -LiteralPath $FullPath).Path
    if (-not $resolved.StartsWith($ProjectRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Binding file escapes target project: $resolved"
    }
    $item = Get-Item -LiteralPath $resolved
    return [ordered]@{
        object_path = $ObjectPath
        relative_path = $resolved.Substring($ProjectRoot.Length).TrimStart('\').Replace('\', '/')
        size_bytes = $item.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolved).Hash
    }
}

function New-CompilerPassBinding {
    $receiptRoot = Join-Path $ProjectRoot 'Saved\ClothingMorphV2QA'
    $receiptCandidates = @(
        Get-ChildItem `
        -LiteralPath $receiptRoot `
        -File `
        -Filter 'compiler_receipt_PreviewRestOnly_*.json' |
            Sort-Object LastWriteTimeUtc -Descending
    )
    $latestReceipt = $null
    $receipt = $null
    foreach ($candidate in $receiptCandidates) {
        try {
            $candidateReceipt = Get-Content -LiteralPath $candidate.FullName -Raw | ConvertFrom-Json
        }
        catch {
            continue
        }
        if (
            [bool]$candidateReceipt.success -and
            [bool]$candidateReceipt.compile_success -and
            [bool]$candidateReceipt.validation_success -and
            [bool]$candidateReceipt.protected_inputs_unchanged -and
            [string]$candidateReceipt.status -ceq 'UE58_EF_CLOTHING_MORPH_V2_COMPILE_PASS' -and
            [string]$candidateReceipt.profile_mode -ceq 'preview_rest_only'
        ) {
            $latestReceipt = $candidate
            $receipt = $candidateReceipt
            break
        }
    }
    if ($null -eq $latestReceipt) {
        throw "No protected PreviewRestOnly compiler PASS exists under $receiptRoot"
    }
    if (
        -not [bool]$receipt.success -or
        -not [bool]$receipt.compile_success -or
        -not [bool]$receipt.validation_success -or
        -not [bool]$receipt.protected_inputs_unchanged -or
        [string]$receipt.status -cne 'UE58_EF_CLOTHING_MORPH_V2_COMPILE_PASS' -or
        [string]$receipt.profile_mode -cne 'preview_rest_only'
    ) {
        throw "The latest PreviewRestOnly receipt is not a protected PASS: $($latestReceipt.FullName)"
    }
    if ([int]$receipt.metrics.compiler_version -ne 25) {
        throw 'The PreviewRestOnly receipt is not compiler V25'
    }
    if (
        [int]$receipt.metrics.morph_binding_count -ne 0 -or
        [int]$receipt.metrics.morph_pair_certificate_count -ne 0 -or
        [int]$receipt.metrics.generated_pair_cell_morph_count -ne 0 -or
        [int]$receipt.metrics.pair_body_probe_count -ne 0 -or
        [int]$receipt.metrics.pair_offset_evaluation_count -ne 0
    ) {
        throw 'PreviewRestOnly compiler PASS unexpectedly contains morph bindings or pair data'
    }
    if (
        [int]$receipt.metrics.penetrating_vertex_count_before -le 0 -or
        [int]$receipt.metrics.penetrating_vertex_count_after -ne 0 -or
        [double]$receipt.metrics.minimum_signed_gap_after_cm -lt 0.55
    ) {
        throw 'PreviewRestOnly compiler PASS does not prove 143->0-style rest-fit repair with >=0.55 cm gap'
    }
    if (
        -not (Test-ExactStringArray `
            -Actual $receipt.metrics.excluded_body_surface_material_slots `
            -Expected $ExpectedSurfaceSlots) -or
        [int]$receipt.metrics.excluded_body_surface_triangle_count -le 0 -or
        -not (Test-ExactStringArray `
            -Actual $receipt.metrics.excluded_body_bone_branches `
            -Expected $ExpectedBoneBranches) -or
        -not (Test-ExactStringArray `
            -Actual $receipt.metrics.excluded_body_morph_prefixes `
            -Expected $ExpectedMorphPrefixes)
    ) {
        throw 'PreviewRestOnly compiler PASS lacks the exact GP exclusion policy'
    }
    $monitoredMorphs = @($receipt.metrics.monitored_body_morph_names)
    if ($monitoredMorphs.Count -eq 0) {
        throw 'PreviewRestOnly compiler PASS has no monitored morph safety set'
    }
    foreach ($morphName in $monitoredMorphs) {
        foreach ($prefix in $ExpectedMorphPrefixes) {
            if ([string]$morphName -clike ($prefix + '*')) {
                throw "Excluded GP morph leaked into the monitored set: $morphName"
            }
        }
    }
    if (@($receipt.metrics.required_weighted_bones).Count -eq 0) {
        throw 'PreviewRestOnly compiler PASS has no weighted-bone contract'
    }

    $buildGuid = ConvertTo-NormalizedGuid -Value ([string]$receipt.metrics.build_guid)
    $derivedObject = [string]$receipt.outputs.derived_garment
    $profileObject = [string]$receipt.outputs.profile
    if ([string]::IsNullOrWhiteSpace($derivedObject) -or [string]::IsNullOrWhiteSpace($profileObject)) {
        throw 'PreviewRestOnly compiler PASS has empty generated outputs'
    }
    $registryObject = '/Game/_Generated/EFClothingMorphV2/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry'
    $derivedFile = Resolve-GameObjectPathFile -ObjectPath $derivedObject
    $profileFile = Resolve-GameObjectPathFile -ObjectPath $profileObject
    $registryFile = Resolve-GameObjectPathFile -ObjectPath $registryObject
    $receiptRow = New-FileBindingRow -FullPath $latestReceipt.FullName
    $binding = [ordered]@{
        schema_version = 1
        profile_mode = 'preview_rest_only'
        generated_utc = (Get-Date).ToUniversalTime().ToString('o')
        compiler_receipt = [ordered]@{
            relative_path = $receiptRow.relative_path
            size_bytes = $receiptRow.size_bytes
            sha256 = $receiptRow.sha256
            generated_utc = [string]$receipt.generated_utc
            status = [string]$receipt.status
            schema_version = [int]$receipt.schema_version
            profile_mode = [string]$receipt.profile_mode
        }
        compiler = [ordered]@{
            version = [int]$receipt.metrics.compiler_version
            build_guid = $buildGuid
        }
        outputs = [ordered]@{
            derived_garment = $derivedObject
            profile = $profileObject
            registry = $registryObject
        }
        assets = [ordered]@{
            derived_garment = New-FileBindingRow -FullPath $derivedFile -ObjectPath $derivedObject
            profile = New-FileBindingRow -FullPath $profileFile -ObjectPath $profileObject
            registry = New-FileBindingRow -FullPath $registryFile -ObjectPath $registryObject
        }
        protected_inputs = [ordered]@{
            unchanged = [bool]$receipt.protected_inputs_unchanged
            hashes_after = $receipt.protected_sha256_after
        }
    }
    Write-JsonFile -Value $binding -Path $CompilerBindingPath
    return $binding
}

function Get-GitPorcelain {
    $lines = @(& git -C $ProjectRoot status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to capture target worktree state'
    }
    return $lines
}

function Get-ProtectedAssetState {
    $relativePaths = @(
        'Content\FullSample\Player.uasset',
        'Content\DazToUnreal\Female\Female.uasset',
        'Content\DazToUnreal\Multiple\Multiple.uasset',
        'Content\DazToUnreal\Multiple\Multiple_Skeleton.uasset',
        'Content\DazToUnreal\Male\Male.uasset',
        'Content\DazToUnreal\UnderWearPanty\UnderWearPanty.uasset',
        'Content\DazToUnreal\UnderWearPanty\UnderWearPanty_Skeleton.uasset',
        'Content\FullSample\GASP\UEFN_Mannequin\Meshes\SK_UEFN_Mannequin.uasset',
        'Content\_Game\Data\EFClothingMorph\DT_EFClothingGarments.uasset'
    )
    $generatedRoot = Join-Path $ProjectRoot 'Content\_Generated\EFClothingMorphV2'
    if (Test-Path -LiteralPath $generatedRoot -PathType Container) {
        $relativePaths += @(
            Get-ChildItem -LiteralPath $generatedRoot -File -Filter '*.uasset' |
                Sort-Object FullName |
                ForEach-Object { $_.FullName.Substring($ProjectRoot.Length).TrimStart('\') }
        )
    }
    $rows = foreach ($relativePath in @($relativePaths | Sort-Object -Unique)) {
        $fullPath = Join-Path $ProjectRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Protected asset is missing: $fullPath"
        }
        $item = Get-Item -LiteralPath $fullPath
        [ordered]@{
            relative_path = $relativePath.Replace('\', '/')
            length = $item.Length
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
                $_.CommandLine.IndexOf(
                    $ProjectFile,
                    [System.StringComparison]::OrdinalIgnoreCase
                ) -ge 0
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
        length = $item.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
        visual_review = 'PENDING_HUMAN_REVIEW'
    }
}

function ConvertTo-SingleQuotedPowerShellLiteral {
    param([Parameter(Mandatory = $true)][string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
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

Add-Type -AssemblyName System.Drawing

$compilerBinding = New-CompilerPassBinding
$compilerBindingSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $CompilerBindingPath).Hash
$summary = [ordered]@{
    schema_version = 1
    run_id = $RunId
    status = 'IN_PROGRESS'
    failure = $null
    project = $ProjectFile
    map = $MapPath
    profile_mode = 'preview_rest_only'
    launch_wrapper = $LaunchWrapper
    renderer = 'D3D12_VISIBLE_NO_NULLRHI'
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    finished_utc = $null
    launcher_pid = $null
    launcher_exit_code = $null
    launcher_process_exit_code = $null
    runtime_result = $null
    screenshots = @()
    protected_assets_unchanged = $null
    git_state_unchanged = $null
    daz_receipt = $null
    compiler_binding = [ordered]@{
        path = $CompilerBindingPath
        sha256 = $compilerBindingSha256
        value = $compilerBinding
    }
    critical_log_matches = @()
    cleanup = 'NOT_STARTED'
    run_dir = $RunDir
}

$gitBefore = Get-GitPorcelain
$assetsBefore = Get-ProtectedAssetState
Write-JsonFile -Value $assetsBefore -Path $BeforeHashesPath

$environmentValues = [ordered]@{
    CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    CODEX_MIGRATION_PIE_SCRIPT = $PythonScript
    CODEX_EF_CLOTHING_V2_QA_DIR = $RunDir
    CODEX_EF_CLOTHING_V2_QA_RESULT = $RuntimeResult
    CODEX_EF_CLOTHING_V2_QA_MAP = $MapPath
    CODEX_EF_CLOTHING_V2_QA_TIMEOUT = [string]($TimeoutSeconds - 45)
    CODEX_EF_CLOTHING_V2_COMPILER_BINDING = $CompilerBindingPath
    CODEX_EF_CLOTHING_V2_COMPILER_BINDING_SHA256 = $compilerBindingSha256
}
$previousEnvironment = [ordered]@{}
$launcher = $null
$forcedCleanup = $false
$lastProgressLine = ''

try {
    $existingEditors = @(Get-ProjectEditorProcesses)
    if ($existingEditors.Count -gt 0) {
        throw "An Unreal Editor already targets this project: $($existingEditors.ProcessId -join ', ')"
    }
    foreach ($entry in $environmentValues.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable(
            $entry.Key,
            'Process'
        )
        [Environment]::SetEnvironmentVariable(
            $entry.Key,
            [string]$entry.Value,
            'Process'
        )
    }

    $additionalArguments = '-NoSplash -NoP4 -Unattended -d3d12 -stdout -FullStdOutLogOutput ' +
        ('-abslog="{0}"' -f $EditorLog)
    if ($additionalArguments -match '(?i)nullrhi') {
        throw 'The visible gameplay runner must never use NullRHI'
    }
    $launchInvocation = '& ' + (ConvertTo-SingleQuotedPowerShellLiteral $LaunchWrapper) +
        ' -ProjectRoot ' + (ConvertTo-SingleQuotedPowerShellLiteral $ProjectRoot) +
        ' -EngineRoot ' + (ConvertTo-SingleQuotedPowerShellLiteral $EngineRoot) +
        ' -AdditionalArguments ' + (ConvertTo-SingleQuotedPowerShellLiteral $additionalArguments) +
        ' -Wait'
    $launchCommand = '$ErrorActionPreference = ''Stop''; $efLauncherExit = 1; try { ' +
        $launchInvocation +
        '; $efLauncherExit = 0 } catch { [Console]::Error.WriteLine($_.Exception.ToString()); ' +
        '$efLauncherExit = 1 } finally { [IO.File]::WriteAllText(' +
        (ConvertTo-SingleQuotedPowerShellLiteral $LauncherExitCodePath) +
        ', [string]$efLauncherExit) }; exit $efLauncherExit'
    $encodedCommand = [Convert]::ToBase64String(
        [Text.Encoding]::Unicode.GetBytes($launchCommand)
    )
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
        $forcedCleanup = Stop-ExactProjectEditors -AllowForce
        Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
        throw "PreviewRestOnly HUB PIE exceeded $TimeoutSeconds seconds"
    }
	# Redirected streams can make HasExited observable before ExitCode is populated.
	$launcher.WaitForExit()
	$launcher.Refresh()
    $summary.launcher_process_exit_code = $launcher.ExitCode
    if (-not (Test-Path -LiteralPath $LauncherExitCodePath -PathType Leaf)) {
        throw "Editor launch wrapper did not publish its exit-code sidecar: $LauncherExitCodePath"
    }
    $launcherExitText = (Get-Content -LiteralPath $LauncherExitCodePath -Raw).Trim()
    if ($launcherExitText -notmatch '^-?\d+$') {
        throw "Editor launch wrapper published an invalid exit code: $launcherExitText"
    }
    $observedLauncherExitCode = [int]$launcherExitText
    $summary.launcher_exit_code = [int]$observedLauncherExitCode
    if ($null -ne $launcher.ExitCode -and [int]$launcher.ExitCode -ne $observedLauncherExitCode) {
        throw "Editor launch process/sidecar exit mismatch: process=$($launcher.ExitCode) sidecar=$observedLauncherExitCode"
    }
    if ([int]$observedLauncherExitCode -ne 0) {
        $launcherError = if (Test-Path -LiteralPath $LauncherStderr) {
            (Get-Content -LiteralPath $LauncherStderr -Raw).Trim()
        }
        else {
            ''
        }
        throw "Editor launch wrapper failed with exit code ${observedLauncherExitCode}: $launcherError"
    }
    $summary.cleanup = 'CLEAN'

    if (-not (Test-Path -LiteralPath $RuntimeResult -PathType Leaf)) {
        throw "Runtime result was not created: $RuntimeResult"
    }
    $runtime = Get-Content -LiteralPath $RuntimeResult -Raw | ConvertFrom-Json
    $summary.runtime_result = $runtime
    if ([string]$runtime.status -cne 'UE58_EF_CLOTHING_MORPH_V2_PREVIEW_REST_HUB_PIE_PASS') {
        throw "Runtime validation failed: $($runtime.failure)"
    }
    if (
        [int]$runtime.schema_version -ne 1 -or
        [string]$runtime.profile_mode -cne 'preview_rest_only' -or
        [int]$runtime.profile.compiler_version -ne 25 -or
        [int]$runtime.profile.morph_binding_count -ne 0 -or
        [int]$runtime.profile.morph_pair_certificate_count -ne 0 -or
        [int]$runtime.profile.generated_pair_cell_morph_count -ne 0
    ) {
        throw 'Runtime result is not the exact PreviewRestOnly no-binding contract'
    }
    if (
        [string]$runtime.compiler_binding.manifest_sha256 -cne $compilerBindingSha256 -or
        -not [bool]$runtime.compiler_binding.loaded_assets_match -or
        (ConvertTo-NormalizedGuid -Value ([string]$runtime.profile.build_guid)) -cne
            [string]$compilerBinding.compiler.build_guid
    ) {
        throw 'Runtime assets are not bound to the exact PreviewRestOnly compiler PASS'
    }
    if (
        -not (Test-ExactStringArray `
            -Actual $runtime.profile.excluded_body_surface_material_slots `
            -Expected $ExpectedSurfaceSlots) -or
        -not (Test-ExactStringArray `
            -Actual $runtime.profile.excluded_body_bone_branches `
            -Expected $ExpectedBoneBranches) -or
        -not (Test-ExactStringArray `
            -Actual $runtime.profile.excluded_body_morph_prefixes `
            -Expected $ExpectedMorphPrefixes) -or
        [int]$runtime.profile.excluded_body_surface_triangle_count -le 0 -or
        [int]$runtime.profile.penetrating_vertex_count_after -ne 0 -or
        [double]$runtime.profile.minimum_signed_gap_after_cm -lt 0.55
    ) {
        throw 'Runtime profile lacks the certified rest-fit/GP-exclusion contract'
    }
    $bodyUsesSelfDriver = (
        [bool]$runtime.runtime_context.body_is_its_own_pose_driver -and
        [string]$runtime.runtime_context.body_direct_leader_component -eq '' -and
        [string]$runtime.runtime_context.effective_pose_driver_component -ceq
            [string]$runtime.runtime_context.body_component -and
        [string]$runtime.runtime_context.effective_pose_driver_mesh -ceq
            '/Game/DazToUnreal/Female/Female'
    )
    $bodyUsesMultipleDriver = (
        [bool]$runtime.runtime_context.body_uses_explicit_multiple_driver -and
        [bool]$runtime.runtime_context.body_direct_leader_is_effective_pose_driver -and
        [string]$runtime.runtime_context.effective_pose_driver_mesh -ceq
            '/Game/DazToUnreal/Multiple/Multiple'
    )
    if (
        [string]$runtime.runtime_context.body_mesh -cne '/Game/DazToUnreal/Female/Female' -or
        (-not $bodyUsesSelfDriver -and -not $bodyUsesMultipleDriver)
    ) {
        throw 'Female effective pose-driver contract is invalid'
    }

    foreach ($checkpoint in $runtime.runtime_checks.PSObject.Properties) {
        $row = $checkpoint.Value
        if (
            [int]$row.runtime.applied -ne 1 -or
            [int]$row.runtime.pending -ne 0 -or
            [int]$row.runtime.coverage_sections -ne 1 -or
            [int]$row.runtime.coverage_refs -ne 1 -or
            [string]$row.garment.mesh -cne [string]$runtime.profile.fitted_garment -or
            [string]$row.garment.leader_pose_component -cne
                [string]$runtime.runtime_context.effective_pose_driver_component -or
            [string]$row.garment.leader_pose_mesh -cne
                [string]$runtime.runtime_context.effective_pose_driver_mesh -or
            [string]$row.garment.skin_weight_profile -cne 'EF_AutoFit' -or
            -not [bool]$row.garment.is_using_skin_weight_profile -or
            [bool]$row.garment.skin_weight_profile_pending -or
            -not [bool]$row.coverage.all_hidden
        ) {
            throw "READY/pose-driver/EF_AutoFit/GP coverage failed at $($checkpoint.Name)"
        }
    }

    $coverageExpectations = [ordered]@{
        pre_equip = [ordered]@{ Hidden = $false; Sections = 0; Refs = 0 }
        equipped_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        idle_front_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        idle_back_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        walk_side_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        walk_back_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        crawl_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        crawl_closeup_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        cvar_disabled_source = [ordered]@{ Hidden = $false; Sections = 0; Refs = 0 }
        cvar_reenabled_ready = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
        final = [ordered]@{ Hidden = $true; Sections = 1; Refs = 1 }
    }
    foreach ($expectation in $coverageExpectations.GetEnumerator()) {
        $property = $runtime.coverage_checks.PSObject.Properties |
            Where-Object { $_.Name -ceq $expectation.Key } |
            Select-Object -First 1
        if ($null -eq $property) {
            throw "Missing GP coverage checkpoint: $($expectation.Key)"
        }
        $row = $property.Value
        $expected = $expectation.Value
        $visibilityPass = if ([bool]$expected.Hidden) {
            [bool]$row.all_hidden
        }
        else {
            [bool]$row.all_shown
        }
        if (
            -not $visibilityPass -or
            [int]$row.runtime.coverage_sections -ne [int]$expected.Sections -or
            [int]$row.runtime.coverage_refs -ne [int]$expected.Refs -or
            [string]$row.slot_name -cne 'Genesis9_GP_Torso' -or
            [int]$row.lod_count -lt 1
        ) {
            throw "GP all-LOD coverage/refcount failed at $($expectation.Key)"
        }
    }
    foreach ($checkpoint in $runtime.monitored_morph_checks.PSObject.Properties) {
        if (
            [string]$checkpoint.Value.status -cne 'PASS_ALL_MONITORED_ZERO' -or
            @($checkpoint.Value.non_zero).Count -ne 0 -or
            [double]$checkpoint.Value.maximum_absolute_weight -gt 0.001
        ) {
            throw "A monitored morph was active during PreviewRestOnly at $($checkpoint.Name)"
        }
    }
    if (
        -not [bool]$runtime.cvar_rollback.disabled.source_restored -or
        [string]$runtime.cvar_rollback.disabled.garment.mesh -cne
            '/Game/DazToUnreal/UnderWearPanty/UnderWearPanty' -or
        -not [bool]$runtime.cvar_rollback.disabled.coverage.all_shown -or
        -not [bool]$runtime.cvar_rollback.reenabled.derived_restored -or
        [string]$runtime.cvar_rollback.reenabled.garment.mesh -cne
            [string]$runtime.profile.fitted_garment -or
        -not [bool]$runtime.cvar_rollback.reenabled.coverage.all_hidden
    ) {
        throw 'Source/fitted rollback or reversible GP coverage did not pass'
    }
    foreach ($checkpoint in $runtime.character_creation.PSObject.Properties) {
        if (@($checkpoint.Value).Count -ne 0) {
            throw "Character Creation appeared in HUB at $($checkpoint.Name)"
        }
    }
    if (
        [string]$runtime.character_creation_early_sampling.status -cne
            'PASS_VISIBLE_CC_ABSENT_FROM_FIRST_VALID_PIE_TICK' -or
        [int]$runtime.character_creation_early_sampling.sample_count -lt 1 -or
        @($runtime.character_creation_early_sampling.violations).Count -ne 0
    ) {
        throw 'Character Creation was not proven absent from first valid HUB PIE tick'
    }
    if (
        [bool]$runtime.acf_interaction.direct_mesh_assignment_used -or
        [bool]$runtime.acf_interaction.direct_equipment_shortcut_used -or
        -not [bool]$runtime.acf_interaction.interact_invoked -or
        -not [bool]$runtime.acf_interaction.pickup_consumed -or
        -not [bool]$runtime.acf_interaction.original_guid_preserved_to_inventory -or
        -not [bool]$runtime.acf_interaction.original_guid_preserved_to_legs_equipment
    ) {
        throw 'Garment was not proven through real ACF interaction/equipment route'
    }
    $pickupGuids = @($runtime.acf_interaction.pickup_guids_before | Sort-Object)
    $inventoryGuidDelta = @($runtime.acf_interaction.inventory_guid_delta | Sort-Object)
    if (
        $pickupGuids.Count -eq 0 -or
        @(Compare-Object -ReferenceObject $pickupGuids -DifferenceObject $inventoryGuidDelta).Count -ne 0 -or
        $inventoryGuidDelta -notcontains [string]$runtime.acf_interaction.equipped_legs_guid
    ) {
        throw 'Pickup GUID delta was not preserved through Inventory to Legs slot'
    }
    if (
        [string]$runtime.initial_visibility_gate.status -cne
            'PASS_OBSERVED_POST_TICK_RENDERABLE_SAMPLES_WERE_EXACT_READY_WITH_GP_HIDDEN' -or
        [int]$runtime.initial_visibility_gate.sample_count -lt 1 -or
        [int]$runtime.initial_visibility_gate.unsafe_renderable_count -ne 0 -or
        -not [bool]$runtime.initial_visibility_gate.first_renderable.safe -or
        -not [bool]$runtime.initial_visibility_gate.first_renderable.coverage.all_hidden
    ) {
        throw 'Observed post-tick fitted/coverage safety gate did not pass'
    }

    $runtimeScreenshotNames = @($runtime.screenshots.PSObject.Properties.Name | Sort-Object)
    $expectedScreenshotNames = @($ExpectedScreenshots | Sort-Object)
    if (@(
            Compare-Object `
                -ReferenceObject $expectedScreenshotNames `
                -DifferenceObject $runtimeScreenshotNames
        ).Count -ne 0) {
        throw 'Runtime screenshot requests do not match required evidence set'
    }
    $summary.screenshots = @(
        foreach ($name in $ExpectedScreenshots) {
            Get-ImageMetadata -Path (Join-Path $RunDir $name)
        }
    )

    if (-not (Test-Path -LiteralPath $EditorLog -PathType Leaf)) {
        throw "Editor log is missing: $EditorLog"
    }
    $logText = Get-Content -LiteralPath $EditorLog -Raw
    $criticalPatterns = [ordered]@{
        fatal = '(?im)^.*Fatal error:.*$'
        ensure = '(?im)^.*Ensure condition failed.*$'
        blueprint_runtime_error = '(?im)^.*Blueprint Runtime Error.*$'
        clothing_v2_reject = '(?im)^.*EFClothingMorphV2 REJECT.*$'
        clothing_v2_error = '(?im)^.*LogEFClothingMorphV2:\s*Error:.*$'
    }
    $criticalRows = foreach ($entry in $criticalPatterns.GetEnumerator()) {
        foreach ($match in [regex]::Matches($logText, $entry.Value)) {
            [ordered]@{ category = $entry.Key; line = $match.Value.Trim() }
        }
    }
    $summary.critical_log_matches = @($criticalRows)
    if ($summary.critical_log_matches.Count -gt 0) {
        throw "Critical Unreal log entries detected: $($summary.critical_log_matches | ConvertTo-Json -Depth 5 -Compress)"
    }

    $summary.daz_receipt = Invoke-DazReceiptVerification
    $summary.status = 'PASS_RUNTIME_EVIDENCE_CAPTURED_VISUAL_REVIEW_PENDING'
}
catch {
    $summary.status = 'FAIL'
    $summary.failure = $_.Exception.Message
}
finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    $remainingEditors = @(Get-ProjectEditorProcesses)
    if ($remainingEditors.Count -gt 0) {
        $forcedCleanup = (Stop-ExactProjectEditors -AllowForce) -or $forcedCleanup
    }
    if ($forcedCleanup) {
        $summary.cleanup = 'FORCED'
        $summary.status = 'FAIL'
        if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) {
            $summary.failure = 'The exact project editor required forced termination'
        }
    }
    elseif ($summary.cleanup -eq 'NOT_STARTED') {
        $summary.cleanup = 'NO_EDITOR_LEFT_RUNNING'
    }

    try {
        $gitAfter = Get-GitPorcelain
        $assetsAfter = Get-ProtectedAssetState
        Write-JsonFile -Value $assetsAfter -Path $AfterHashesPath
        $summary.git_state_unchanged = (($gitBefore -join "`n") -ceq ($gitAfter -join "`n"))
        $summary.protected_assets_unchanged = (
            ($assetsBefore | ConvertTo-Json -Depth 20 -Compress) -ceq
            ($assetsAfter | ConvertTo-Json -Depth 20 -Compress)
        )
        if (-not $summary.git_state_unchanged -or -not $summary.protected_assets_unchanged) {
            $summary.status = 'FAIL'
            if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) {
                $summary.failure = 'Worktree or protected skeletal/catalog assets changed during PIE QA'
            }
        }
    }
    catch {
        $summary.git_state_unchanged = $false
        $summary.protected_assets_unchanged = $false
        $summary.status = 'FAIL'
        if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) {
            $summary.failure = $_.Exception.Message
        }
    }

    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
    Write-JsonFile -Value $summary -Path $SummaryPath
    $screenshotReport = @(
        foreach ($name in $ExpectedScreenshots) {
            "- ``$name``"
        }
    )
    $report = @(
        '# EF Clothing Morph V2 PreviewRestOnly - real HUB PIE smoke QA',
        '',
        "- Status: ``$($summary.status)``",
        "- Map: ``$MapPath``",
        '- Renderer: ``D3D12 visible; NullRHI forbidden``',
        '- Equip path: ``ACF Interaction -> WorldItem -> Inventory -> Equipment -> ArmorSlot``',
        '- Scope: ``rest geometry fit + EF_AutoFit animation smoke; no morph certification``',
        '- Geometry gate: ``compiled penetrations >0 -> 0; minimum signed gap >=0.55 cm``',
        '- Pose driver: ``garment follows Female''s exact effective driver; Multiple remains the protected compatibility reference``',
        '- GP coverage: ``Genesis9_GP_Torso hidden across every LOD at READY; 1 section/1 ref; reversible on CVar rollback``',
        '- Render-safety sampling: ``every observed post-tick sample was suppressed or exact READY + GP hidden; same-frame protection is the C++ OnBeginDraw guard``',
        '- Character Creation: ``absent from first valid HUB PIE tick and later checkpoints``',
        '- Morph safety: ``all monitored body morphs remained zero; harness never changes morph values``',
        "- Compiler binding: ``$CompilerBindingPath`` / ``$compilerBindingSha256``",
        "- Cleanup: ``$($summary.cleanup)``",
        "- Git state unchanged: ``$($summary.git_state_unchanged)``",
        "- Protected assets unchanged: ``$($summary.protected_assets_unchanged)``",
        "- Runtime receipt: ``$RuntimeResult``",
        "- Editor log: ``$EditorLog``",
        "- Failure: ``$($summary.failure)``",
        '',
        '## Gameplay screenshots',
        ''
    ) + $screenshotReport + @(
        '',
        'Automated runtime gates passed only when status is PASS; clipping judgment remains pending human visual review.'
    )
    $report | Set-Content -LiteralPath $ReportPath -Encoding UTF8
}

"RESULT_DIR=$RunDir"
"SUMMARY=$SummaryPath"
"STATUS=$($summary.status)"
if ($summary.status -eq 'FAIL') {
    throw "EF Clothing Morph V2 PreviewRestOnly HUB PIE QA failed: $($summary.failure)"
}
