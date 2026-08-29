[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$MapPath = '/Game/_Game/Hub/HUB',
    [ValidateRange(180, 900)]
    [int]$TimeoutSeconds = 420
)

throw 'The public-root V25 HUB harness is retired. Use Run-EFClothingMorphV26SurfaceRuntimePIE58.ps1 with the single Clothing Director.'

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ProjectFile = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$LaunchWrapper = Join-Path $ProjectRoot 'Tools\Migration\Launch-NoShellForWinterEditor58.ps1'
$ReceiptGuard = Join-Path $ProjectRoot 'Tools\Migration\Repair-DazPluginReceipt58.ps1'
$PythonScript = Join-Path $ProjectRoot 'Tools\ClothingMorphV2\Validate-EFClothingMorphV2HubPIE58.py'
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
    throw 'The gameplay harness must not assign a garment mesh directly'
}
if ($PythonSource -notmatch 'call\(STATE\.interaction,\s*"interact",\s*""\)') {
    throw 'The gameplay harness no longer invokes the real ACF Interact route'
}
if (
    $PythonSource -match 'EXPECTED_CLEARANCE_PRODUCT' -or
    $PythonSource -notmatch 'automatic_clearance_requirement' -or
    $PythonSource -notmatch 'combined_request\s*=\s*max\(manual_request,\s*automatic\["required_multiplier"\]\)'
) {
    throw 'The gameplay harness must resolve clearance as max(manual offset, active-shape automatic tier)'
}

$RunId = Get-Date -Format 'yyyyMMdd_HHmmss'
$RunDir = Join-Path $ProjectRoot "Saved\ClothingMorphV2QA\HubPIE_$RunId"
$RuntimeResult = Join-Path $RunDir 'RuntimeResult.json'
$ProgressLog = Join-Path $RunDir 'Progress.log'
$EditorLog = Join-Path $RunDir 'UnrealEditor_EFClothingMorphV2HubPIE58.log'
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
    '03_back_heavy_fitness_ready.png',
    '03b_back_heavy_fitness_closeup.png',
    '04_front_offset_product.png',
    '05_side_walk_motion.png',
    '05b_side_walk_motion_burst.png',
    '06_back_walk_motion.png',
    '07_side_crawl_heavy_fitness.png',
    '07b_side_crawl_closeup.png',
    '08_cvar_disabled_source_rollback.png',
    '09_cvar_reenabled_ready.png'
)

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

function Test-FiniteNumber {
    param([Parameter(Mandatory = $true)][double]$Value)
    return -not [double]::IsNaN($Value) -and -not [double]::IsInfinity($Value)
}

function Get-CertifiedClearanceTier {
    param(
        [Parameter(Mandatory = $true)]$Profile,
        [Parameter(Mandatory = $true)][double]$Requested
    )
    $minimum = [double]$Profile.certified_clearance_multiplier_min
    $maximum = [double]$Profile.certified_clearance_multiplier_max
    $tierCount = [int]$Profile.certified_clearance_tier_count
    if (
        -not (Test-FiniteNumber -Value $Requested) -or
        -not (Test-FiniteNumber -Value $minimum) -or
        -not (Test-FiniteNumber -Value $maximum) -or
        $tierCount -lt 2 -or
        $maximum -le $minimum
    ) {
        throw 'Cannot quantize an invalid certified clearance request'
    }
    $clamped = [math]::Max($minimum, [math]::Min($maximum, $Requested))
    $step = ($maximum - $minimum) / [double]($tierCount - 1)
    $index = [math]::Ceiling((($clamped - $minimum) / $step) - 0.000001)
    $index = [math]::Max(0, [math]::Min($tierCount - 1, $index))
    return $minimum + ([double]$index * $step)
}

function Resolve-GameObjectPathFile {
    param([Parameter(Mandatory = $true)][string]$ObjectPath)
    $packagePath = ($ObjectPath -split '\.', 2)[0]
    if (-not $packagePath.StartsWith('/Game/', [System.StringComparison]::Ordinal)) {
        throw "Compiler output is not a /Game asset: $ObjectPath"
    }
    $relativePath = $packagePath.Substring('/Game/'.Length).Replace('/', '\') + '.uasset'
    $fullPath = [System.IO.Path]::GetFullPath((Join-Path (Join-Path $ProjectRoot 'Content') $relativePath))
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
        throw "Binding file escapes the target project: $resolved"
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
    $latestReceipt = Get-ChildItem -LiteralPath $receiptRoot -File -Filter 'compiler_receipt_FullCatalog_*.json' |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $latestReceipt) {
        throw "No EF Clothing Morph compiler receipt exists under $receiptRoot"
    }
    $receipt = Get-Content -LiteralPath $latestReceipt.FullName -Raw | ConvertFrom-Json
    if (
        -not [bool]$receipt.success -or
        -not [bool]$receipt.compile_success -or
        -not [bool]$receipt.validation_success -or
        -not [bool]$receipt.protected_inputs_unchanged -or
        [string]$receipt.status -ne 'UE58_EF_CLOTHING_MORPH_V2_COMPILE_PASS'
    ) {
        throw "The latest compiler receipt is not a protected PASS: $($latestReceipt.FullName)"
    }
    if ([int]$receipt.metrics.compiler_version -ne 25) {
        throw "The latest compiler PASS is not V25: $($latestReceipt.FullName)"
    }
    $receiptMonitoredMorphs = @($receipt.metrics.monitored_body_morph_names)
    if (
        [int]$receipt.metrics.certified_morph_pair_count -ne 1 -or
        [int]$receipt.metrics.morph_pair_certificate_count -ne 1 -or
        [int]$receipt.metrics.generated_pair_cell_morph_count -ne 16 -or
        [int]$receipt.metrics.pair_body_probe_count -ne 144 -or
        [int]$receipt.metrics.pair_offset_evaluation_count -ne 1296 -or
        [double]$receipt.metrics.compiled_morph_activation_epsilon -ne 0.0 -or
        $receiptMonitoredMorphs -notcontains 'Body Fitness Mass' -or
        $receiptMonitoredMorphs -notcontains 'Body Heavy'
    ) {
        throw "The latest compiler PASS lacks the exact V25 Fitness+Heavy pair contract: $($latestReceipt.FullName)"
    }
    $buildGuid = ConvertTo-NormalizedGuid -Value ([string]$receipt.metrics.build_guid)
    $derivedObject = [string]$receipt.outputs.derived_garment
    $profileObject = [string]$receipt.outputs.profile
    if ([string]::IsNullOrWhiteSpace($derivedObject) -or [string]::IsNullOrWhiteSpace($profileObject)) {
        throw 'The compiler PASS receipt has empty generated outputs'
    }
    $registryObject = '/Game/_Generated/EFClothingMorphV2/DA_EFClothingFitRegistry.DA_EFClothingFitRegistry'
    $derivedFile = Resolve-GameObjectPathFile -ObjectPath $derivedObject
    $profileFile = Resolve-GameObjectPathFile -ObjectPath $profileObject
    $registryFile = Resolve-GameObjectPathFile -ObjectPath $registryObject
    $receiptRow = New-FileBindingRow -FullPath $latestReceipt.FullName
    $binding = [ordered]@{
        schema_version = 1
        generated_utc = (Get-Date).ToUniversalTime().ToString('o')
        compiler_receipt = [ordered]@{
            relative_path = $receiptRow.relative_path
            size_bytes = $receiptRow.size_bytes
            sha256 = $receiptRow.sha256
            generated_utc = [string]$receipt.generated_utc
            status = [string]$receipt.status
            schema_version = [int]$receipt.schema_version
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
        throw 'Unable to capture the target worktree state'
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
        'Content\FullSample\GASP\UEFN_Mannequin\Meshes\SK_UEFN_Mannequin.uasset'
    )
    $generatedRoot = Join-Path $ProjectRoot 'Content\_Generated\EFClothingMorphV2'
    if (Test-Path -LiteralPath $generatedRoot -PathType Container) {
        $relativePaths += @(
            Get-ChildItem -LiteralPath $generatedRoot -File -Filter '*.uasset' |
                Sort-Object FullName |
                ForEach-Object {
                    $_.FullName.Substring($ProjectRoot.Length).TrimStart('\')
                }
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
    schema_version = 6
    run_id = $RunId
    status = 'IN_PROGRESS'
    failure = $null
    project = $ProjectFile
    map = $MapPath
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
        throw "EF Clothing Morph V2 HUB PIE exceeded $TimeoutSeconds seconds"
    }
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
    if ([string]$runtime.status -ne 'UE58_EF_CLOTHING_MORPH_V2_HUB_PIE_PASS') {
        throw "Runtime validation failed: $($runtime.failure)"
    }
    if (
        [int]$runtime.schema_version -ne 6 -or
        [int]$runtime.expected_compiler_version -ne 25 -or
        [int]$runtime.profile.compiler_version -ne 25
    ) {
        throw "The runtime receipt is not an EF Clothing Morph V25 receipt"
    }
    if (
        [string]$runtime.compiler_binding.manifest_sha256 -ne $compilerBindingSha256 -or
        [string]$runtime.compiler_binding.compiler_receipt.sha256 -ne
            [string]$compilerBinding.compiler_receipt.sha256 -or
        -not [bool]$runtime.compiler_binding.loaded_assets_match -or
        (ConvertTo-NormalizedGuid -Value ([string]$runtime.profile.build_guid)) -ne
            [string]$compilerBinding.compiler.build_guid
    ) {
        throw 'Runtime assets are not bound to this run''s exact compiler PASS receipt/build GUID'
    }
    foreach ($bindingAsset in @('derived_garment', 'profile', 'registry')) {
        $expectedBindingAsset = $compilerBinding.assets.$bindingAsset
        $runtimeBindingAsset = $runtime.compiler_binding.assets.$bindingAsset
        if (
            [string]$runtimeBindingAsset.sha256 -ne [string]$expectedBindingAsset.sha256 -or
            [long]$runtimeBindingAsset.size_bytes -ne [long]$expectedBindingAsset.size_bytes -or
            [string]$runtimeBindingAsset.object_path -ne [string]$expectedBindingAsset.object_path
        ) {
            throw "Runtime compiler binding differs for $bindingAsset"
        }
    }
    if (
        [double]$runtime.profile.certified_clearance_multiplier_min -ne 1.0 -or
        [double]$runtime.profile.certified_clearance_multiplier_max -ne 2.0 -or
        [int]$runtime.profile.certified_clearance_tier_count -ne 9
    ) {
        throw "The V25 certified 1..2 / 9-tier offset contract is missing"
    }
    $monitoredMorphs = @($runtime.profile.monitored_body_morph_names)
    if (
        [double]$runtime.profile.compiled_morph_activation_epsilon -ne 0.0 -or
        $monitoredMorphs -notcontains 'Body Fitness Mass' -or
        $monitoredMorphs -notcontains 'Body Heavy' -or
        @($monitoredMorphs | Select-Object -Unique).Count -ne $monitoredMorphs.Count
    ) {
        throw 'The V25 exact monitored active-set contract is invalid'
    }
    $pairCertificates = @($runtime.profile.morph_pair_certificates)
    if (
        $pairCertificates.Count -ne 1 -or
        [int]$runtime.profile.morph_pair_certificate_count -ne 1 -or
        [int]$runtime.profile.certified_morph_pair_count -ne 1 -or
        [int]$runtime.profile.generated_pair_cell_morph_count -ne 16 -or
        [int]$runtime.profile.pair_body_probe_count -ne 144 -or
        [int]$runtime.profile.pair_offset_evaluation_count -ne 1296
    ) {
        throw 'The V25 Fitness+Heavy pair metrics are invalid'
    }
    $pairCertificate = $pairCertificates[0]
    $pairCells = @($pairCertificate.cells)
    if (
        [string]$pairCertificate.first_body_morph -cne 'Body Fitness Mass' -or
        [string]$pairCertificate.second_body_morph -cne 'Body Heavy' -or
        [int]$pairCertificate.grid_resolution -ne 4 -or
        [int]$pairCertificate.probe_count_per_axis -ne 3 -or
        [int]$pairCertificate.certified_offset_tier_count -ne 9 -or
        [int]$pairCertificate.cell_count -ne 16 -or
        $pairCells.Count -ne 16
    ) {
        throw 'The V25 canonical Fitness+Heavy 4x4/3-probe/9-tier certificate is invalid'
    }
    $actualPairCellKeys = @(
        $pairCells |
            ForEach-Object { '{0},{1}' -f [int]$_.first_cell_index, [int]$_.second_cell_index } |
            Sort-Object -Unique
    )
    $expectedPairCellKeys = @(
        foreach ($firstIndex in 0..3) {
            foreach ($secondIndex in 0..3) {
                '{0},{1}' -f $firstIndex, $secondIndex
            }
        }
    ) | Sort-Object
    if (
        $actualPairCellKeys.Count -ne 16 -or
        @(Compare-Object -ReferenceObject $expectedPairCellKeys -DifferenceObject $actualPairCellKeys).Count -ne 0
    ) {
        throw 'The V25 pair certificate does not contain each 4x4 cell exactly once'
    }
    foreach ($pairCell in $pairCells) {
        $minimumMultiplier = [double]$pairCell.minimum_clearance_multiplier
        $minimumMultiplierTier = Get-CertifiedClearanceTier `
            -Profile $runtime.profile `
            -Requested $minimumMultiplier
        if (
            [string]::IsNullOrWhiteSpace([string]$pairCell.garment_morph) -or
            [int]$pairCell.certified_body_probe_count -ne 9 -or
            [int]$pairCell.certified_offset_evaluation_count -ne 81 -or
            [double]$pairCell.minimum_certified_gap_cm -lt
                ([double]$runtime.profile.compiled_minimum_clearance_cm - 0.001) -or
            -not (Test-FiniteNumber -Value $minimumMultiplier) -or
            [math]::Abs($minimumMultiplier - $minimumMultiplierTier) -gt 0.0001
        ) {
            throw "A V25 pair cell has an invalid morph/probe/evaluation/clearance contract: $($pairCell | ConvertTo-Json -Depth 5 -Compress)"
        }
    }
    $profileBindings = @($runtime.profile.morph_bindings)
    if ($profileBindings.Count -ne [int]$runtime.profile.binding_count) {
        throw 'The runtime receipt does not expose every morph binding for automatic-clearance QA'
    }
    foreach ($profileBinding in $profileBindings) {
        $profileSamples = @($profileBinding.samples)
        if ($profileSamples.Count -ne [int]$profileBinding.sample_count -or $profileSamples.Count -lt 2) {
            throw "A V25 binding has an incomplete automatic-clearance sample table: $($profileBinding.body_morph)"
        }
        foreach ($profileSample in $profileSamples) {
            $sampleMinimum = [double]$profileSample.minimum_clearance_multiplier
            $sampleTier = Get-CertifiedClearanceTier `
                -Profile $runtime.profile `
                -Requested $sampleMinimum
            if (
                -not (Test-FiniteNumber -Value $sampleMinimum) -or
                [math]::Abs($sampleMinimum - $sampleTier) -gt 0.0001
            ) {
                throw "A V25 morph sample minimum is not an exact certified tier: $($profileSample | ConvertTo-Json -Depth 5 -Compress)"
            }
        }
    }
    if (
        [double]$pairCertificate.minimum_certified_gap_cm -lt
            ([double]$runtime.profile.compiled_minimum_clearance_cm - 0.001) -or
        [double]$runtime.profile.minimum_sampled_pair_gap_cm -lt
            ([double]$runtime.profile.compiled_minimum_clearance_cm - 0.001)
    ) {
        throw 'The V25 pair clearance certificate is below the declared minimum'
    }
    if (
        [string]$runtime.profile.shared_skeleton_fingerprint -notmatch '^[0-9A-Fa-f]{32}$' -or
        [string]::IsNullOrWhiteSpace([string]$runtime.profile.shared_skeleton_path)
    ) {
        throw "The V25 shared-skeleton integrity receipt is invalid"
    }
    if (@($runtime.profile.direct_body_morph_targets_present).Count -ne 0) {
        throw "The fitted garment still contains direct body morph targets"
    }
    foreach ($axis in @('x', 'y', 'z')) {
        if ([double]$runtime.profile.compiled_concurrent_bounds_expansion_cm.$axis -le 0.0) {
            throw "The V25 exclusive-state bounds expansion is invalid on axis $axis"
        }
    }
    if ([double]$runtime.profile.compiled_concurrent_sphere_expansion_cm -le 0.0) {
        throw "The V25 exclusive-state sphere expansion is invalid"
    }
    if (
        [string]$runtime.runtime_context.body_mesh -cne '/Game/DazToUnreal/Female/Female' -or
        [string]$runtime.runtime_context.body_direct_leader_mesh -cne '/Game/DazToUnreal/Multiple/Multiple' -or
        [string]$runtime.runtime_context.effective_pose_driver_mesh -cne '/Game/DazToUnreal/Multiple/Multiple' -or
        -not [bool]$runtime.runtime_context.body_direct_leader_is_effective_pose_driver -or
        [string]$runtime.runtime_context.body_direct_leader_component -cne
            [string]$runtime.runtime_context.effective_pose_driver_component
    ) {
        throw 'The exact Female -> direct/effective Multiple pose-driver contract is invalid'
    }
    $poseDriverChain = @($runtime.runtime_context.pose_driver_chain)
    if (
        $poseDriverChain.Count -ne 2 -or
        [string]$poseDriverChain[0].mesh -cne '/Game/DazToUnreal/Female/Female' -or
        [string]$poseDriverChain[1].mesh -cne '/Game/DazToUnreal/Multiple/Multiple'
    ) {
        throw 'The observed pose-driver chain is not exactly Female -> Multiple'
    }
    foreach ($checkpoint in $runtime.runtime_checks.PSObject.Properties) {
        $garment = $checkpoint.Value.garment
        if ([string]$garment.skin_weight_profile -ne 'EF_AutoFit') {
            throw "EF_AutoFit is not the active skin profile at $($checkpoint.Name)"
        }
        if ([string]$garment.leader_pose_mesh -cne '/Game/DazToUnreal/Multiple/Multiple') {
            throw "The garment does not follow Female's effective Multiple driver at $($checkpoint.Name)"
        }
        if (
            -not [bool]$garment.leader_pose_is_effective_pose_driver -or
            [string]$garment.leader_pose_component -cne
                [string]$runtime.runtime_context.effective_pose_driver_component
        ) {
            throw "The garment LeaderPose is not Female's exact effective Multiple component at $($checkpoint.Name)"
        }
        if (
            -not [bool]$garment.is_using_skin_weight_profile -or
            [bool]$garment.skin_weight_profile_pending
        ) {
            throw "EF_AutoFit is not exact Active=True/Pending=False at $($checkpoint.Name)"
        }
        if (
            [int]$checkpoint.Value.runtime.applied -ne 1 -or
            [int]$checkpoint.Value.runtime.pending -ne 0
        ) {
            throw "Runtime READY/Pending is not exact 1/0 at $($checkpoint.Name)"
        }
    }
    $restPair = $runtime.morph_weights.rest_pair_cells_0
    if (
        [int]$restPair.cell_count -ne 16 -or
        [int]$restPair.active_cell_count -ne 0 -or
        @($restPair.cells | Where-Object { [math]::Abs([double]$_.weight) -gt 0.001 }).Count -ne 0
    ) {
        throw 'One or more V25 pair cells remain active at rest'
    }
    $matrixStates = @($runtime.pair_matrix.states)
    $matrixKeys = @(
        $matrixStates |
            ForEach-Object {
                '{0},{1}' -f [int]$_.first_cell_index, [int]$_.second_cell_index
            } |
            Sort-Object -Unique
    )
    if (
        [string]$runtime.pair_matrix.status -cne
            'PASS_ALL_16_CELL_CENTERS_EXCLUSIVE_1_OF_16' -or
        [int]$runtime.pair_matrix.state_count -ne 16 -or
        $matrixStates.Count -ne 16 -or
        $matrixKeys.Count -ne 16 -or
        @(Compare-Object -ReferenceObject $expectedPairCellKeys -DifferenceObject $matrixKeys).Count -ne 0
    ) {
        throw 'The real runtime 4x4 pair-cell matrix is incomplete'
    }
    foreach ($matrixState in $matrixStates) {
        $expectedFirst = [int]$matrixState.first_cell_index
        $expectedSecond = [int]$matrixState.second_cell_index
        $activeCells = @($matrixState.pair.active_cells)
        if (
            [int]$matrixState.first_binding.active_sample_count -ne 0 -or
            [int]$matrixState.second_binding.active_sample_count -ne 0 -or
            [math]::Abs([double]$matrixState.first_binding.original_same_named_garment_weight) -gt 0.001 -or
            [math]::Abs([double]$matrixState.second_binding.original_same_named_garment_weight) -gt 0.001 -or
            @(
                $matrixState.first_binding.samples |
                    Where-Object { [math]::Abs([double]$_.weight) -gt 0.001 }
            ).Count -ne 0 -or
            @(
                $matrixState.second_binding.samples |
                    Where-Object { [math]::Abs([double]$_.weight) -gt 0.001 }
            ).Count -ne 0 -or
            [int]$matrixState.pair.cell_count -ne 16 -or
            [int]$matrixState.pair.active_cell_count -ne 1 -or
            [math]::Abs([double]$matrixState.pair.active_weight_sum - 1.0) -gt 0.001 -or
            [int]$matrixState.pair.expected_cell_indices.first -ne $expectedFirst -or
            [int]$matrixState.pair.expected_cell_indices.second -ne $expectedSecond -or
            $activeCells.Count -ne 1 -or
            [int]$activeCells[0].first_cell_index -ne $expectedFirst -or
            [int]$activeCells[0].second_cell_index -ne $expectedSecond -or
            [math]::Abs([double]$activeCells[0].weight - 1.0) -gt 0.001 -or
            @(
                $matrixState.pair.cells |
                    Where-Object {
                        $isSelected =
                            [int]$_.first_cell_index -eq $expectedFirst -and
                            [int]$_.second_cell_index -eq $expectedSecond
                        if ($isSelected) {
                            [math]::Abs([double]$_.weight - 1.0) -gt 0.001
                        }
                        else {
                            [math]::Abs([double]$_.weight) -gt 0.001
                        }
                    }
            ).Count -ne 0
        ) {
            throw "V25 runtime matrix exclusivity failed at cell $expectedFirst,$expectedSecond"
        }
        $matrixClearance = $matrixState.clearance
        $expectedMatrixCheckpoint = 'pair_matrix_{0}_{1}' -f $expectedFirst, $expectedSecond
        if (
            [string]$matrixClearance.checkpoint -cne $expectedMatrixCheckpoint -or
            [string]$matrixClearance.automatic.source -cne 'pair_cell' -or
            [int]$matrixClearance.automatic.first_cell_index -ne $expectedFirst -or
            [int]$matrixClearance.automatic.second_cell_index -ne $expectedSecond
        ) {
            throw "V25 matrix automatic-clearance provenance failed at cell $expectedFirst,$expectedSecond"
        }
    }

    $expectedClearanceCheckpoints = @(
        'fitted_rest',
        'heavy_fitness_before_manual_offset',
        'before_cvar_rollback',
        'after_out_of_range_recovery',
        'crawl',
        'crawl_closeup',
        'after_cvar_reenable',
        'final'
    )
    foreach ($firstIndex in 0..3) {
        foreach ($secondIndex in 0..3) {
            $expectedClearanceCheckpoints += 'pair_matrix_{0}_{1}' -f $firstIndex, $secondIndex
        }
    }
    $clearanceProperties = @($runtime.offset_product.PSObject.Properties)
    $clearanceCheckpointNames = @($clearanceProperties.Name | Sort-Object)
    $expectedClearanceCheckpointNames = @($expectedClearanceCheckpoints | Sort-Object)
    if (
        $clearanceProperties.Count -ne $expectedClearanceCheckpoints.Count -or
        @(
            Compare-Object `
                -ReferenceObject $expectedClearanceCheckpointNames `
                -DifferenceObject $clearanceCheckpointNames
        ).Count -ne 0
    ) {
        throw 'The automatic/manual clearance checkpoint matrix is incomplete'
    }
    foreach ($clearanceProperty in $clearanceProperties) {
        $clearance = $clearanceProperty.Value
        $automatic = $clearance.automatic
        $manualRequest =
            [double]$runtime.profile.default_clearance_value *
            [double]$clearance.global *
            [double]$clearance.per_garment
        if (
            -not (Test-FiniteNumber -Value ([double]$automatic.required_multiplier)) -or
            [math]::Abs([double]$clearance.manual_requested_offset - $manualRequest) -gt 0.0001
        ) {
            throw "Invalid manual/automatic clearance inputs at $($clearanceProperty.Name)"
        }

        $expectedAutomatic = [double]$runtime.profile.certified_clearance_multiplier_min
        switch ([string]$automatic.source) {
            'fitted_rest' {
                if (@($automatic.active_body_morphs).Count -ne 0) {
                    throw 'Fitted-rest automatic clearance reports active body morphs'
                }
            }
            'pair_cell' {
                $matchingCells = @(
                    $pairCells |
                        Where-Object {
                            [int]$_.first_cell_index -eq [int]$automatic.first_cell_index -and
                            [int]$_.second_cell_index -eq [int]$automatic.second_cell_index
                        }
                )
                if ($matchingCells.Count -ne 1) {
                    throw "Automatic clearance points to no unique pair cell at $($clearanceProperty.Name)"
                }
                $expectedAutomatic = [double]$matchingCells[0].minimum_clearance_multiplier
            }
            'one_dimensional_samples' {
                foreach ($activeSample in @($automatic.active_samples)) {
                    $expectedAutomatic = [math]::Max(
                        $expectedAutomatic,
                        [double]$activeSample.minimum_clearance_multiplier
                    )
                }
            }
            default {
                throw "Unknown automatic clearance source at $($clearanceProperty.Name): $($automatic.source)"
            }
        }
        $automaticTier = Get-CertifiedClearanceTier `
            -Profile $runtime.profile `
            -Requested $expectedAutomatic
        $combinedRequest = [math]::Max($manualRequest, $expectedAutomatic)
        $expectedTier = Get-CertifiedClearanceTier `
            -Profile $runtime.profile `
            -Requested $combinedRequest
        if (
            [math]::Abs([double]$automatic.required_multiplier - $expectedAutomatic) -gt 0.0001 -or
            [math]::Abs($automaticTier - $expectedAutomatic) -gt 0.0001 -or
            [math]::Abs([double]$clearance.combined_request - $combinedRequest) -gt 0.0001 -or
            [math]::Abs([double]$clearance.expected_certified_tier - $expectedTier) -gt 0.0001 -or
            [math]::Abs([double]$clearance.actual_clearance_morph_weight - $expectedTier) -gt 0.002
        ) {
            throw "Runtime clearance is not max(manual, automatic shape tier) at $($clearanceProperty.Name)"
        }
    }
    $comboCheckpoints = @(
        'initial_combo',
        'offset_combo',
        'recovered_combo',
        'crawl_combo',
        'crawl_closeup_combo',
        'cvar_reenabled_combo',
        'final_combo'
    )
    foreach ($comboName in $comboCheckpoints) {
        $comboProperty = $runtime.morph_weights.PSObject.Properties |
            Where-Object { $_.Name -ceq $comboName } |
            Select-Object -First 1
        if ($null -eq $comboProperty) {
            throw "Missing V25 pair runtime proof at $comboName"
        }
        $combo = $comboProperty.Value
        $activeCells = @($combo.pair.active_cells)
        if (
            [math]::Abs([double]$combo.heavy.body_weight - 1.0) -gt 0.01 -or
            [math]::Abs([double]$combo.fitness_mass.body_weight - 1.0) -gt 0.01 -or
            [int]$combo.heavy.active_sample_count -ne 0 -or
            [int]$combo.fitness_mass.active_sample_count -ne 0 -or
            [int]$combo.pair.active_cell_count -ne 1 -or
            [math]::Abs([double]$combo.pair.active_weight_sum - 1.0) -gt 0.001 -or
            [int]$combo.pair.expected_cell_indices.first -ne 3 -or
            [int]$combo.pair.expected_cell_indices.second -ne 3 -or
            $activeCells.Count -ne 1 -or
            [int]$activeCells[0].first_cell_index -ne 3 -or
            [int]$activeCells[0].second_cell_index -ne 3 -or
            [math]::Abs([double]$activeCells[0].weight - 1.0) -gt 0.001 -or
            @(
                $combo.pair.cells |
                    Where-Object {
                        $isSelected =
                            [int]$_.first_cell_index -eq 3 -and
                            [int]$_.second_cell_index -eq 3
                        if ($isSelected) {
                            [math]::Abs([double]$_.weight - 1.0) -gt 0.001
                        }
                        else {
                            [math]::Abs([double]$_.weight) -gt 0.001
                        }
                    }
            ).Count -ne 0
        ) {
            throw "V25 pair-cell exclusivity failed at $comboName"
        }
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
        throw 'Character Creation was not proven absent from the first valid HUB PIE tick'
    }
    if (
        -not [bool]$runtime.out_of_range_suppression.suppressed.render_suppressed -or
        -not [bool]$runtime.out_of_range_suppression.recovered.certified_values_restored
    ) {
        throw "Out-of-range morph fail-suppression/recovery was not proven"
    }
    if (
        -not [bool]$runtime.cvar_rollback.reenabled.per_garment_multiplier_preserved -or
        [math]::Abs(
            [double]$runtime.cvar_rollback.disabled.expected_offset_after_reenable -
            [double]$runtime.cvar_rollback.reenabled.offset.expected_certified_tier
        ) -gt 0.0001
    ) {
        throw "The per-garment offset multiplier did not survive CVar rollback"
    }
    if (
        [bool]$runtime.acf_interaction.direct_mesh_assignment_used -or
        [bool]$runtime.acf_interaction.direct_equipment_shortcut_used -or
        -not [bool]$runtime.acf_interaction.interact_invoked -or
        -not [bool]$runtime.acf_interaction.pickup_consumed -or
        -not [bool]$runtime.acf_interaction.original_guid_preserved_to_inventory -or
        -not [bool]$runtime.acf_interaction.original_guid_preserved_to_legs_equipment
    ) {
        throw "The garment was not proven through the real ACF interaction/equipment route"
    }
    $pickupGuids = @($runtime.acf_interaction.pickup_guids_before | Sort-Object)
    $inventoryGuidDelta = @($runtime.acf_interaction.inventory_guid_delta | Sort-Object)
    if (
        $pickupGuids.Count -eq 0 -or
        @(Compare-Object -ReferenceObject $pickupGuids -DifferenceObject $inventoryGuidDelta).Count -ne 0 -or
        $inventoryGuidDelta -notcontains [string]$runtime.acf_interaction.equipped_legs_guid
    ) {
        throw 'The real pickup GUID delta was not preserved through Inventory to the Legs slot'
    }
    if (
        [string]$runtime.initial_visibility_gate.status -ne
            'PASS_OBSERVED_POST_TICK_RENDERABLE_SAMPLES_WERE_EXACT_READY' -or
        [int]$runtime.initial_visibility_gate.sample_count -lt 2 -or
        [int]$runtime.initial_visibility_gate.unsafe_renderable_count -ne 0 -or
        -not [bool]$runtime.initial_visibility_gate.first_renderable.safe
    ) {
        throw 'The observed post-tick Interact-to-READY render safety gate did not pass'
    }
    $runtimeScreenshotNames = @($runtime.screenshots.PSObject.Properties.Name | Sort-Object)
    $expectedScreenshotNames = @($ExpectedScreenshots | Sort-Object)
    if (@(Compare-Object -ReferenceObject $expectedScreenshotNames -DifferenceObject $runtimeScreenshotNames).Count -ne 0) {
        throw 'Runtime screenshot requests do not match the required evidence set'
    }

    $screenshotRows = foreach ($name in $ExpectedScreenshots) {
        Get-ImageMetadata -Path (Join-Path $RunDir $name)
    }
    $summary.screenshots = @($screenshotRows)

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
            [ordered]@{
                category = $entry.Key
                line = $match.Value.Trim()
            }
        }
    }
    $summary.critical_log_matches = @($criticalRows)
    if ($summary.critical_log_matches.Count -gt 0) {
        throw "Critical Unreal log entries were detected: $($summary.critical_log_matches | ConvertTo-Json -Depth 5 -Compress)"
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
        $summary.git_state_unchanged = (
            ($gitBefore -join "`n") -ceq ($gitAfter -join "`n")
        )
        $summary.protected_assets_unchanged = (
            ($assetsBefore | ConvertTo-Json -Depth 20 -Compress) -ceq
            ($assetsAfter | ConvertTo-Json -Depth 20 -Compress)
        )
        if (-not $summary.git_state_unchanged -or -not $summary.protected_assets_unchanged) {
            $summary.status = 'FAIL'
            if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) {
                $summary.failure = 'Worktree or protected skeletal assets changed during PIE QA'
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

    $screenshotReport = @()
    foreach ($name in $ExpectedScreenshots) {
        $screenshotReport += "- ``$name``"
    }
    $report = @(
        '# EF Clothing Morph V25 - real HUB PIE QA',
        '',
        "- Status: ``$($summary.status)``",
        "- Map: ``$MapPath``",
        '- Renderer: ``D3D12 visible; NullRHI forbidden``',
        '- Equip path: ``ACF Interaction -> WorldItem Gather -> Inventory -> Equipment -> ArmorSlot``',
        '- Contract: ``compiler 25; exclusive 4x4 Fitness+Heavy pair cells; 3x3 probes/cell; 1..2 / 9 offset tiers``',
        '- Pose driver: ``exact body Female -> direct/effective leader Multiple; garment follows that exact Multiple component``',
        "- Compiler binding: ``$CompilerBindingPath`` / ``$compilerBindingSha256``",
        '- Render-safety sampling: ``sampled every observed post-tick from ACF Interact through exact READY; same-frame protection is the C++ OnBeginDraw guard``',
        '- ACF identity gate: ``original pickup GUID -> inventory delta -> equipped Legs GUID; pickup consumed``',
        '- Stress pose: ``Body Heavy=1 + Body Fitness Mass=1``',
        '- Pair proof: ``all 16 cell centers exercised; selected cell = 1; other 15 = 0; every Heavy/Fitness 1D sample = 0``',
        '- Pair geometry: ``16 cells; 144 body probes; 1296 offset evaluations; activation epsilon 0``',
        '- Dynamic offset: ``every rest/sample/cell state resolves upward to max(manual request, compiler-certified automatic tier)``',
        '- Character Creation: ``visibly absent on every early sample from the first valid PIE tick and all later checkpoints``',
        '- Safety gate: ``out-of-range morph hides the garment, then recovers at certified values``',
        '- Rollback: ``source mesh restored; per-garment 1.25 offset and active-shape automatic floor preserved for re-enable``',
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
        'The added back, walk-burst, and crawl closeups strengthen visual review; human judgment remains pending.'
    )
    $report | Set-Content -LiteralPath $ReportPath -Encoding UTF8
}

"RESULT_DIR=$RunDir"
"SUMMARY=$SummaryPath"
"STATUS=$($summary.status)"
if ($summary.status -eq 'FAIL') {
    throw "EF Clothing Morph V2 HUB PIE QA failed: $($summary.failure)"
}
