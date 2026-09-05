[CmdletBinding()]
param(
    [ValidateSet('list_toolsets', 'describe_toolset', 'call_tool')]
    [string]$ToolName = 'list_toolsets',
    [string]$ArgumentsJson = '{}',
    [string]$Uri = 'http://127.0.0.1:8000/mcp',
    [ValidateRange(1, 300)]
    [int]$TimeoutSec = 30,
    [switch]$Raw
)

$ErrorActionPreference = 'Stop'
$deadlineClock = [Diagnostics.Stopwatch]::StartNew()
Add-Type -AssemblyName System.Net.Http

$handler = New-Object System.Net.Http.HttpClientHandler
$handler.UseProxy = $false
$client = New-Object System.Net.Http.HttpClient($handler)
$client.Timeout = [System.Threading.Timeout]::InfiniteTimeSpan
$deadlineCancellation = New-Object System.Threading.CancellationTokenSource
$deadlineCancellation.CancelAfter([Math]::Max(
    1, [int](($TimeoutSec * 1000.0) - $deadlineClock.Elapsed.TotalMilliseconds)
))

function Get-McpRemainingMilliseconds {
    $remaining = ($TimeoutSec * 1000.0) - $deadlineClock.Elapsed.TotalMilliseconds
    if ($remaining -le 0) {
        $deadlineCancellation.Cancel()
        throw "Unreal MCP exceeded its ${TimeoutSec}s end-to-end deadline."
    }
    return [Math]::Max(1, [int][Math]::Ceiling($remaining))
}

function Wait-McpTask {
    param(
        [Parameter(Mandatory)] [System.Threading.Tasks.Task]$Task,
        [Parameter(Mandatory)] [string]$Operation
    )
    $remainingMs = Get-McpRemainingMilliseconds
    if (-not $Task.Wait($remainingMs)) {
        $deadlineCancellation.Cancel()
        throw "Unreal MCP timed out during $Operation within its ${TimeoutSec}s end-to-end deadline."
    }
    # Recheck after completion: a late result cannot extend the shared deadline.
    $null = Get-McpRemainingMilliseconds
    return $Task.GetAwaiter().GetResult()
}

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

        $null = Get-McpRemainingMilliseconds
        $sendTask = $client.SendAsync(
            $request,
            [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead,
            $deadlineCancellation.Token
        )
        $response = Wait-McpTask -Task $sendTask -Operation 'response headers'
        try {
            $mediaType = [string]$response.Content.Headers.ContentType.MediaType
            if ($mediaType -eq 'text/event-stream') {
                $stream = Wait-McpTask -Task $response.Content.ReadAsStreamAsync() -Operation 'SSE stream'
                $reader = New-Object System.IO.StreamReader($stream)
                try {
                    $lines = New-Object System.Collections.Generic.List[string]
                    $receivedData = $false
                    while ($true) {
                        $readTask = $reader.ReadLineAsync()
                        $line = Wait-McpTask -Task $readTask -Operation 'SSE event body'
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
                $responseBody = Wait-McpTask -Task $response.Content.ReadAsStringAsync() -Operation 'JSON response body'
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
    $null = Get-McpRemainingMilliseconds
    if ($Raw) {
        $payload | ConvertTo-Json -Depth 100
    }

    if ($payload.error) {
        throw ($payload.error | ConvertTo-Json -Depth 20 -Compress)
    }
    if ($payload.result.isError -eq $true) {
        throw ('Unreal MCP tool reported isError: ' + ($payload.result | ConvertTo-Json -Depth 20 -Compress))
    }
    if ($Raw) {
        exit 0
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
    $deadlineCancellation.Cancel()
    $client.Dispose()
    $handler.Dispose()
    $deadlineCancellation.Dispose()
}
