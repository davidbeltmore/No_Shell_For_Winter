[CmdletBinding()]
param(
    [string]$Uri = 'http://127.0.0.1:8000/mcp',
    [string]$OutputPath = 'Saved\Migration\FoodKitAlcohol\UnrealMcpFoodKit58.json',
    [int]$TimeoutSec = 30
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http
$handler = New-Object System.Net.Http.HttpClientHandler
$handler.UseProxy = $false
$client = New-Object System.Net.Http.HttpClient($handler)
$client.Timeout = [TimeSpan]::FromSeconds($TimeoutSec)

function Invoke-Post([string]$Body, [string]$SessionId = '') {
    $request = New-Object System.Net.Http.HttpRequestMessage([Net.Http.HttpMethod]::Post, $Uri)
    try {
        $null = $request.Headers.TryAddWithoutValidation('Accept', 'application/json, text/event-stream')
        if ($SessionId) { $null = $request.Headers.TryAddWithoutValidation('Mcp-Session-Id', $SessionId) }
        $request.Content = New-Object System.Net.Http.StringContent($Body, [Text.Encoding]::UTF8, 'application/json')
        $response = $client.SendAsync($request, [Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        try {
            $mediaType = [string]$response.Content.Headers.ContentType.MediaType
            $data = $null
            if ($mediaType -eq 'text/event-stream') {
                $stream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
                $reader = New-Object IO.StreamReader($stream)
                try {
                    $timer = [Diagnostics.Stopwatch]::StartNew()
                    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSec) {
                        $task = $reader.ReadLineAsync()
                        if (-not $task.Wait([Math]::Max(1, [int](($TimeoutSec - $timer.Elapsed.TotalSeconds) * 1000)))) { throw 'MCP response timeout.' }
                        $line = $task.Result
                        if ($null -eq $line) { break }
                        if ($line.StartsWith('data:')) { $data = $line.Substring(5).Trim() }
                        elseif ($data -and [string]::IsNullOrWhiteSpace($line)) { break }
                    }
                }
                finally { $reader.Dispose(); $stream.Dispose() }
            }
            else { $data = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult() }
            if (-not $response.IsSuccessStatusCode) { throw "MCP HTTP $([int]$response.StatusCode): $data" }
            $values = $null
            $responseSession = ''
            if ($response.Headers.TryGetValues('Mcp-Session-Id', [ref]$values)) { $responseSession = [string]($values | Select-Object -First 1) }
            $payload = if ([string]::IsNullOrWhiteSpace($data)) { $null } else { $data | ConvertFrom-Json }
            return [pscustomobject]@{ Payload = $payload; SessionId = $responseSession }
        }
        finally { $response.Dispose() }
    }
    finally { $request.Dispose() }
}

function Invoke-Tool([int]$Id, [string]$Name, $Arguments, [string]$SessionId) {
    $body = @{ jsonrpc='2.0'; id=$Id; method='tools/call'; params=@{ name=$Name; arguments=$Arguments } } | ConvertTo-Json -Depth 20 -Compress
    return (Invoke-Post $body $SessionId).Payload
}

try {
    $initializeBody = @{ jsonrpc='2.0'; id=1; method='initialize'; params=@{ protocolVersion='2025-11-25'; capabilities=@{}; clientInfo=@{name='Codex-FoodKit58-Inspector';version='1.0'} } } | ConvertTo-Json -Depth 8 -Compress
    $initialize = Invoke-Post $initializeBody
    $session = $initialize.SessionId
    if (-not $session) { throw 'MCP session id missing.' }
    $null = Invoke-Post (@{jsonrpc='2.0';method='notifications/initialized'} | ConvertTo-Json -Compress) $session
    $tools = Invoke-Post (@{jsonrpc='2.0';id=2;method='tools/list';params=@{}} | ConvertTo-Json -Depth 5 -Compress) $session
    $list = Invoke-Tool 3 'list_toolsets' @{} $session

    $toolsets = @(
        'editor_toolset.toolsets.asset.AssetTools',
        'editor_toolset.toolsets.blueprint.BlueprintTools',
        'editor_toolset.toolsets.static_mesh.StaticMeshTools',
        'editor_toolset.toolsets.data_asset.DataAssetTools',
        'editor_toolset.toolsets.data_table.DataTableTools'
    )
    $descriptions = [ordered]@{}
    $id = 10
    foreach ($toolset in $toolsets) {
        $descriptions[$toolset] = Invoke-Tool $id 'describe_toolset' @{ toolset_name=$toolset } $session
        $id++
    }
    $readOnlyCalls = [ordered]@{}
    function Invoke-ReadOnlyTool([string]$Key, [string]$Toolset, [string]$ToolName, $Arguments) {
        $script:readOnlyCalls[$Key] = Invoke-Tool $script:id 'call_tool' @{
            toolset_name = $Toolset
            tool_name = $ToolName
            arguments = $Arguments
        } $session
        $script:id++
    }
    $appleMesh = @{ refPath='/Game/Food_Props_Kit/Meshes/SM_Apple1.SM_Apple1' }
    $applePickup = @{ refPath='/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Apple01.BP_Pickup_Food_Apple01' }
    $statusTable = @{ refPath='/Game/_Game/Data/Survival/DT_ProjectSurvivalStatuses.DT_ProjectSurvivalStatuses' }
    Invoke-ReadOnlyTool 'mesh_assets' $toolsets[0] 'find_assets' @{ folder_path='/Game/Food_Props_Kit/Meshes'; name=''; recursive=$false }
    Invoke-ReadOnlyTool 'registry_class' $toolsets[0] 'get_asset_class' @{ asset_path='/Game/_Game/FoodSystem/Food/Data/DA_FoodConsumableRegistry' }
    Invoke-ReadOnlyTool 'pickup_dirty' $toolsets[0] 'is_dirty' @{ asset_path='/Game/_Game/FoodSystem/Food/Items/PickuableItems/Food/BP_Pickup_Food_Apple01' }
    Invoke-ReadOnlyTool 'apple_bounds' $toolsets[2] 'get_bounds' @{ mesh=$appleMesh }
    Invoke-ReadOnlyTool 'apple_material_slots' $toolsets[2] 'get_material_slots' @{ mesh=$appleMesh }
    Invoke-ReadOnlyTool 'apple_material' $toolsets[2] 'get_material' @{ mesh=$appleMesh; slot_name='WorldGridMaterial' }
    Invoke-ReadOnlyTool 'apple_vertices' $toolsets[2] 'get_vertex_count' @{ mesh=$appleMesh; lod_index=0 }
    Invoke-ReadOnlyTool 'apple_triangles' $toolsets[2] 'get_triangle_count' @{ mesh=$appleMesh; lod_index=0 }
    Invoke-ReadOnlyTool 'pickup_parent' $toolsets[1] 'get_parent' @{ blueprint=$applePickup }
    Invoke-ReadOnlyTool 'status_rows' $toolsets[4] 'get_rows' @{ data_table=$statusTable; row_names=@('WellFed','Alcoholized') }
    Invoke-ReadOnlyTool 'status_row_names' $toolsets[4] 'list_rows' @{ data_table=$statusTable }
    function Get-ReturnValue($Call) {
        if ($Call.error) { throw ('MCP tool returned an error: ' + ($Call.error | ConvertTo-Json -Compress)) }
        return (($Call.result.content[0].text | ConvertFrom-Json).returnValue)
    }
    $meshAssets = @(Get-ReturnValue $readOnlyCalls.mesh_assets)
    $registryClass = [string](Get-ReturnValue $readOnlyCalls.registry_class)
    $pickupDirty = [bool](Get-ReturnValue $readOnlyCalls.pickup_dirty)
    $bounds = Get-ReturnValue $readOnlyCalls.apple_bounds
    $materialSlots = @(Get-ReturnValue $readOnlyCalls.apple_material_slots)
    $material = Get-ReturnValue $readOnlyCalls.apple_material
    $vertices = [int](Get-ReturnValue $readOnlyCalls.apple_vertices)
    $triangles = [int](Get-ReturnValue $readOnlyCalls.apple_triangles)
    $parent = Get-ReturnValue $readOnlyCalls.pickup_parent
    $statusRows = ([string](Get-ReturnValue $readOnlyCalls.status_rows)) | ConvertFrom-Json
    $statusRowNames = @(Get-ReturnValue $readOnlyCalls.status_row_names)
    if ($meshAssets.Count -ne 81) { throw "MCP mesh count is $($meshAssets.Count), expected 81." }
    if ($registryClass -ne 'ProjectSurvivalConsumableRegistry') { throw "MCP registry class differs: $registryClass" }
    if ($pickupDirty) { throw 'MCP sample pickup remains dirty.' }
    if (-not $bounds.isValid -or $bounds.max.x -le $bounds.min.x -or $bounds.max.y -le $bounds.min.y -or $bounds.max.z -le $bounds.min.z) { throw 'MCP sample mesh bounds are invalid.' }
    if ($materialSlots.Count -lt 1 -or -not $material.refPath) { throw 'MCP sample mesh material is missing.' }
    if ($vertices -le 0 -or $triangles -le 0) { throw 'MCP sample mesh geometry is empty.' }
    if ($parent.refPath -ne '/AscentCombatFramework/Blueprints/Actors/ACF_WorldItem_BP.ACF_WorldItem_BP_C') { throw "MCP pickup parent differs: $($parent.refPath)" }
    if ($statusRows.WellFed.activationThresholdNormalized -ne 0.9 -or $statusRows.WellFed.deactivationThresholdNormalized -ne 0.75) { throw 'MCP WellFed thresholds differ.' }
    if ($statusRows.Alcoholized.activationThresholdNormalized -ne 0.25 -or $statusRows.Alcoholized.deactivationThresholdNormalized -ne 0.1 -or $statusRows.Alcoholized.movementInputScale -ne 0.85) { throw 'MCP Alcoholized thresholds/movement differ.' }
    if ($statusRowNames -notcontains 'WellFed' -or $statusRowNames -notcontains 'Alcoholized') { throw 'MCP status rows are absent.' }
    $validation = [ordered]@{
        mesh_count = $meshAssets.Count
        registry_class = $registryClass
        pickup_dirty = $pickupDirty
        pickup_parent = $parent.refPath
        apple_bounds_valid = [bool]$bounds.isValid
        apple_material = $material.refPath
        apple_vertices_lod0 = $vertices
        apple_triangles_lod0 = $triangles
        well_fed_thresholds = @(0.9, 0.75)
        alcoholized_thresholds = @(0.25, 0.1)
        alcoholized_movement_scale = 0.85
        status_row_count = $statusRowNames.Count
    }
    $payload = [ordered]@{
        schema_version = 1
        generated_utc = [DateTime]::UtcNow.ToString('o')
        status = 'UE58_UNREAL_MCP_FOODKIT_READ_ONLY_INSPECTION_PASS'
        endpoint = $Uri
        protocol = $initialize.Payload.result.protocolVersion
        registered_meta_tools = @($tools.Payload.result.tools | ForEach-Object {$_.name})
        meta_tool_schemas = @($tools.Payload.result.tools)
        listed_toolsets = [string]$list.result.content[0].text
        described_toolsets = $descriptions
        read_only_calls = $readOnlyCalls
        validation = $validation
        mutation_calls = 0
    }
    $fullOutput = [IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputPath))
    New-Item -ItemType Directory -Path (Split-Path -Parent $fullOutput) -Force | Out-Null
    $payload | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $fullOutput -Encoding UTF8
    Write-Host "UE58_UNREAL_MCP_FOODKIT_READ_ONLY_INSPECTION_PASS: $fullOutput"
}
finally {
    $client.Dispose()
    $handler.Dispose()
}
