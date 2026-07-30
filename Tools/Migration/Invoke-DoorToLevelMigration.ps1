[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$Engine57Root = 'D:\Unreal Engine 5\Library\UE_5.7',
    [string]$Engine58Root = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$RunId = '',
    [switch]$AllowKnownAcfuPdbDrift
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "DOOR_TO_LEVEL_ORCHESTRATOR_FAIL: $Message"
    }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith(
        $fullRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Get-Sha256OrNull {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-JsonEvidence {
    param(
        [string]$Path,
        [string]$ExpectedStatus,
        [string]$ExpectedRunId,
        [string]$ExpectedStage = ''
    )
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "Expected evidence is absent: $Path"
    $evidence = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    Assert-True ([string]$evidence.status -eq $ExpectedStatus) "Evidence status differs: $Path"
    Assert-True ([string]$evidence.run_id -eq $ExpectedRunId) "Evidence RunId differs: $Path"
    if (-not [string]::IsNullOrWhiteSpace($ExpectedStage)) {
        Assert-True ([string]$evidence.stage -eq $ExpectedStage) "Evidence stage differs: $Path"
    }
}

function Assert-AuditedProjectsClosed {
    param([string]$ResolvedSource, [string]$ResolvedTarget)
    try {
        $processes = @(
            Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%.exe'"
        )
    }
    catch {
        throw "DOOR_TO_LEVEL_ORCHESTRATOR_FAIL: Cannot query Unreal process command lines; failing closed: $($_.Exception.Message)"
    }

    $needles = @(
        $ResolvedSource,
        $ResolvedSource.Replace('\', '/'),
        (Join-Path $ResolvedSource 'ACFSample.uproject'),
        'ACFSample.uproject',
        $ResolvedTarget,
        $ResolvedTarget.Replace('\', '/'),
        (Join-Path $ResolvedTarget 'NoShellForWinter.uproject'),
        'NoShellForWinter.uproject'
    )
    foreach ($process in $processes) {
        $commandLine = [string]$process.CommandLine
        if ([string]::IsNullOrWhiteSpace($commandLine)) {
            throw "DOOR_TO_LEVEL_ORCHESTRATOR_FAIL: Unreal process $($process.ProcessId) has an unreadable command line; failing closed."
        }
        foreach ($needle in $needles) {
            if ($commandLine.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "DOOR_TO_LEVEL_ORCHESTRATOR_FAIL: Audited project is already open in Unreal process $($process.ProcessId). Close it before migration."
            }
        }
    }
}

function Invoke-CheckedScript {
    param([string]$Path, [hashtable]$Arguments)
    & $Path @Arguments
    if (-not $?) {
        throw "DOOR_TO_LEVEL_ORCHESTRATOR_FAIL: Script failed: $Path"
    }
}

function Invoke-UnrealPython {
    param(
        [string]$Executable,
        [string]$Project,
        [string]$Script,
        [string]$LogPath,
        [string]$Label
    )
    Assert-True (Test-Path -LiteralPath $Executable -PathType Leaf) "$Label executable is absent: $Executable"
    Assert-True (Test-Path -LiteralPath $Project -PathType Leaf) "$Label project is absent: $Project"
    Assert-True (Test-Path -LiteralPath $Script -PathType Leaf) "$Label Python script is absent: $Script"
    $arguments = @(
        $Project,
        '-run=pythonscript',
        ("-script={0}" -f $Script),
        '-EnablePlugins=PythonScriptPlugin',
        '-unattended',
        '-nop4',
        '-nosplash',
        '-nullrhi',
        '-NoSound',
        '-stdout',
        '-FullStdOutLogOutput',
        ("-abslog={0}" -f $LogPath)
    )
    & $Executable @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "DOOR_TO_LEVEL_ORCHESTRATOR_FAIL: $Label exited with code $LASTEXITCODE. Log: $LogPath"
    }
}

if ([string]::IsNullOrWhiteSpace($RunId)) {
    $RunId = '{0}_{1}' -f [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss_fff'), ([Guid]::NewGuid().ToString('N').Substring(0, 8))
}
Assert-True ($RunId -match '^[A-Za-z0-9][A-Za-z0-9_-]{7,63}$') 'RunId format is invalid.'
Assert-True (-not $RunId.Contains('..')) 'RunId contains a forbidden traversal token.'

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$engine57 = (Resolve-Path -LiteralPath $Engine57Root).Path.TrimEnd('\')
$engine58 = (Resolve-Path -LiteralPath $Engine58Root).Path.TrimEnd('\')
$targetProject = Join-Path $target 'NoShellForWinter.uproject'
$sourceProject = Join-Path $source 'ACFSample.uproject'
$ue57 = Join-Path $engine57 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$ue58 = Join-Path $engine58 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4\DoorToLevel'
$runsRoot = Join-Path $phaseRoot 'Runs'
$runRoot = Join-Path $runsRoot $RunId
$gateRoot = Join-Path $runRoot 'Gates'
$logRoot = Join-Path $runRoot 'Logs'
$statePath = Join-Path $runRoot 'OrchestratorState.json'
$gateScript = Join-Path $target 'Tools\Migration\Test-DoorToLevelMigrationGates.ps1'
$prepareScript = Join-Path $target 'Tools\Migration\Prepare-DoorToLevel57Harness.ps1'
$validate57Script = Join-Path $target 'Tools\Migration\Validate-DoorToLevelVisualAssets57.py'
$migrate57Script = Join-Path $target 'Tools\Migration\Migrate-DoorToLevelVisualAssets57.py'
$rebuild58Script = Join-Path $target 'Tools\Migration\Rebuild-Validate-DoorToLevel58.py'
$receiptPath = Join-Path $runRoot 'DoorToLevel57HarnessReceipt.json'
$validationPath = Join-Path $runRoot 'DoorToLevelVisualAssets57Validation.json'
$migrationPath = Join-Path $runRoot 'DoorToLevelVisualAssets57Migration.json'
$rebuildPath = Join-Path $runRoot 'DoorToLevel58Rebuild.json'
$preStageGatePath = Join-Path $gateRoot 'PRE_STAGE_SafetyGate.json'
$preMigrationGatePath = Join-Path $gateRoot 'PRE_MIGRATION57_SafetyGate.json'
$postMigrationGatePath = Join-Path $gateRoot 'POST_MIGRATION57_SafetyGate.json'
$postResaveGatePath = Join-Path $gateRoot 'POST_RESAVE58_SafetyGate.json'
$harnessProject = Join-Path $runRoot 'Harness57\DoorToLevelVisual57Harness.uproject'
$harnessContent = Join-Path $runRoot 'Harness57\Content'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-Path -LiteralPath $sourceProject -PathType Leaf) 'Source project descriptor is absent.'
Assert-True (Test-Path -LiteralPath $targetProject -PathType Leaf) 'Target project descriptor is absent.'
Assert-True (Test-IsUnderRoot -Path $runRoot -Root $runsRoot) 'Run root escapes target Saved/Migration.'
Assert-True (-not (Test-Path -LiteralPath $runRoot)) 'Immutable RunId already exists; use a new RunId.'
foreach ($requiredScript in @($gateScript, $prepareScript, $validate57Script, $migrate57Script, $rebuild58Script)) {
    Assert-True (Test-Path -LiteralPath $requiredScript -PathType Leaf) "Migration script is absent: $requiredScript"
}
Assert-True (Test-Path -LiteralPath $ue57 -PathType Leaf) 'UE 5.7 commandlet executable is absent.'
Assert-True (Test-Path -LiteralPath $ue58 -PathType Leaf) 'UE 5.8 commandlet executable is absent.'

New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$script:history = @()
$script:currentStep = 'PREFLIGHT'
$script:liveMutationStarted = $false

function Write-OrchestratorState {
    param([string]$Status, [string]$Message, [string]$Recovery)
    $script:history += [ordered]@{
        utc = [DateTime]::UtcNow.ToString('o')
        step = $script:currentStep
        status = $Status
        message = $Message
    }
    $payload = [ordered]@{
        schema_version = 1
        generated_utc = [DateTime]::UtcNow.ToString('o')
        status = $Status
        run_id = $RunId
        current_step = $script:currentStep
        source_root = $source
        target_root = $target
        run_root = $runRoot
        live_target_mutation_started = $script:liveMutationStarted
        known_acfu_pdb_drift_quarantine = [bool]$AllowKnownAcfuPdbDrift
        exact_visual_allowlist = @(
            '/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_BaseMaterial',
            '/Game/Calysto/Dungeon/Demo/LowPoly/Material/M_Metal',
            '/Game/Calysto/Dungeon/Demo/LowPoly/Texture/T_Palette',
            '/Game/Calysto/Dungeon/Mesh/DungeonMesh/SM_SquaredArchedWoodenDoors'
        )
        rebuilt_blueprint = '/Game/Procedural/DoorToLevel'
        history = $script:history
        recovery = $Recovery
        evidence = [ordered]@{
            pre_stage_gate = [ordered]@{ path = $preStageGatePath; sha256 = Get-Sha256OrNull -Path $preStageGatePath }
            receipt = [ordered]@{ path = $receiptPath; sha256 = Get-Sha256OrNull -Path $receiptPath }
            validation57 = [ordered]@{ path = $validationPath; sha256 = Get-Sha256OrNull -Path $validationPath }
            pre_migration_gate = [ordered]@{ path = $preMigrationGatePath; sha256 = Get-Sha256OrNull -Path $preMigrationGatePath }
            migration57 = [ordered]@{ path = $migrationPath; sha256 = Get-Sha256OrNull -Path $migrationPath }
            post_migration_gate = [ordered]@{ path = $postMigrationGatePath; sha256 = Get-Sha256OrNull -Path $postMigrationGatePath }
            rebuild58 = [ordered]@{ path = $rebuildPath; sha256 = Get-Sha256OrNull -Path $rebuildPath }
            post_resave_gate = [ordered]@{ path = $postResaveGatePath; sha256 = Get-Sha256OrNull -Path $postResaveGatePath }
        }
        pending_after_this_orchestrator = @('PIE', 'visual QA', 'cook', 'packaged validation')
    }
    $payload | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $statePath -Encoding UTF8
}

$environmentNames = @(
    'CODEX_DOOR_RUN_ID',
    'CODEX_DOOR_RUN_ROOT',
    'CODEX_DOOR57_RECEIPT',
    'CODEX_DOOR57_VALIDATION_EVIDENCE',
    'CODEX_DOOR57_MIGRATION_EVIDENCE',
    'CODEX_DOOR57_PRE_MIGRATION_GATE',
    'CODEX_DOOR57_DESTINATION',
    'CODEX_EXPECTED_TARGET_ROOT',
    'CODEX_EXPECTED_HARNESS_CONTENT'
)
$savedEnvironment = @{}
foreach ($name in $environmentNames) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'Immutable run created; preflight begins.' -Recovery 'No live target Content mutation has started.'

    $script:currentStep = 'PROJECT_CLOSED_PREFLIGHT'
    Assert-AuditedProjectsClosed -ResolvedSource $source -ResolvedTarget $target
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'Source and target projects are not open in Unreal.' -Recovery 'No live target Content mutation has started.'

    $script:currentStep = 'PRE_STAGE_GATE'
    Invoke-CheckedScript -Path $gateScript -Arguments @{
        Stage = 'PRE_STAGE'; RunId = $RunId; SourceRoot = $source; TargetRoot = $target; AllowKnownAcfuPdbDrift = [bool]$AllowKnownAcfuPdbDrift
    }
    Assert-JsonEvidence -Path $preStageGatePath -ExpectedStatus 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS' -ExpectedRunId $RunId -ExpectedStage 'PRE_STAGE'
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'PRE_STAGE source/protected gate passed.' -Recovery 'No live target Content mutation has started.'

    $script:currentStep = 'PREPARE_57_HARNESS'
    Invoke-CheckedScript -Path $prepareScript -Arguments @{
        RunId = $RunId; SourceRoot = $source; TargetRoot = $target
    }
    Assert-True (Test-Path -LiteralPath $harnessProject -PathType Leaf) 'Harness project was not created.'
    Assert-JsonEvidence -Path $receiptPath -ExpectedStatus 'ISOLATED_DOOR_VISUAL57_HARNESS_PASS' -ExpectedRunId $RunId
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'Detached UE 5.7 harness prepared.' -Recovery 'Harness lives under Saved/Migration and may be retained as evidence.'

    [Environment]::SetEnvironmentVariable('CODEX_DOOR_RUN_ID', $RunId, 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_DOOR_RUN_ROOT', $runRoot, 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_DOOR57_RECEIPT', $receiptPath, 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_DOOR57_VALIDATION_EVIDENCE', $validationPath, 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_DOOR57_MIGRATION_EVIDENCE', $migrationPath, 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_DOOR57_PRE_MIGRATION_GATE', $preMigrationGatePath, 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_DOOR57_DESTINATION', (Join-Path $target 'Content'), 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_EXPECTED_TARGET_ROOT', $target, 'Process')
    [Environment]::SetEnvironmentVariable('CODEX_EXPECTED_HARNESS_CONTENT', $harnessContent, 'Process')

    $script:currentStep = 'UE57_READ_ONLY_VALIDATE'
    Invoke-UnrealPython -Executable $ue57 -Project $harnessProject -Script $validate57Script `
        -LogPath (Join-Path $logRoot 'UE57_DoorVisualValidation.log') -Label 'UE 5.7 read-only validation'
    Assert-JsonEvidence -Path $validationPath -ExpectedStatus 'UE57_DOOR_VISUAL_READ_ONLY_LOAD_PASS' -ExpectedRunId $RunId
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'UE 5.7 read-only validation passed.' -Recovery 'No live target Content mutation has started.'

    $script:currentStep = 'PRE_MIGRATION57_GATE'
    Assert-AuditedProjectsClosed -ResolvedSource $source -ResolvedTarget $target
    Invoke-CheckedScript -Path $gateScript -Arguments @{
        Stage = 'PRE_MIGRATION57'; RunId = $RunId; SourceRoot = $source; TargetRoot = $target; AllowKnownAcfuPdbDrift = [bool]$AllowKnownAcfuPdbDrift
    }
    Assert-JsonEvidence -Path $preMigrationGatePath -ExpectedStatus 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS' -ExpectedRunId $RunId -ExpectedStage 'PRE_MIGRATION57'
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'Immediate PRE_MIGRATION57 source/protected gate passed.' -Recovery 'No live target Content mutation has started.'

    $script:currentStep = 'UE57_ASSETTOOLS_MIGRATE'
    Assert-AuditedProjectsClosed -ResolvedSource $source -ResolvedTarget $target
    $script:liveMutationStarted = $true
    Invoke-UnrealPython -Executable $ue57 -Project $harnessProject -Script $migrate57Script `
        -LogPath (Join-Path $logRoot 'UE57_DoorVisualMigration.log') -Label 'UE 5.7 exact AssetTools migration'
    Assert-JsonEvidence -Path $migrationPath -ExpectedStatus 'ASSETTOOLS_EXACT_DOOR_VISUAL_MIGRATION_PASS' -ExpectedRunId $RunId
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'UE 5.7 AssetTools created exactly the four visual packages.' -Recovery 'If a later gate fails, do not raw-delete assets; use Unreal Editor for any cleanup.'

    $script:currentStep = 'POST_MIGRATION57_GATE'
    Invoke-CheckedScript -Path $gateScript -Arguments @{
        Stage = 'POST_MIGRATION57'; RunId = $RunId; SourceRoot = $source; TargetRoot = $target; AllowKnownAcfuPdbDrift = [bool]$AllowKnownAcfuPdbDrift
    }
    Assert-JsonEvidence -Path $postMigrationGatePath -ExpectedStatus 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS' -ExpectedRunId $RunId -ExpectedStage 'POST_MIGRATION57'
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'Immediate POST_MIGRATION57 source/protected gate passed.' -Recovery 'The four visual assets now exist in live target Content.'

    $script:currentStep = 'UE58_REBUILD_RESAVE_RELOAD'
    Assert-AuditedProjectsClosed -ResolvedSource $source -ResolvedTarget $target
    Invoke-UnrealPython -Executable $ue58 -Project $targetProject -Script $rebuild58Script `
        -LogPath (Join-Path $logRoot 'UE58_DoorRebuildResaveReload.log') -Label 'UE 5.8 DoorToLevel rebuild/resave/reload'
    Assert-JsonEvidence -Path $rebuildPath -ExpectedStatus 'UE58_DOOR_TO_LEVEL_REBUILD_RESAVE_RELOAD_PASS' -ExpectedRunId $RunId
    Write-OrchestratorState -Status 'IN_PROGRESS' -Message 'UE 5.8 rebuilt the thin Blueprint and resaved/reloaded the visual closure.' -Recovery 'If the final gate fails, preserve evidence and perform cleanup only through Unreal Editor.'

    $script:currentStep = 'POST_RESAVE58_GATE'
    Invoke-CheckedScript -Path $gateScript -Arguments @{
        Stage = 'POST_RESAVE58'; RunId = $RunId; SourceRoot = $source; TargetRoot = $target; AllowKnownAcfuPdbDrift = [bool]$AllowKnownAcfuPdbDrift
    }
    Assert-JsonEvidence -Path $postResaveGatePath -ExpectedStatus 'DOOR_TO_LEVEL_SOURCE_PROTECTED_SAFETY_PASS' -ExpectedRunId $RunId -ExpectedStage 'POST_RESAVE58'
    Write-OrchestratorState -Status 'DOOR_ASSET_PHASE_PASS_RUNTIME_PENDING' -Message 'All chained asset migration/rebuild gates passed.' -Recovery 'No cleanup required. PIE, visual QA, cook, and packaged validation remain pending.'

    Write-Host "DOOR_TO_LEVEL_ORCHESTRATOR_PASS_RUNTIME_PENDING: $RunId"
    Write-Host "State: $statePath"
}
catch {
    $message = $_.Exception.Message
    $recovery = if ($script:liveMutationStarted) {
        'STOP. Preserve the run evidence. Never use raw filesystem deletion for .uasset/.umap. Inspect the exact allowlist in Unreal Editor 5.8; if recovery is required, quarantine or remove only through Unreal Editor, re-run protected/source gates, and start a new immutable RunId.'
    }
    else {
        'No live target Content mutation was authorized. Preserve the failed run evidence and start a new RunId after correcting the gate failure.'
    }
    try {
        Write-OrchestratorState -Status 'FAILED_FAIL_CLOSED' -Message $message -Recovery $recovery
    }
    catch {
        Write-Warning "Could not write orchestrator failure evidence: $($_.Exception.Message)"
    }
    throw
}
finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
}
