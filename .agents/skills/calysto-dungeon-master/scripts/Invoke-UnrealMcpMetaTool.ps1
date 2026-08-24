[CmdletBinding()]
param(
    [ValidateSet('list_toolsets', 'describe_toolset', 'call_tool')]
    [string]$ToolName = 'list_toolsets',
    [string]$ArgumentsJson = '{}',
    [string]$Uri = 'http://127.0.0.1:8000/mcp',
    [int]$TimeoutSec = 30,
    [switch]$Raw
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
    $arguments = $ArgumentsJson | ConvertFrom-Json
    $initializeBody = @{
        jsonrpc = '2.0'
        id = 1
        method = 'initialize'
        params = @{
            protocolVersion = '2025-11-25'
            capabilities = @{}
            clientInfo = @{
                name = 'Calysto-Dungeon-Master'
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

    $callBody = @{
        jsonrpc = '2.0'
        id = 2
        method = 'tools/call'
        params = @{
            name = $ToolName
            arguments = $arguments
        }
    } | ConvertTo-Json -Depth 30 -Compress

    $call = Invoke-McpPost -Body $callBody -SessionId $sessionId
    $payload = ConvertFrom-McpPayload -Content $call.Content
    if ($Raw) {
        $payload | ConvertTo-Json -Depth 100
        exit 0
    }

    if ($payload.error) {
        throw ($payload.error | ConvertTo-Json -Depth 20 -Compress)
    }
    $content = @($payload.result.content)
    foreach ($item in $content) {
        if ($item.type -eq 'text') {
            [string]$item.text
        }
        else {
            $item | ConvertTo-Json -Depth 30
        }
    }
}
finally {
    $client.Dispose()
    $handler.Dispose()
}
