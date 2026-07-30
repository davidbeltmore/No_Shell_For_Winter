[CmdletBinding()]
param(
    [string]$Uri = 'http://127.0.0.1:8000/mcp',
    [int]$TimeoutSec = 15
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Net.Http

$handler = New-Object System.Net.Http.HttpClientHandler
$handler.UseProxy = $false
$client = New-Object System.Net.Http.HttpClient($handler)
$client.Timeout = [TimeSpan]::FromSeconds($TimeoutSec)

function Invoke-McpPost {
    param(
        [Parameter(Mandatory)] [string]$Body,
        [string]$SessionId
    )

    $request = New-Object System.Net.Http.HttpRequestMessage(
        [System.Net.Http.HttpMethod]::Post,
        $Uri
    )
    try {
        $null = $request.Headers.TryAddWithoutValidation(
            'Accept',
            'application/json, text/event-stream'
        )
        if (-not [string]::IsNullOrWhiteSpace($SessionId)) {
            $null = $request.Headers.TryAddWithoutValidation(
                'Mcp-Session-Id',
                $SessionId
            )
        }
        $request.Content = New-Object System.Net.Http.StringContent(
            $Body,
            [Text.Encoding]::UTF8,
            'application/json'
        )

        $response = $client.SendAsync(
            $request,
            [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead
        ).GetAwaiter().GetResult()
        try {
            $mediaType = [string]$response.Content.Headers.ContentType.MediaType
            if ($mediaType -eq 'text/event-stream') {
                $stream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
                $reader = New-Object System.IO.StreamReader($stream)
                try {
                    $lines = New-Object System.Collections.Generic.List[string]
                    $receivedData = $false
                    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
                    while ($stopwatch.Elapsed.TotalSeconds -lt $TimeoutSec) {
                        $remainingMs = [Math]::Max(
                            1,
                            [int](($TimeoutSec - $stopwatch.Elapsed.TotalSeconds) * 1000)
                        )
                        $readTask = $reader.ReadLineAsync()
                        if (-not $readTask.Wait($remainingMs)) {
                            throw 'Timed out waiting for the Unreal MCP SSE result.'
                        }
                        $line = $readTask.Result
                        if ($null -eq $line) {
                            break
                        }
                        $lines.Add($line)
                        if ($line.StartsWith('data:')) {
                            $receivedData = $true
                        }
                        elseif ($receivedData -and [string]::IsNullOrWhiteSpace($line)) {
                            break
                        }
                    }
                    if (-not $receivedData) {
                        throw 'The Unreal MCP SSE response contained no data event.'
                    }
                    $responseBody = $lines -join "`r`n"
                }
                finally {
                    $reader.Dispose()
                    $stream.Dispose()
                }
            }
            else {
                $responseBody = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            }
            if (-not $response.IsSuccessStatusCode) {
                throw "MCP HTTP $([int]$response.StatusCode): $responseBody"
            }

            $responseSessionId = $null
            $values = $null
            if ($response.Headers.TryGetValues('Mcp-Session-Id', [ref]$values)) {
                $responseSessionId = [string]($values | Select-Object -First 1)
            }

            [pscustomobject]@{
                StatusCode = [int]$response.StatusCode
                Content = $responseBody
                SessionId = $responseSessionId
            }
        }
        finally {
            $response.Dispose()
        }
    }
    finally {
        $request.Dispose()
    }
}

function ConvertFrom-McpPayload {
    param([Parameter(Mandatory)] [string]$Content)

    $dataMatches = [regex]::Matches($Content, '(?m)^data:\s*(.+)\r?$')
    if ($dataMatches.Count -gt 0) {
        return ($dataMatches[$dataMatches.Count - 1].Groups[1].Value | ConvertFrom-Json)
    }
    return ($Content | ConvertFrom-Json)
}

try {
    $initializeBody = @{
        jsonrpc = '2.0'
        id = 1
        method = 'initialize'
        params = @{
            protocolVersion = '2025-11-25'
            capabilities = @{}
            clientInfo = @{
                name = 'NoShellForWinter-MCP-Probe'
                version = '1.0'
            }
        }
    } | ConvertTo-Json -Depth 8 -Compress

    $initialize = Invoke-McpPost -Body $initializeBody
    $sessionId = $initialize.SessionId
    if ([string]::IsNullOrWhiteSpace($sessionId)) {
        throw 'The Unreal MCP server did not return Mcp-Session-Id.'
    }

    $initializedBody = @{
        jsonrpc = '2.0'
        method = 'notifications/initialized'
    } | ConvertTo-Json -Compress
    $null = Invoke-McpPost -Body $initializedBody -SessionId $sessionId

    $toolsBody = @{
        jsonrpc = '2.0'
        id = 2
        method = 'tools/list'
        params = @{}
    } | ConvertTo-Json -Depth 4 -Compress
    $tools = Invoke-McpPost -Body $toolsBody -SessionId $sessionId
    $initializePayload = ConvertFrom-McpPayload -Content $initialize.Content
    $toolsPayload = ConvertFrom-McpPayload -Content $tools.Content
    $toolNames = @($toolsPayload.result.tools | ForEach-Object { $_.name })

    $listToolsetsBody = @{
        jsonrpc = '2.0'
        id = 3
        method = 'tools/call'
        params = @{
            name = 'list_toolsets'
            arguments = @{}
        }
    } | ConvertTo-Json -Depth 6 -Compress
    $listToolsets = Invoke-McpPost -Body $listToolsetsBody -SessionId $sessionId
    $listToolsetsPayload = ConvertFrom-McpPayload -Content $listToolsets.Content
    $toolsetSummary = [string]$listToolsetsPayload.result.content[0].text

    [pscustomobject]@{
        status = 'PASS'
        endpoint = $Uri
        protocol = $initializePayload.result.protocolVersion
        session_id = $sessionId
        tool_count = $toolNames.Count
        tools = $toolNames
        toolsets = $toolsetSummary
    } | ConvertTo-Json -Depth 5
}
catch {
    [pscustomobject]@{
        status = 'FAIL'
        endpoint = $Uri
        error = $_.Exception.Message
        hint = 'Open NoShellForWinter in Unreal Editor 5.8 and check LogModelContextProtocol.'
    } | ConvertTo-Json -Depth 4
    exit 1
}
finally {
    $client.Dispose()
    $handler.Dispose()
}
