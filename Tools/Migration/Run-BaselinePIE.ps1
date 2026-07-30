[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$MapPath = '/Game/FullSample/Test',
    [int]$EditorReadyTimeoutSeconds = 180,
    [int]$PieReadyTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}

$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ProjectFile = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'

if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) {
    throw "Project file not found: $ProjectFile"
}
if (-not (Test-Path -LiteralPath $EditorExe -PathType Leaf)) {
    throw "Unreal Editor not found: $EditorExe"
}

$RunId = Get-Date -Format 'yyyyMMdd_HHmmss'
$RunDir = Join-Path $ProjectRoot "Saved\Migration\PIE_Baseline\$RunId"
$EditorLog = Join-Path $RunDir 'UnrealEditor_Baseline.log'
$ScreenshotPath = Join-Path $RunDir 'PIE_Test_Visible.png'
$SummaryPath = Join-Path $RunDir 'Summary.json'
$ReportPath = Join-Path $RunDir 'BaselinePIE.md'
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

$script:RpcId = 0
$script:EvidenceSequence = 0
$script:McpUri = $null
$script:Headers = $null
$script:EditorProcess = $null
$script:Cleanup = 'NOT_STARTED'
$script:PieStarted = $false

function Write-JsonFile {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $Value | ConvertTo-Json -Depth 80 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-SafeFileToken {
    param([string]$Value)
    return (($Value -replace '[^A-Za-z0-9_.-]', '_').Trim('_'))
}

function Save-ToolEvidence {
    param(
        [string]$ToolName,
        [hashtable]$Arguments,
        $Response
    )
    $script:EvidenceSequence++
    $action = if ($Arguments.ContainsKey('action')) { [string]$Arguments.action } else { 'call' }
    $name = '{0:D3}_{1}_{2}.json' -f $script:EvidenceSequence, (Get-SafeFileToken $ToolName), (Get-SafeFileToken $action)
    $path = Join-Path $RunDir $name
    Write-JsonFile -Value $Response -Path $path
    return $path
}

function Invoke-UecpRpc {
    param(
        [Parameter(Mandatory = $true)][string]$Method,
        [Parameter(Mandatory = $true)]$Params
    )
    $script:RpcId++
    $payload = [ordered]@{
        jsonrpc = '2.0'
        id = $script:RpcId
        method = $Method
        params = $Params
    }
    $body = $payload | ConvertTo-Json -Depth 80 -Compress
    $response = Invoke-RestMethod -Uri $script:McpUri -Method Post -Headers $script:Headers -ContentType 'application/json' -Body $body -TimeoutSec 180
    if ($response.PSObject.Properties.Name -contains 'error' -and $null -ne $response.error) {
        throw "MCP RPC error for $Method`: $($response.error | ConvertTo-Json -Depth 20 -Compress)"
    }
    return $response
}

function Convert-ToolText {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $null
    }
    try {
        return ($Text | ConvertFrom-Json)
    }
    catch {
        return $Text
    }
}

function Invoke-UecpTool {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][hashtable]$Arguments,
        [switch]$Record
    )
    $rpc = Invoke-UecpRpc -Method 'tools/call' -Params ([ordered]@{ name = $Name; arguments = $Arguments })
    $result = $rpc.result
    if ($null -eq $result) {
        throw "Tool $Name returned no result"
    }
    if ($result.PSObject.Properties.Name -contains 'isError' -and [bool]$result.isError) {
        throw "Tool $Name failed: $($result | ConvertTo-Json -Depth 30 -Compress)"
    }

    $texts = @()
    if ($result.PSObject.Properties.Name -contains 'content') {
        foreach ($block in @($result.content)) {
            if ($null -ne $block -and $block.PSObject.Properties.Name -contains 'text') {
                $texts += [string]$block.text
            }
        }
    }
    $text = $texts -join "`n"
    $data = Convert-ToolText -Text $text
    $wrapper = [ordered]@{
        tool = $Name
        arguments = $Arguments
        text = $text
        data = $data
        rpc = $rpc
    }
    if ($Record) {
        [void](Save-ToolEvidence -ToolName $Name -Arguments $Arguments -Response $wrapper)
    }
    return [pscustomobject]$wrapper
}

function Find-DeepValue {
    param(
        $Object,
        [string[]]$Names,
        [int]$Depth = 0
    )
    if ($null -eq $Object -or $Depth -gt 20) {
        return $null
    }

    if ($Object -is [System.Collections.IDictionary]) {
        foreach ($key in $Object.Keys) {
            if ($Names -contains [string]$key) {
                return $Object[$key]
            }
        }
        foreach ($key in $Object.Keys) {
            $found = Find-DeepValue -Object $Object[$key] -Names $Names -Depth ($Depth + 1)
            if ($null -ne $found) { return $found }
        }
        return $null
    }

    if ($Object -is [System.Management.Automation.PSCustomObject]) {
        foreach ($property in $Object.PSObject.Properties) {
            if ($Names -contains $property.Name) {
                return $property.Value
            }
        }
        foreach ($property in $Object.PSObject.Properties) {
            $found = Find-DeepValue -Object $property.Value -Names $Names -Depth ($Depth + 1)
            if ($null -ne $found) { return $found }
        }
        return $null
    }

    if ($Object -is [System.Collections.IEnumerable] -and -not ($Object -is [string])) {
        foreach ($item in $Object) {
            $found = Find-DeepValue -Object $item -Names $Names -Depth ($Depth + 1)
            if ($null -ne $found) { return $found }
        }
    }
    return $null
}

function Test-Truthy {
    param($Value)
    if ($Value -is [bool]) { return $Value }
    if ($null -eq $Value) { return $false }
    return ([string]$Value -match '^(?i:true|1|yes)$')
}

function Convert-ToDouble {
    param($Value)
    if ($null -eq $Value) { return 0.0 }
    $number = 0.0
    if ([double]::TryParse([string]$Value, [System.Globalization.NumberStyles]::Any, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
        return $number
    }
    return 0.0
}

function Normalize-MapName {
    param($Value)
    if ($null -eq $Value) { return '' }
    $name = [string]$Value
    $name = [regex]::Replace($name, '^UEDPIE_\d+_', '')
    if ($name.Contains('/')) {
        $name = $name.Substring($name.LastIndexOf('/') + 1)
    }
    return $name
}

function Get-ProtectedAssetState {
    $paths = @(
        (Join-Path $ProjectRoot 'Content\FullSample\Player.uasset'),
        (Join-Path $ProjectRoot 'Content\DazToUnreal\Female\Female.uasset'),
        (Join-Path $ProjectRoot 'Content\DazToUnreal\Multiple\Multiple.uasset'),
        (Join-Path $ProjectRoot 'Content\DazToUnreal\Male\Male.uasset')
    )
    $rows = foreach ($path in $paths) {
        $item = Get-Item -LiteralPath $path
        [ordered]@{
            path = $path
            length = $item.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
        }
    }
    return @($rows)
}

function Get-GitPorcelain {
    $lines = @(& git -C $ProjectRoot status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to capture Git worktree state'
    }
    return $lines
}

function Stop-EditorCleanly {
    if ($null -eq $script:EditorProcess) {
        $script:Cleanup = 'NO_PROCESS'
        return
    }
    try { $script:EditorProcess.Refresh() } catch {}
    if ($script:EditorProcess.HasExited) {
        $script:Cleanup = 'EXITED'
        return
    }

    if ($null -ne $script:McpUri -and $null -ne $script:Headers) {
        try {
            if ($script:PieStarted) {
                [void](Invoke-UecpTool -Name 'level_actor' -Arguments @{ action = 'stop_play_in_editor' } -Record)
                $deadline = (Get-Date).AddSeconds(30)
                do {
                    Start-Sleep -Milliseconds 500
                    $status = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_pie_status'; client_index = 0 }
                    $running = Test-Truthy (Find-DeepValue -Object $status.data -Names @('pie_running', 'is_pie_running'))
                } while ($running -and (Get-Date) -lt $deadline)
            }
        }
        catch {}

        try {
            [void](Invoke-UecpTool -Name 'python_tools' -Arguments @{ action = 'execute_python'; code = "import unreal`nunreal.SystemLibrary.quit_editor()" } -Record)
        }
        catch {}
    }

    try {
        if ($script:EditorProcess.WaitForExit(60000)) {
            $script:Cleanup = 'CLEAN'
            return
        }
    }
    catch {}

    try {
        [void]$script:EditorProcess.CloseMainWindow()
        if ($script:EditorProcess.WaitForExit(15000)) {
            $script:Cleanup = 'CLOSE_MAIN_WINDOW'
            return
        }
    }
    catch {}

    Stop-Process -Id $script:EditorProcess.Id -Force -ErrorAction SilentlyContinue
    $script:Cleanup = 'FORCED'
}

$summary = [ordered]@{
    schema_version = 1
    run_id = $RunId
    project = $ProjectFile
    map = $MapPath
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    status = 'IN_PROGRESS'
    failure = $null
    editor_pid = $null
    mcp_port = $null
    structural_gate = $null
    screenshot = $null
    cleanup = $null
    git_state_match = $null
    protected_assets_match = $null
    run_dir = $RunDir
}

$gitBefore = Get-GitPorcelain
$assetsBefore = Get-ProtectedAssetState
$previousSkip = [Environment]::GetEnvironmentVariable('CODEX_SKIP_EF_STARTUP_HELPERS', 'Process')

try {
    $existing = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object {
        $null -ne $_.CommandLine -and $_.CommandLine.IndexOf($ProjectFile, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
    })
    if ($existing.Count -gt 0) {
        throw "An Unreal Editor process already targets this project: $($existing.ProcessId -join ', ')"
    }

    [Environment]::SetEnvironmentVariable('CODEX_SKIP_EF_STARTUP_HELPERS', '1', 'Process')
    $argumentLine = ('"{0}" -NoSplash -d3d12 -abslog="{1}"' -f $ProjectFile, $EditorLog)
    $script:EditorProcess = Start-Process -FilePath $EditorExe -ArgumentList $argumentLine -PassThru
    $summary.editor_pid = $script:EditorProcess.Id
    [Environment]::SetEnvironmentVariable('CODEX_SKIP_EF_STARTUP_HELPERS', $previousSkip, 'Process')

    $registryPath = Join-Path $env:LOCALAPPDATA "UECP\instances\$($script:EditorProcess.Id).json"
    $registryDeadline = (Get-Date).AddSeconds($EditorReadyTimeoutSeconds)
    while (-not (Test-Path -LiteralPath $registryPath -PathType Leaf)) {
        $script:EditorProcess.Refresh()
        if ($script:EditorProcess.HasExited) {
            throw "Unreal Editor exited before MCP registration (exit $($script:EditorProcess.ExitCode))"
        }
        if ((Get-Date) -ge $registryDeadline) {
            throw "Timed out waiting for MCP instance registry: $registryPath"
        }
        Start-Sleep -Milliseconds 500
    }

    $instance = Get-Content -Raw -LiteralPath $registryPath | ConvertFrom-Json
    $registeredProject = [System.IO.Path]::GetFullPath([string]$instance.project_path)
    if (-not $registeredProject.Equals([System.IO.Path]::GetFullPath($ProjectFile), [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "MCP registry project mismatch: $registeredProject"
    }

    $tokenPath = Join-Path $env:LOCALAPPDATA 'UECP\session_token.txt'
    if (-not (Test-Path -LiteralPath $tokenPath -PathType Leaf)) {
        throw "UECP session token not found: $tokenPath"
    }
    $token = (Get-Content -Raw -LiteralPath $tokenPath).Trim()
    if ($token -notmatch '^[0-9A-Fa-f]{64}$') {
        throw 'UECP session token has an unexpected shape'
    }

    $summary.mcp_port = [int]$instance.mcp_http_port
    $script:McpUri = "http://127.0.0.1:$($summary.mcp_port)/mcp"
    $script:Headers = @{ Authorization = "Bearer $token" }

    $healthDeadline = (Get-Date).AddSeconds(30)
    do {
        try {
            $health = Invoke-RestMethod -Uri "$($script:McpUri)/health" -Method Get -Headers $script:Headers -TimeoutSec 5
            $healthReady = $true
        }
        catch {
            $healthReady = $false
            Start-Sleep -Milliseconds 500
        }
    } while (-not $healthReady -and (Get-Date) -lt $healthDeadline)
    if (-not $healthReady) { throw 'UECP MCP health endpoint did not become ready' }
    Write-JsonFile -Value $health -Path (Join-Path $RunDir '000_mcp_health.json')

    $init = Invoke-UecpRpc -Method 'initialize' -Params ([ordered]@{
        protocolVersion = '2025-11-25'
        capabilities = @{}
        clientInfo = [ordered]@{ name = 'NSFWMigrationBaseline'; version = '1.0' }
    })
    Write-JsonFile -Value $init -Path (Join-Path $RunDir '001_mcp_initialize.json')
    if ([string]$init.result.protocolVersion -ne '2025-11-25') {
        throw "Unexpected MCP protocol version: $($init.result.protocolVersion)"
    }

    $logStart = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_pie_log'; line_count = 1 } -Record
    $t0 = Find-DeepValue -Object $logStart.data -Names @('current_timestamp', 'timestamp')

    [void](Invoke-UecpTool -Name 'level_actor' -Arguments @{ action = 'open_level'; level_path = $MapPath } -Record)
    $levelDeadline = (Get-Date).AddSeconds(90)
    $levelStable = 0
    do {
        Start-Sleep -Milliseconds 500
        $levelInfo = Invoke-UecpTool -Name 'level_actor' -Arguments @{ action = 'get_level_info' }
        $worldName = Normalize-MapName (Find-DeepValue -Object $levelInfo.data -Names @('world_name', 'map_name', 'level_name'))
        $actorCount = Convert-ToDouble (Find-DeepValue -Object $levelInfo.data -Names @('actor_count', 'num_actors'))
        if ($worldName -eq 'Test' -and $actorCount -gt 0) { $levelStable++ } else { $levelStable = 0 }
    } while ($levelStable -lt 3 -and (Get-Date) -lt $levelDeadline)
    if ($levelStable -lt 3) {
        [void](Save-ToolEvidence -ToolName 'level_actor' -Arguments @{ action = 'get_level_info' } -Response $levelInfo)
        throw "Level did not reach a stable Test state (world=$worldName actors=$actorCount)"
    }
    [void](Save-ToolEvidence -ToolName 'level_actor' -Arguments @{ action = 'get_level_info' } -Response $levelInfo)

    $begin = Invoke-UecpTool -Name 'level_actor' -Arguments @{ action = 'begin_play_in_editor' } -Record
    $blueprintErrors = Find-DeepValue -Object $begin.data -Names @('blueprints_with_errors', 'blueprint_errors')
    if ($null -ne $blueprintErrors -and @($blueprintErrors).Count -gt 0) {
        throw "PIE preflight found Blueprint errors: $($blueprintErrors | ConvertTo-Json -Depth 20 -Compress)"
    }
    $script:PieStarted = $true

    $pieDeadline = (Get-Date).AddSeconds($PieReadyTimeoutSeconds)
    $pieStable = 0
    do {
        Start-Sleep -Milliseconds 500
        $pieStatus = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_pie_status'; client_index = 0 }
        $playerState = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_pie_player_state' }
        $running = Test-Truthy (Find-DeepValue -Object $pieStatus.data -Names @('pie_running', 'is_pie_running'))
        $mapName = Normalize-MapName (Find-DeepValue -Object $pieStatus.data -Names @('map_name', 'world_name'))
        $timeSeconds = Convert-ToDouble (Find-DeepValue -Object $pieStatus.data -Names @('time_seconds', 'world_time_seconds'))
        $gameMode = [string](Find-DeepValue -Object $pieStatus.data -Names @('game_mode', 'game_mode_class'))
        $hasPawn = Test-Truthy (Find-DeepValue -Object $playerState.data -Names @('has_pawn', 'pawn_valid'))
        $controllerClass = [string](Find-DeepValue -Object $playerState.data -Names @('controller_class', 'player_controller_class'))
        $pawnClass = [string](Find-DeepValue -Object $playerState.data -Names @('pawn_class'))
        $ready = $running -and $mapName -eq 'Test' -and $timeSeconds -ge 3.0 -and -not [string]::IsNullOrWhiteSpace($gameMode) -and $hasPawn -and -not [string]::IsNullOrWhiteSpace($controllerClass) -and -not [string]::IsNullOrWhiteSpace($pawnClass)
        if ($ready) { $pieStable++ } else { $pieStable = 0 }
    } while ($pieStable -lt 3 -and (Get-Date) -lt $pieDeadline)
    if ($pieStable -lt 3) {
        [void](Save-ToolEvidence -ToolName 'play_test' -Arguments @{ action = 'get_pie_status'; client_index = 0 } -Response $pieStatus)
        [void](Save-ToolEvidence -ToolName 'play_test' -Arguments @{ action = 'get_pie_player_state' } -Response $playerState)
        throw "PIE structural gate timed out (running=$running map=$mapName time=$timeSeconds gameMode=$gameMode hasPawn=$hasPawn controller=$controllerClass pawn=$pawnClass)"
    }

    [void](Save-ToolEvidence -ToolName 'play_test' -Arguments @{ action = 'get_pie_status'; client_index = 0 } -Response $pieStatus)
    [void](Save-ToolEvidence -ToolName 'play_test' -Arguments @{ action = 'get_pie_player_state' } -Response $playerState)
    $runtimeState = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_player_runtime_state' } -Record
    $controllers = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_pie_actors'; class_filter = 'PlayerController'; max_count = 10 } -Record
    $pawns = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_pie_actors'; class_filter = 'Pawn'; max_count = 100 } -Record
    $widgets = Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'get_visible_widgets'; max_widgets = 200 } -Record

    $meshPython = @'
import json
import unreal

def path(obj):
    try:
        return obj.get_path_name() if obj else ""
    except Exception:
        return str(obj) if obj else ""

def cls_path(obj):
    try:
        return obj.get_class().get_path_name() if obj else ""
    except Exception:
        return ""

def prop(obj, name, default=None):
    if not obj:
        return default
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default

world = unreal.EditorLevelLibrary.get_game_world()
pc = unreal.GameplayStatics.get_player_controller(world, 0) if world else None
pawn = unreal.GameplayStatics.get_player_pawn(world, 0) if world else None
gm = unreal.GameplayStatics.get_game_mode(world) if world else None
player_state = prop(pc, "player_state")

meshes = []
if pawn:
    try:
        components = pawn.get_components_by_class(unreal.SkeletalMeshComponent)
    except Exception:
        components = []
    for component in components or []:
        try:
            mesh_asset = component.get_skeletal_mesh_asset()
        except Exception:
            mesh_asset = prop(component, "skeletal_mesh")
        try:
            anim_instance = component.get_anim_instance()
        except Exception:
            anim_instance = None
        try:
            attach_parent = component.get_attach_parent()
        except Exception:
            attach_parent = None
        try:
            visible = bool(component.is_visible())
        except Exception:
            visible = None
        meshes.append({
            "component": component.get_name(),
            "component_class": cls_path(component),
            "mesh_asset": path(mesh_asset),
            "visible": visible,
            "hidden_in_game": prop(component, "hidden_in_game"),
            "leader_pose_component": path(prop(component, "leader_pose_component")),
            "attach_parent": path(attach_parent),
            "anim_instance_class": cls_path(anim_instance),
        })
meshes.sort(key=lambda item: item["component"].lower())
payload = {
    "world": path(world),
    "game_mode_object": path(gm),
    "game_mode_class": cls_path(gm),
    "player_controller_object": path(pc),
    "player_controller_class": cls_path(pc),
    "pawn_object": path(pawn),
    "pawn_class": cls_path(pawn),
    "player_state_object": path(player_state),
    "player_state_class": cls_path(player_state),
    "skeletal_meshes": meshes,
}
print("NSFW_BASELINE_JSON=" + json.dumps(payload, sort_keys=True))
'@
    $meshResult = Invoke-UecpTool -Name 'python_tools' -Arguments @{ action = 'execute_python'; code = $meshPython } -Record
    $meshPayload = $null
    if ($meshResult.text -match 'NSFW_BASELINE_JSON=(\{.*\})') {
        $meshPayload = $matches[1] | ConvertFrom-Json
        Write-JsonFile -Value $meshPayload -Path (Join-Path $RunDir 'RuntimeMeshManifest.json')
    }
    else {
        throw 'Runtime mesh manifest marker was not returned by Unreal Python'
    }

    $toolScreenshot = $ScreenshotPath.Replace('\', '/')
    [void](Invoke-UecpTool -Name 'play_test' -Arguments @{ action = 'take_pie_screenshot'; file_path = $toolScreenshot; client_index = 0 } -Record)
    $screenshotDeadline = (Get-Date).AddSeconds(15)
    while ((-not (Test-Path -LiteralPath $ScreenshotPath -PathType Leaf) -or (Get-Item -LiteralPath $ScreenshotPath -ErrorAction SilentlyContinue).Length -le 0) -and (Get-Date) -lt $screenshotDeadline) {
        Start-Sleep -Milliseconds 250
    }
    if (-not (Test-Path -LiteralPath $ScreenshotPath -PathType Leaf)) {
        throw "PIE screenshot was not created: $ScreenshotPath"
    }

    Add-Type -AssemblyName System.Drawing
    $image = [System.Drawing.Image]::FromFile($ScreenshotPath)
    try {
        $width = $image.Width
        $height = $image.Height
    }
    finally {
        $image.Dispose()
    }
    if ($width -lt 320 -or $height -lt 200) {
        throw "PIE screenshot dimensions are invalid: ${width}x${height}"
    }
    $summary.screenshot = [ordered]@{
        path = $ScreenshotPath
        width = $width
        height = $height
        length = (Get-Item -LiteralPath $ScreenshotPath).Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $ScreenshotPath).Hash
        visual_review = 'PENDING'
    }

    $logArgs = @{ action = 'get_pie_log'; line_count = 5000 }
    if ($null -ne $t0 -and -not [string]::IsNullOrWhiteSpace([string]$t0)) { $logArgs.since_timestamp = $t0 }
    [void](Invoke-UecpTool -Name 'play_test' -Arguments $logArgs -Record)
    $warningArgs = $logArgs.Clone(); $warningArgs.severity_filter = 'warning'
    $errorArgs = $logArgs.Clone(); $errorArgs.severity_filter = 'error'
    [void](Invoke-UecpTool -Name 'play_test' -Arguments $warningArgs -Record)
    [void](Invoke-UecpTool -Name 'play_test' -Arguments $errorArgs -Record)

    $summary.structural_gate = [ordered]@{
        map = $mapName
        time_seconds = $timeSeconds
        game_mode = $gameMode
        controller_class = $controllerClass
        pawn_class = $pawnClass
        has_pawn = $hasPawn
        stable_samples = $pieStable
        mesh_count = @($meshPayload.skeletal_meshes).Count
        visible_widget_data_present = ($null -ne $widgets.data)
    }
    $summary.status = 'PASS_STRUCTURAL_VISUAL_PENDING'
}
catch {
    $summary.status = 'FAIL'
    $summary.failure = $_.Exception.Message
}
finally {
    [Environment]::SetEnvironmentVariable('CODEX_SKIP_EF_STARTUP_HELPERS', $previousSkip, 'Process')
    Stop-EditorCleanly
    $summary.cleanup = $script:Cleanup
    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')

    try {
        $gitAfter = Get-GitPorcelain
        $assetsAfter = Get-ProtectedAssetState
        $summary.git_state_match = (($gitBefore -join "`n") -ceq ($gitAfter -join "`n"))
        $summary.protected_assets_match = (($assetsBefore | ConvertTo-Json -Depth 10 -Compress) -ceq ($assetsAfter | ConvertTo-Json -Depth 10 -Compress))
        if (-not $summary.git_state_match -or -not $summary.protected_assets_match) {
            if ($summary.status -notlike 'FAIL*') { $summary.status = 'FAIL' }
            if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) {
                $summary.failure = 'Worktree or protected target assets changed during baseline PIE'
            }
        }
    }
    catch {
        $summary.git_state_match = $false
        $summary.protected_assets_match = $false
        if ($summary.status -notlike 'FAIL*') { $summary.status = 'FAIL' }
        if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) { $summary.failure = $_.Exception.Message }
    }

    if ($summary.cleanup -eq 'FORCED') {
        $summary.status = 'FAIL'
        if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) { $summary.failure = 'Editor required forced termination' }
    }

    Write-JsonFile -Value $summary -Path $SummaryPath

    $report = @(
        '# UE 5.8 baseline PIE',
        '',
        "- Status: ``$($summary.status)``",
        "- Map: ``$MapPath``",
        "- Editor PID: ``$($summary.editor_pid)``",
        "- MCP port: ``$($summary.mcp_port)``",
        "- Cleanup: ``$($summary.cleanup)``",
        "- Git state unchanged: ``$($summary.git_state_match)``",
        "- Protected assets unchanged: ``$($summary.protected_assets_match)``",
        "- Screenshot: ``$ScreenshotPath``",
        "- Editor log: ``$EditorLog``",
        "- Failure: ``$($summary.failure)``"
    )
    $report | Set-Content -LiteralPath $ReportPath -Encoding UTF8
}

"RESULT_DIR=$RunDir"
"SUMMARY=$SummaryPath"
"STATUS=$($summary.status)"
if ($summary.status -eq 'FAIL') {
    throw "Baseline PIE failed: $($summary.failure)"
}
