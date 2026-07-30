[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [string]$EngineRoot = 'D:\Unreal Engine 5\Library\UE_5.8',
    [string]$MapPath = '/Game/FullSample/Test',
    [int]$TimeoutSeconds = 360
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
}
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ProjectFile = Join-Path $ProjectRoot 'NoShellForWinter.uproject'
$EditorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$PythonScript = Join-Path $ProjectRoot 'Tools\Migration\BaselinePIE.py'
foreach ($path in @($ProjectFile, $EditorExe, $PythonScript)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file not found: $path" }
}

$RunId = Get-Date -Format 'yyyyMMdd_HHmmss'
$RunDir = Join-Path $ProjectRoot "Saved\Migration\PIE_Baseline_Python\$RunId"
$OutputFile = Join-Path $RunDir 'BaselinePIE_Output.txt'
$ResultFile = Join-Path $RunDir 'RuntimeResult.json'
$ViewportScreenshot = Join-Path $RunDir 'PIE_Test_Viewport.png'
$WindowScreenshot = Join-Path $RunDir 'PIE_Test_Window.png'
$EditorLog = Join-Path $RunDir 'UnrealEditor_BaselinePython.log'
$SummaryPath = Join-Path $RunDir 'Summary.json'
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

function Write-JsonFile {
    param([Parameter(Mandatory = $true)]$Value, [Parameter(Mandatory = $true)][string]$Path)
    $Value | ConvertTo-Json -Depth 80 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-GitPorcelain {
    $lines = @(& git -C $ProjectRoot status --porcelain=v1 --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw 'Unable to capture Git worktree state' }
    return $lines
}

function Get-ProtectedAssetState {
    $paths = @(
        (Join-Path $ProjectRoot 'Content\FullSample\Player.uasset'),
        (Join-Path $ProjectRoot 'Content\DazToUnreal\Female\Female.uasset'),
        (Join-Path $ProjectRoot 'Content\DazToUnreal\Multiple\Multiple.uasset'),
        (Join-Path $ProjectRoot 'Content\DazToUnreal\Male\Male.uasset')
    )
    return @($paths | ForEach-Object {
        $item = Get-Item -LiteralPath $_
        [ordered]@{ path = $_; length = $item.Length; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash }
    })
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class MigrationWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public sealed class WindowRecord {
        public IntPtr Handle;
        public string Title;
        public int Width;
        public int Height;
        public long Area;
    }
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextLength(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);

    public static WindowRecord[] GetWindowsForProcess(int processId) {
        var result = new List<WindowRecord>();
        EnumWindows(delegate(IntPtr handle, IntPtr ignored) {
            uint owner;
            GetWindowThreadProcessId(handle, out owner);
            if (owner != (uint)processId || !IsWindowVisible(handle)) return true;
            RECT rect;
            if (!GetWindowRect(handle, out rect)) return true;
            int width = rect.Right - rect.Left;
            int height = rect.Bottom - rect.Top;
            if (width <= 0 || height <= 0) return true;
            int length = GetWindowTextLength(handle);
            var title = new StringBuilder(Math.Max(length + 1, 2));
            GetWindowText(handle, title, title.Capacity);
            result.Add(new WindowRecord {
                Handle = handle,
                Title = title.ToString(),
                Width = width,
                Height = height,
                Area = (long)width * height
            });
            return true;
        }, IntPtr.Zero);
        return result.ToArray();
    }
}
'@

function Save-WindowScreenshot {
    param([System.Diagnostics.Process]$Process, [string]$Path)
    $deadline = (Get-Date).AddSeconds(8)
    $handle = [IntPtr]::Zero
    do {
        $Process.Refresh()
        $windows = @([MigrationWindowCapture]::GetWindowsForProcess($Process.Id))
        foreach ($popup in @($windows | Where-Object { $_.Title -eq 'Ultimate Engine Copilot' })) {
            [void][MigrationWindowCapture]::PostMessage($popup.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        }
        if (@($windows | Where-Object { $_.Title -eq 'Ultimate Engine Copilot' }).Count -gt 0) {
            Start-Sleep -Milliseconds 700
            $windows = @([MigrationWindowCapture]::GetWindowsForProcess($Process.Id))
        }
        $selected = $windows | Where-Object { $_.Title -ne 'Ultimate Engine Copilot' } | Sort-Object Area -Descending | Select-Object -First 1
        if ($null -ne $selected) { $handle = $selected.Handle }
        if ($handle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    if ($handle -eq [IntPtr]::Zero) { throw 'Unreal Editor main window handle was not available' }
    $topMost = [IntPtr](-1)
    $notTopMost = [IntPtr](-2)
    $positionFlags = [uint32]0x0043 # NOSIZE | NOMOVE | SHOWWINDOW
    if ([MigrationWindowCapture]::IsIconic($handle)) { [void][MigrationWindowCapture]::ShowWindow($handle, 9) }
    [void][MigrationWindowCapture]::SetWindowPos($handle, $topMost, 0, 0, 0, 0, $positionFlags)
    [void][MigrationWindowCapture]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 700
    try {
        $rect = New-Object MigrationWindowCapture+RECT
        if (-not [MigrationWindowCapture]::GetWindowRect($handle, [ref]$rect)) { throw 'GetWindowRect failed' }
        $width = $rect.Right - $rect.Left
        $height = $rect.Bottom - $rect.Top
        if ($width -lt 320 -or $height -lt 200) { throw "Unreal window dimensions are invalid: ${width}x${height}" }
        $bitmap = New-Object System.Drawing.Bitmap $width, $height
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
            $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
    finally {
        [void][MigrationWindowCapture]::SetWindowPos($handle, $notTopMost, 0, 0, 0, 0, $positionFlags)
    }
}

function Get-ImageMetadata {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $image = [System.Drawing.Image]::FromFile($Path)
    try { $width = $image.Width; $height = $image.Height } finally { $image.Dispose() }
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        path = $Path
        width = $width
        height = $height
        length = $item.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
        visual_review = 'PENDING'
    }
}

$summary = [ordered]@{
    schema_version = 1
    run_id = $RunId
    automation = 'Unreal Python built-in fallback'
    mcp_transport = 'PASS'
    uecp_tool_status = 'BLOCKED_LICENSE'
    project = $ProjectFile
    map = $MapPath
    started_utc = (Get-Date).ToUniversalTime().ToString('o')
    status = 'IN_PROGRESS'
    failure = $null
    editor_pid = $null
    cleanup = 'NOT_STARTED'
    runtime_result = $null
    window_screenshot = $null
    viewport_screenshot = $null
    git_state_match = $null
    protected_assets_match = $null
    log_counts = $null
    run_dir = $RunDir
}

$gitBefore = Get-GitPorcelain
$assetsBefore = Get-ProtectedAssetState
$previousEnvironment = [ordered]@{}
$environmentValues = [ordered]@{
    CODEX_SKIP_EF_STARTUP_HELPERS = '1'
    CODEX_RUN_MIGRATION_BASELINE_PIE = '1'
    CODEX_MIGRATION_PIE_SCRIPT = $PythonScript
    CODEX_MIGRATION_PIE_OUTPUT = $OutputFile
    CODEX_MIGRATION_PIE_RESULT = $ResultFile
    CODEX_MIGRATION_PIE_VIEWPORT_SCREENSHOT = $ViewportScreenshot
    CODEX_MIGRATION_PIE_MAP = $MapPath
    CODEX_MIGRATION_PIE_TIMEOUT = '240'
    CODEX_MIGRATION_PIE_CAPTURE_HOLD = '10'
}
$editorProcess = $null
$windowCaptured = $false
$forced = $false

try {
    $existing = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object {
        $null -ne $_.CommandLine -and $_.CommandLine.IndexOf($ProjectFile, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
    })
    if ($existing.Count -gt 0) { throw "An Unreal Editor process already targets this project: $($existing.ProcessId -join ', ')" }

    foreach ($entry in $environmentValues.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, [string]$entry.Value, 'Process')
    }

    $argumentLine = ('"{0}" -NoSplash -NoP4 -d3d12 -abslog="{1}"' -f $ProjectFile, $EditorLog)
    $editorProcess = Start-Process -FilePath $EditorExe -ArgumentList $argumentLine -PassThru
    $summary.editor_pid = $editorProcess.Id

    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $editorProcess.Refresh()
        if (-not $windowCaptured -and (Test-Path -LiteralPath $OutputFile -PathType Leaf)) {
            $output = Get-Content -Raw -LiteralPath $OutputFile
            if ($output -match 'SCREENSHOT_WINDOW_READY=True') {
                Save-WindowScreenshot -Process $editorProcess -Path $WindowScreenshot
                $windowCaptured = $true
            }
        }
        if ($editorProcess.HasExited) { break }
        if ((Get-Date) -ge $deadline) { throw "Baseline PIE timed out after $TimeoutSeconds seconds" }
        Start-Sleep -Milliseconds 250
    } while ($true)

    if (-not $editorProcess.HasExited) {
        if (-not $editorProcess.WaitForExit(30000)) { throw 'Unreal Editor did not exit after the runtime probe' }
    }
    $summary.cleanup = 'CLEAN'
    if ($editorProcess.ExitCode -ne 0) { throw "Unreal Editor exited with code $($editorProcess.ExitCode)" }
    if (-not (Test-Path -LiteralPath $ResultFile -PathType Leaf)) { throw "Runtime result was not created: $ResultFile" }

    $runtimeResult = Get-Content -Raw -LiteralPath $ResultFile | ConvertFrom-Json
    $summary.runtime_result = $runtimeResult
    if ([string]$runtimeResult.status -ne 'PASS') { throw "Runtime probe failed: $($runtimeResult.failure)" }
    if (-not $windowCaptured) { throw 'The visible Unreal window screenshot was not captured' }

    $summary.window_screenshot = Get-ImageMetadata -Path $WindowScreenshot
    $summary.viewport_screenshot = Get-ImageMetadata -Path $ViewportScreenshot
    if ($null -eq $summary.window_screenshot) { throw 'Visible window screenshot metadata is missing' }

    $summary.status = 'PASS_STRUCTURAL_VISUAL_PENDING'
}
catch {
    $summary.status = 'FAIL'
    $summary.failure = $_.Exception.Message
}
finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    if ($null -ne $editorProcess) {
        try { $editorProcess.Refresh() } catch {}
        if (-not $editorProcess.HasExited) {
            try {
                [void]$editorProcess.CloseMainWindow()
                if (-not $editorProcess.WaitForExit(15000)) {
                    Stop-Process -Id $editorProcess.Id -Force -ErrorAction SilentlyContinue
                    $forced = $true
                }
            }
            catch {
                Stop-Process -Id $editorProcess.Id -Force -ErrorAction SilentlyContinue
                $forced = $true
            }
        }
    }
    if ($forced) {
        $summary.cleanup = 'FORCED'
        $summary.status = 'FAIL'
        if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) { $summary.failure = 'Editor required forced termination' }
    }

    try {
        $gitAfter = Get-GitPorcelain
        $assetsAfter = Get-ProtectedAssetState
        $summary.git_state_match = (($gitBefore -join "`n") -ceq ($gitAfter -join "`n"))
        $summary.protected_assets_match = (($assetsBefore | ConvertTo-Json -Depth 10 -Compress) -ceq ($assetsAfter | ConvertTo-Json -Depth 10 -Compress))
        if (-not $summary.git_state_match -or -not $summary.protected_assets_match) {
            $summary.status = 'FAIL'
            if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) { $summary.failure = 'Worktree or protected target assets changed during baseline PIE' }
        }
    }
    catch {
        $summary.git_state_match = $false
        $summary.protected_assets_match = $false
        $summary.status = 'FAIL'
        if ([string]::IsNullOrWhiteSpace([string]$summary.failure)) { $summary.failure = $_.Exception.Message }
    }

    if (Test-Path -LiteralPath $EditorLog -PathType Leaf) {
        $logText = Get-Content -Raw -LiteralPath $EditorLog
        $summary.log_counts = [ordered]@{
            fatal = @([regex]::Matches($logText, '(?im)^.*Fatal error.*$')).Count
            ensure = @([regex]::Matches($logText, '(?im)^.*Ensure condition failed.*$')).Count
            error_lines = @([regex]::Matches($logText, '(?im)^.*(?:Log\w+: Error:|\[Error\]).*$')).Count
            warning_lines = @([regex]::Matches($logText, '(?im)^.*(?:Log\w+: Warning:|\[Warning\]).*$')).Count
        }
    }
    $summary.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
    Write-JsonFile -Value $summary -Path $SummaryPath
}

"RESULT_DIR=$RunDir"
"SUMMARY=$SummaryPath"
"STATUS=$($summary.status)"
if ($summary.status -eq 'FAIL') { throw "Baseline PIE Python fallback failed: $($summary.failure)" }
